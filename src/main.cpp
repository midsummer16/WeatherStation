#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <SHT2x.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include "config.h"

// ======================== DATA TYPES ========================
struct DailyForecast {
  String date, textDay;
  int codeDay, high, low;
};

struct WeatherData {
  String text;
  int code = -1, temp = 0, feelsLike = 0, humidity = 0;
  DailyForecast forecast[3];
  bool valid = false;
  unsigned long lastUpdate = 0;
};

struct Raindrop { int16_t x, y; uint8_t speed, len; };
struct Snowflake { int16_t x, y; int8_t speed, size; float drift; };
struct CloudP { int16_t x, y; uint8_t speed, w, h; };

// ======================== GLOBALS ========================
SHT2x sht20;
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft); // 彻底移除易导致虚函数 Bug 的 canvas 指针
bool hasSHT20 = false;
float temperature = NAN, humidity = NAN;

bool wifiConnected = false, timeSynced = false, screenDimmed = false;
struct tm timeinfo;
WeatherData weather;

unsigned long lastSensorRead = 0, lastWeatherFetch = 0, lastAnimFrame = 0;
unsigned long lastNtpSync = 0, lastWiFiAttempt = 0, lastFullDraw = 0;
unsigned long lastActivity = 0;
volatile bool bootPressed = false;
bool fullRedraw = true;

float animPhase = 0;
bool flashState = false;
char tmpBuf[64];

// Particles
#define MAX_DROPS 40
#define MAX_FLAKES 30
#define MAX_CLOUDS 4
Raindrop drops[MAX_DROPS];
Snowflake flakes[MAX_FLAKES];
CloudP clouds[MAX_CLOUDS];

// ======================== 横屏布局参数 (Landscape 320x240) ========================
const int SW = 320, SH = 240;
const int STATUS_H = 22;
const int CLOCK_Y = 28;      // 时钟 Y 坐标
const int DATE_Y = 76;       // 日期 Y 坐标
const int SENSOR_Y = 100;    // 传感器卡片 Y 坐标
const int SENSOR_H = 48;     // 传感器卡片高度
const int FC_Y = 158;        // 天气预报卡片 Y 坐标
const int FC_H = 78;         // 天气预报卡片高度

// Colors
#define CARD_BG      0x4228
#define CARD_BORDER  0x6B4D
#define TXT_WHITE    0xFFFF
#define TXT_GRAY     0xBDF7
#define TXT_DIM      0x8C71
#define CLR_TEMP     0xFD20
#define CLR_HUMI     0x07FF
#define CLR_GOLD     0xFEC0
#define CLR_GREEN    0x07E0

// ======================== FWD DECLARATIONS ========================
void connectWiFi();
bool syncNtpTime();
bool fetchWeatherNow();
bool fetchWeatherDaily();
void drawStatusBar();
void drawClock();
void drawDate();
void drawSensorData();
void drawForecast();
void drawWeatherIcon(int x, int y, int code, int r);
void updateParticles();
void drawParticles();
void initAnimation();
void drawAll();
void setBacklight(int level);
void IRAM_ATTR onBootPress();

// ======================== BACKLIGHT ========================
void setupBacklight() {
  ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_RES);
  ledcAttachPin(LCD_BL, BL_PWM_CH);
  setBacklight(BL_FULL);
}

void setBacklight(int level) {
  ledcWrite(BL_PWM_CH, constrain(level, 0, 255));
}

void IRAM_ATTR onBootPress() {
  bootPressed = true;
}

void checkDimming() {
  if (screenDimmed) {
    if (bootPressed) {
      bootPressed = false;
      screenDimmed = false;
      setBacklight(BL_FULL);
      lastActivity = millis();
      fullRedraw = true;
    }
    return;
  }
  if (millis() - lastActivity > DIM_TIMEOUT_MS) {
    screenDimmed = true;
    setBacklight(BL_DIM);
  }
}

