#pragma once

// ======================== WiFi ========================
#define WIFI_SSID       "mfer1001"
#define WIFI_PASS       "z1234567"

// ======================== 心知天气 API ========================
#define SENIVERSE_KEY   "SMTnUf1cjFlWCtSyZ"
#define WEATHER_LOC     "wuxi"
#define WEATHER_INTERVAL_MS  600000  // 10 min

// ======================== NTP ========================
#define NTP_SERVER1     "ntp.aliyun.com"
#define NTP_SERVER2     "pool.ntp.org"
#define TZ_OFFSET       28800  // UTC+8
#define NTP_INTERVAL_MS 3600000  // 1 h

// ======================== I2C (SHT20) ========================
#define SHT20_SDA       33
#define SHT20_SCL       22
#define SENSOR_INTERVAL_MS  2000

// ======================== TFT (ILI9341 / SPI) ========================
#define TFT_SCLK        15
#define TFT_MOSI        23
#define TFT_MISO        5
#define TFT_CS          18
#define TFT_DC          16
#define TFT_RST         4
#define LCD_BL          17

// ======================== Touch (unused) ========================
#define TOUCH_CS        19
#define TOUCH_IRQ       21

// ======================== BOOT button ========================
#define BOOT_BTN        0

// ======================== Backlight / Dimming ========================
#define BL_PWM_CH       0
#define BL_PWM_FREQ     5000
#define BL_PWM_RES      8
#define BL_FULL         200
#define BL_DIM          8
#define DIM_TIMEOUT_MS  30000