// ======================== WIFI ========================
void connectWiFi() {
  if (wifiConnected && WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int att = 0;
  while (WiFi.status() != WL_CONNECTED && att < 40) { delay(250); att++; }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
}

// ======================== NTP ========================
bool syncNtpTime() {
  configTime(TZ_OFFSET, 0, NTP_SERVER1, NTP_SERVER2);
  int t = 0;
  while (!getLocalTime(&timeinfo) && t < 20) { delay(250); t++; }
  timeSynced = getLocalTime(&timeinfo);
  return timeSynced;
}

// ======================== WEATHER API ========================
bool fetchWeatherNow() {
  if (!wifiConnected) return false;
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://api.seniverse.com/v3/weather/now.json?key="
             + String(SENIVERSE_KEY) + "&location=" + WEATHER_LOC
             + "&language=zh-Hans&unit=c";
  http.begin(client, url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, http.getStream())) { http.end(); return false; }
  weather.text = doc["results"][0]["now"]["text"].as<String>();
  weather.code = doc["results"][0]["now"]["code"].as<int>();
  weather.temp = doc["results"][0]["now"]["temperature"].as<int>();
  weather.feelsLike = doc["results"][0]["now"]["feels_like"].as<int>();
  weather.humidity = doc["results"][0]["now"]["humidity"].as<int>();
  http.end();
  weather.valid = true;
  weather.lastUpdate = millis();
  return true;
}

bool fetchWeatherDaily() {
  if (!wifiConnected) return false;
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  String url = "https://api.seniverse.com/v3/weather/daily.json?key="
             + String(SENIVERSE_KEY) + "&location=" + WEATHER_LOC
             + "&language=en&unit=c&start=0&days=3";
  http.begin(client, url);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, http.getStream())) { http.end(); return false; }
  for (int i = 0; i < 3; i++) {
    weather.forecast[i].date = doc["results"][0]["daily"][i]["date"].as<String>();
    weather.forecast[i].textDay = doc["results"][0]["daily"][i]["text_day"].as<String>();
    weather.forecast[i].codeDay = doc["results"][0]["daily"][i]["code_day"].as<int>();
    weather.forecast[i].high = doc["results"][0]["daily"][i]["high"].as<int>();
    weather.forecast[i].low = doc["results"][0]["daily"][i]["low"].as<int>();
  }
  http.end();
  return true;
}

// ======================== ANIMATION ========================
int getAnimType() {
  if (!weather.valid) return -1;
  int c = weather.code;
  if (c == 0) return 0;       // Sunny
  if (c == 1) return 1;       // Cloudy
  if (c == 2) return 2;       // Overcast
  if (c >= 3 && c <= 6) return 3;
  if (c == 11) return 3;      // Shower -> rain
  if (c >= 7 && c <= 10) return 4;
  if (c == 14) return 4;      // Sleet -> snow
  if (c == 12) return 5;      // Thunder
  if (c == 15 || c == 16) return 6;
  if (c == 17) return 7;      // Windy
  return 1;
}

void initAnimation() {
  randomSeed(millis());
  for (int i = 0; i < MAX_DROPS; i++) {
    drops[i].x = random(0, SW);
    drops[i].y = random(-SH, 0);
    drops[i].speed = random(4, 10);
    drops[i].len = random(6, 14);
  }
  for (int i = 0; i < MAX_FLAKES; i++) {
    flakes[i].x = random(0, SW);
    flakes[i].y = random(-SH, 0);
    flakes[i].speed = random(1, 4);
    flakes[i].size = random(2, 5);
    flakes[i].drift = random(-10, 10) * 0.01;
  }
  for (int i = 0; i < MAX_CLOUDS; i++) {
    clouds[i].x = random(-SW, 0);
    clouds[i].y = random(8, 60);
    clouds[i].speed = random(1, 3);
    clouds[i].w = random(40, 80);
    clouds[i].h = random(16, 28);
  }
  animPhase = 0;
  flashState = false;
}

uint16_t getBgColor() {
  if (!weather.valid) return 0x2124;
  switch (weather.code) {
    case 0: return 0x04B2;     // Sunny blue
    case 1: return 0x3A69;     // Cloudy
    case 2: return 0x2A28;     // Overcast
    case 3 ... 6: case 11: return 0x18A3; // Rain
    case 7 ... 10: case 14: return 0x6B6D; // Snow
    case 12: return 0x0821;    // Thunder
    case 15: case 16: return 0x7B4D; // Fog
    case 17: return 0x2945;    // Windy
    default: return 0x3A69;
  }
}

void updateParticles() {
  animPhase += 0.03;
  if (animPhase > TWO_PI) animPhase -= TWO_PI;
  int at = getAnimType();
  if (at == 3 || at == 5) {
    for (int i = 0; i < MAX_DROPS; i++) {
      drops[i].y += drops[i].speed;
      drops[i].x += (at == 5) ? 3 : 0;
      if (drops[i].y > SH) {
        drops[i].y = random(-20, -5);
        drops[i].x = random(0, SW);
        drops[i].speed = random(4, 10);
      }
    }
    if (at == 5) flashState = (random(0, 30) == 0);
  }
  if (at == 4) {
    for (int i = 0; i < MAX_FLAKES; i++) {
      flakes[i].y += flakes[i].speed;
      flakes[i].x += sin(animPhase + flakes[i].drift * 10) * 0.6;
      if (flakes[i].y > SH) {
        flakes[i].y = random(-10, -2);
        flakes[i].x = random(0, SW);
      }
    }
  }
  if (at == 1 || at == 2) {
    for (int i = 0; i < MAX_CLOUDS; i++) {
      clouds[i].x += clouds[i].speed;
      if (clouds[i].x > SW + clouds[i].w) {
        clouds[i].x = -clouds[i].w - 20;
        clouds[i].y = random(8, 60);
      }
    }
  }
}

void drawParticles() {
  int at = getAnimType();
  if (at == -1) return;
  if (at == 0) {
    int cx = SW - 35, cy = 35, r = 16;
    spr.fillCircle(cx, cy, r, 0xFEC0);
    for (int i = 0; i < 8; i++) {
      float a = animPhase * 0.4 + i * PI / 4;
      int x2 = cx + cos(a) * (r + 5);
      int y2 = cy + sin(a) * (r + 5);
      spr.drawLine(cx, cy, x2, y2, 0xFDA0);
    }
    spr.fillCircle(cx, cy, r - 2, 0xFFE0);
    return;
  }
  if (at == 3 || at == 5) {
    for (int i = 0; i < MAX_DROPS; i++)
      spr.fillRect(drops[i].x, drops[i].y, 1, drops[i].len, 0x6B8F);
    return;
  }
  if (at == 4) {
    for (int i = 0; i < MAX_FLAKES; i++)
      spr.fillCircle(flakes[i].x, flakes[i].y, flakes[i].size, 0xFFFF);
    return;
  }
  if (at == 1 || at == 2) {
    uint16_t col = (at == 1) ? 0xDF7A : 0x7B4D;
    for (int i = 0; i < MAX_CLOUDS; i++) {
      spr.fillEllipse(clouds[i].x, clouds[i].y, clouds[i].w, clouds[i].h, col);
      spr.fillEllipse(clouds[i].x - clouds[i].w / 3, clouds[i].y - 4,
                      clouds[i].w / 2, clouds[i].h * 0.7, col);
      spr.fillEllipse(clouds[i].x + clouds[i].w / 3, clouds[i].y - 2,
                      clouds[i].w / 2, clouds[i].h * 0.7, col);
    }
    return;
  }
  if (at == 7) {
    for (int y = 15; y < SH; y += 25) {
      float ph = animPhase + y * 0.08;
      spr.drawLine(5, y, 35, y - 4 + sin(ph) * 4, 0xBDF7);
      spr.drawLine(55, y + 8, 85, y + 4 + sin(ph + 1) * 4, 0xBDF7);
    }
    return;
  }
}

// ======================== UI DRAWING ========================
void drawWeatherIcon(int x, int y, int code, int r) {
  if (code == 0) {
    spr.fillCircle(x, y, r, 0xFEC0);
    for (int i = 0; i < 6; i++) {
      float a = i * PI / 3;
      spr.drawLine(x + cos(a)*(r+2), y + sin(a)*(r+2),
                   x + cos(a)*(r+5), y + sin(a)*(r+5), 0xFEC0);
    }
  } else if (code == 1) {
    spr.fillCircle(x-4, y-2, r*0.6, 0xFFFF);
    spr.fillCircle(x+5, y, r*0.7, 0xFFFF);
    spr.fillCircle(x, y+2, r*0.5, 0xDEDB);
  } else if (code == 2) {
    spr.fillCircle(x-4, y-2, r*0.6, 0xBDF7);
    spr.fillCircle(x+5, y, r*0.7, 0xBDF7);
    spr.fillCircle(x, y+2, r*0.5, 0x8C51);
  } else if (code >= 3 && code <= 6) {
    spr.fillCircle(x-4, y-4, r*0.5, 0x9CD3);
    spr.fillCircle(x+5, y-2, r*0.6, 0x9CD3);
    spr.fillCircle(x, y, r*0.45, 0x8C51);
    for (int i = 0; i < 4; i++)
      spr.drawLine(x-2+i*3, y+4, x-2+i*3, y+4+r*0.6, 0x6B8F);
  } else if (code >= 7 && code <= 10) {
    spr.fillCircle(x-4, y-4, r*0.5, 0xEF5B);
    spr.fillCircle(x+5, y-2, r*0.6, 0xEF5B);
    spr.fillCircle(x, y, r*0.45, 0xC618);
    for (int i = 0; i < 4; i++)
      spr.fillCircle(x-2+i*3, y+6+i*2, 2, 0xFFFF);
  } else if (code == 12) {
    spr.fillCircle(x-4, y-4, r*0.5, 0x8C51);
    spr.fillCircle(x+5, y-2, r*0.6, 0x8C51);
    spr.drawLine(x, y+3, x-2, y+r+2, 0xFEC0);
    spr.drawLine(x, y+3, x+3, y+r+2, 0xFEC0);
  } else if (code == 15 || code == 16) {
    spr.fillCircle(x, y, r, 0xBDF7);
    spr.drawCircle(x, y, r, 0x9CD3);
  } else {
    spr.fillCircle(x, y, r, 0xFFFF);
  }
}

void drawStatusBar() {
  spr.fillRect(0, 0, SW, STATUS_H, CARD_BG);
  spr.drawRect(0, 0, SW, STATUS_H, CARD_BORDER);
  spr.setTextSize(1);
  spr.setTextFont(2);
  
  if (wifiConnected) {
    spr.fillCircle(12, STATUS_H / 2, 3, CLR_GREEN);
  } else {
    spr.drawCircle(12, STATUS_H / 2, 3, TXT_DIM);
  }
  
  spr.setTextColor(CLR_GOLD, CARD_BG);
  spr.setCursor(22, 4);
  String locStr = WEATHER_LOC;
  locStr.toUpperCase();
  spr.print(locStr);
  
  if (weather.valid) {
    spr.setTextColor(TXT_WHITE, CARD_BG);
    spr.setTextDatum(TR_DATUM);
    spr.drawString(weather.text + " " + weather.temp + "`C", SW - 8, 4);
    spr.setTextDatum(TL_DATUM);
  }
}

void drawClock() {
  uint16_t bg = getBgColor();
  spr.setTextDatum(TC_DATUM);
  
  if (!timeSynced) {
    spr.setTextColor(TXT_DIM, bg);
    spr.setTextFont(4);
    spr.drawString("--:--:--", SW / 2, CLOCK_Y);
  } else {
    getLocalTime(&timeinfo);
    char buf[12];
    sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    spr.setTextColor(0xFFFF, bg);
    spr.setTextFont(6);
    spr.drawString(buf, SW / 2, CLOCK_Y);
  }
  spr.setTextDatum(TL_DATUM);
}

void drawDate() {
  if (!timeSynced) return;
  getLocalTime(&timeinfo);
  const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char buf[36];
  sprintf(buf, "%04d-%02d-%02d   %s", 
          1900 + timeinfo.tm_year, timeinfo.tm_mon + 1,
          timeinfo.tm_mday, wd[timeinfo.tm_wday]);
          
  uint16_t bg = getBgColor();
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TXT_GRAY, bg);
  spr.setTextFont(4);
  spr.drawString(buf, SW / 2, DATE_Y);
  spr.setTextDatum(TL_DATUM);
}

void drawSensorData() {
  int y = SENSOR_Y;
  uint16_t cardW = SW / 2 - 12; 
  int lx = 8;
  int rx = SW / 2 + 4;
  
  // ================= 1. TEMP =================
  spr.fillRoundRect(lx, y, cardW, SENSOR_H, 6, CARD_BG);
  spr.drawRoundRect(lx, y, cardW, SENSOR_H, 6, CARD_BORDER);
  
  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(CLR_TEMP, CARD_BG);
  spr.setTextFont(2);
  spr.drawString("TEMP", lx + cardW / 2, y + 4);

  if (!isnan(temperature)) {
    char buf[12];
    sprintf(buf, "%.1f", temperature);
    spr.setTextColor(TXT_WHITE, CARD_BG);
    spr.setTextFont(4); 
    
    int bw = spr.textWidth(buf);
    int numX = lx + cardW / 2 - 8;
    spr.drawString(buf, numX, y + 20);
    
    spr.setTextFont(2);
    spr.drawString("C", numX + bw / 2 + 10, y + 22);
    spr.drawCircle(numX + bw / 2 + 3, y + 24, 2, TXT_WHITE);
  }

  // ================= 2. HUMI =================
  spr.fillRoundRect(rx, y, cardW, SENSOR_H, 6, CARD_BG);
  spr.drawRoundRect(rx, y, cardW, SENSOR_H, 6, CARD_BORDER);
  
  spr.setTextColor(CLR_HUMI, CARD_BG);
  spr.setTextFont(2);
  spr.drawString("HUMI", rx + cardW / 2, y + 4);

  if (!isnan(humidity)) {
    char buf[12];
    sprintf(buf, "%.0f", humidity);
    spr.setTextColor(TXT_WHITE, CARD_BG);
    spr.setTextFont(4);
    
    int bw = spr.textWidth(buf);
    int numX = rx + cardW / 2 - 6;
    spr.drawString(buf, numX, y + 20);
    
    spr.setTextFont(2);
    spr.setTextColor(TXT_GRAY, CARD_BG);
    spr.drawString("%", numX + bw / 2 + 10, y + 22);
  }

  // ================= 3. Feels Like =================
  if (weather.valid) {
    spr.setTextColor(TXT_DIM, getBgColor());
    spr.setTextFont(2);
    String fl = "Feels like " + String(weather.feelsLike) + " `C";
    spr.drawString(fl, SW / 2, y + SENSOR_H + 2);
  }
  spr.setTextDatum(TL_DATUM);
}

void drawForecast() {
  if (!weather.valid) return;
  int y = FC_Y;
  int colW = SW / 3; 
  const char* lbl[] = {"Today", "Tmrw", "Day 3"};
  
  for (int i = 0; i < 3; i++) {
    int x = i * colW;
    spr.fillRoundRect(x + 4, y, colW - 8, FC_H, 6, CARD_BG);
    spr.drawRoundRect(x + 4, y, colW - 8, FC_H, 6, CARD_BORDER);

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TXT_GRAY, CARD_BG);
    spr.setTextFont(2);
    spr.drawString(lbl[i], x + colW / 2, y + 4);

    drawWeatherIcon(x + colW / 2, y + 26, weather.forecast[i].codeDay, 10);

    spr.setTextColor(TXT_WHITE, CARD_BG);
    char tb[16];
    sprintf(tb, "%d`/%d`", weather.forecast[i].high, weather.forecast[i].low);
    spr.drawString(tb, x + colW / 2, y + 44);

    spr.setTextColor(TXT_DIM, CARD_BG);
    spr.setTextFont(1);
    
    String textD = weather.forecast[i].textDay;
    if (textD.length() > 12) textD = textD.substring(0, 12);
    spr.drawString(textD, x + colW / 2, y + 56);

    String d = weather.forecast[i].date;
    if (d.length() >= 10) d = d.substring(5);
    spr.drawString(d, x + colW / 2, y + 66);
  }
  spr.setTextDatum(TL_DATUM);
}

void drawAll() {
  if (screenDimmed) return;

  // 【核心修复】强制使用 spr.fillSprite 清洗内存缓冲区！杜绝残影！
  spr.fillSprite(getBgColor());
  
  drawParticles();
  drawStatusBar();
  drawClock();
  drawDate();
  drawSensorData();
  drawForecast();
  
  if (getAnimType() == 5 && flashState) {
    spr.pushSprite(0, 0);
    tft.fillRect(0, 0, SW, STATUS_H, 0xFFFF);
    delay(30);
    flashState = false;
  } else {
    spr.pushSprite(0, 0);
  }
  
  fullRedraw = false;
}

// ======================== SETUP ========================
void setup() {
  Serial.begin(115200);
  
  Wire.begin(SHT20_SDA, SHT20_SCL);
  if (sht20.begin()) { hasSHT20 = true; }

  tft.begin();
  tft.setRotation(1); 

  setupBacklight();
  setBacklight(BL_FULL);

  spr.setColorDepth(8); 
  spr.createSprite(SW, SH);

  // 【同步修改】开机界面也必须使用 spr.pushSprite 将缓冲推到屏幕上
  spr.fillSprite(0x0000);
  spr.setTextColor(0xFFFF, 0x0000);
  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(4);
  spr.drawString("WeatherStation", SW / 2, SH / 2 - 20);
  spr.setTextFont(2);
  spr.drawString("Connecting WiFi...", SW / 2, SH / 2 + 10);
  spr.pushSprite(0, 0);
  
  connectWiFi();
  
  if (wifiConnected) {
    spr.fillSprite(0x0000);
    spr.drawString("NTP syncing...", SW / 2, SH / 2);
    spr.pushSprite(0, 0);
    syncNtpTime();
    
    spr.fillSprite(0x0000);
    spr.drawString("Fetching weather...", SW / 2, SH / 2);
    spr.pushSprite(0, 0);
    bool wOk = fetchWeatherNow();
    fetchWeatherDaily();
  }
  spr.setTextDatum(TL_DATUM);

  initAnimation();
  pinMode(BOOT_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_BTN), onBootPress, FALLING);

  lastActivity = lastSensorRead = lastWeatherFetch = lastNtpSync = millis();
  fullRedraw = true;
  drawAll();
}

// ======================== LOOP ========================
void loop() {
  unsigned long now = millis();

  if (hasSHT20 && now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;
    sht20.read();
    float t = sht20.getTemperature();
    float h = sht20.getHumidity();
    if (!isnan(t) && !isnan(h)) { temperature = t; humidity = h; }
  }

  if (!wifiConnected && now - lastWiFiAttempt > 30000) {
    lastWiFiAttempt = now;
    connectWiFi();
  }

  if (wifiConnected && (!timeSynced || now - lastNtpSync > NTP_INTERVAL_MS)) {
    if (syncNtpTime()) { lastNtpSync = now; }
  }

  if (wifiConnected && now - lastWeatherFetch > WEATHER_INTERVAL_MS) {
    lastWeatherFetch = now;
    bool ok = fetchWeatherNow();
    fetchWeatherDaily();
    if (ok) { initAnimation(); }
  }

  if (now - lastAnimFrame > 66) {
    lastAnimFrame = now;
    updateParticles();
  }

  if (!screenDimmed && now - lastFullDraw > 66) { 
    lastFullDraw = now;
    lastActivity = now;
    drawAll();
  }

  checkDimming();
  delay(5);
}