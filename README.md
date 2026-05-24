# WeatherStation

ESP32 桌面气象站 — 温湿度实时显示 + NTP 网络授时 + 心知天气 3 日预报 + 天气动画

## 硬件清单

| 模块 | 型号 |
|------|------|
| 主控 | ESP32 Dev Module |
| 温湿度 | SHT20 (I2C) |
| 屏幕 | 2.8" TFT LCD ILI9341 驱动 SPI 触摸串口屏 |

## 最终稳定接线

### ILI9341 屏幕 (VSPI 原生引脚)

| 屏幕引脚 | ESP32 GPIO | 信号 | 备注 |
|----------|-----------|------|------|
| VCC | 3.3V | 电源 | |
| GND | GND | 地 | |
| CLK | **D18** | SPI 时钟 | VSPI 原生 SCK |
| MOSI | **D23** | SPI 数据(主出) | VSPI 原生 MOSI |
| MISO | **D19** | SPI 数据(主入) | VSPI 原生 MISO |
| CS1 | D18 → 改 **D14** | TFT 片选 | 避开 Strapping |
| DC | D16 | 数据/命令 | |
| RST | D4 | 复位 | |
| BLK | D17 | 背光 PWM | |
| CS2 | D19 | 触摸片选(未用) | |
| PEN | D21 | 触摸中断(未用) | |

### SHT20 传感器

| SHT20 | ESP32 GPIO |
|-------|-----------|
| VCC | 3.3V |
| GND | GND |
| SCL | D22 |
| SDA | **D33** |

### BOOT 按键

板载 GPIO0（低电平触发），用于从暗屏状态唤醒。

## 功能

- **实时温湿度**：SHT20 每 2 秒采集
- **北京时间**：NTP (`ntp.aliyun.com`)，每小时同步
- **3 日天气预报**：心知天气 API，每 10 分钟刷新
- **天气动画**：
  - ☀️ 晴 — 旋转太阳光线
  - ⛅ 多云 — 白云飘移
  - ☁️ 阴 — 深色云层慢飘
  - 🌧️ 雨 — 雨滴下落
  - ❄️ 雪 — 雪花飘落
  - ⚡ 雷阵雨 — 雨滴 + 闪电闪白
  - 🌫️ 雾/霾 — 像素噪声
  - 💨 大风 — 风线扫过
- **暗屏模式**：30 秒无操作 PWM 降低背光，按 BOOT 键唤醒

## 软件架构

```
WeatherStation/
├── platformio.ini           # 编译配置 + 库依赖 + 驱动宏
├── include/
│   └── config.h             # WiFi/API 密钥/引脚定义
├── src/
│   └── main.cpp             # 全部固件
├── .gitignore
└── README.md
```

### 关键库依赖

```ini
lib_deps =
    robtillaart/SHT2x@^0.5.5      # SHT20 驱动
    bodmer/TFT_eSPI@^2.5.43       # ILI9341 驱动
    bblanchon/ArduinoJson@^7.2.1  # JSON 解析
```

### 渲染管线

```
loop() @ 8fps
  └─→ drawAll()
        ├── TFT_eSprite 创建离屏帧缓冲 (320x240, 150KB 堆)
        ├── fillScreen() 底色
        ├── drawParticles() 天气粒子动画
        ├── drawStatusBar() / drawClock() / drawDate()
        ├── drawSensorData() / drawForecast()
        └── pushSprite() 一次推送 → 0 闪烁
```

## 编译与烧录

### 前提

- [PlatformIO IDE](https://platformio.org/) (VS Code 扩展)
- ESP32 USB 驱动 (CH340/CP210x)

### 步骤

1. 修改 `include/config.h` 中的 WiFi 和 API 密钥：
   ```cpp
   #define WIFI_SSID     "your_ssid"
   #define WIFI_PASS     "your_password"
   #define SENIVERSE_KEY "your_api_key"      // 心知天气私钥
   #define WEATHER_LOC   "wuxi"              // 城市拼音或中文
   ```

2. 用 VS Code 打开项目目录，点击底部 PlatformIO: Upload

或命令行：
```bash
pio run --target upload --upload-port COM4
```

3. 打开串口监视器 (115200 baud) 查看启动日志

## API 配置

本项目使用 [心知天气 API v3](https://docs.seniverse.com/)：

| 接口 | 用途 | 免费限制 |
|------|------|---------|
| `/v3/weather/now.json` | 实时天气 | 天气现象文字 + 代码 + 气温 |
| `/v3/weather/daily.json` | 逐日预报 | 3 天 |

### 获取 API 密钥

1. 注册 [心知天气](https://www.seniverse.com/)
2. 控制台 → 产品管理 → 添加产品（免费版）
3. 复制私钥填入 `config.h` 的 `SENIVERSE_KEY`

### location 参数

支持：城市中文名 `"无锡"`、拼音 `"wuxi"`、城市 ID、经纬度 `"31.56:120.30"`、IP 自动定位 `"ip"`

---

# 踩坑记录

以下是本项目开发过程中遇到的典型问题，记录以供参考。

## 坑 1：PlatformIO build_flags 对库源码不生效

**现象**：使用 `-DUSER_SETUP_HEADER="TFT_Setup.h"` 编译自定义 User_Setup，项目源码 (main.cpp) 编译通过但 TFT_eSPI 库自己的 `.cpp` 文件找不到自定义头文件，`TFT_WIDTH` / `TFT_HEIGHT` 等 ILI9341 驱动宏全部未定义。

**原因**：PlatformIO 的 `build_flags` 通过 `-D` 传递的宏对项目源码生效，但库文件（`.pio/libdeps/...`）编译时的头文件搜索路径不同，`#include USER_SETUP_HEADER` 展开后在库目录下找不到项目 `include/` 中的文件。

**解决**：所有 TFT_eSPI 驱动宏直接写在 `build_flags` 里，不依赖外部头文件：

```ini
build_flags =
    -DUSER_SETUP_LOADED=1   ← 阻止库自带的 User_Setup.h 被引入
    -DILI9341_DRIVER        ← 驱动型号
    -DTFT_CS=18             ← 引脚定义
    -DTFT_DC=16
    -DTFT_RST=4
    -DTFT_MISO=19
    -DTFT_MOSI=23
    -DTFT_SCLK=18
    -DSPI_FREQUENCY=20000000
    -DSPI_READ_FREQUENCY=10000000
    -DLOAD_GLCD
    -DLOAD_FONT2
    ; ... 其他 LOAD_FONTx
```

**教训**：PlatformIO + TFT_eSPI 组合下，**不要用 `USER_SETUP_HEADER` 宏**，直接在 `build_flags` 中定义所有驱动宏。`USER_SETUP_LOADED=1` 是关键——它阻止 `User_Setup_Select.h` 引入库自带的 `User_Setup.h`，否则该文件会用 ESP8266 的引脚值 (D6/D7/D5...) 覆盖你的定义。

## 坑 2：ESP32 引脚不能随便用

### 2.1 GPIO2 (D2) — Strapping 引脚 + 板载 LED

**致命问题**：
- D2 是 **Strapping 引脚**：上电时决定启动模式。烧录时必须为低电平（或悬空）。如果屏幕模块有上拉电阻把 D2 拉高，**无法烧录代码**（卡 `Connecting...` 超时），甚至无法正常开机。
- D2 物理连接板载 **蓝色 LED + 限流电阻**。用作 SPI MOSI 时：
  - LED 随画面闪烁
  - LED + 电阻构成额外负载 → **破坏高频信号完整性** → 白屏/花屏/数据丢失

### 2.2 GPIO15 (D15) — Strapping 引脚 + 永久下拉

- D15 是 MTDO 启动日志脚，**内部永久下拉电阻**持续消耗信号 → 高速时钟边沿衰减
- 非 VSPI 原生脚，信号需绕 GPIO 交换矩阵 → 最高频率受限

### 2.3 GPIO5, GPIO12 同理

- GPIO5：启动时控制 Flash 电压
- GPIO12：启动时控制 Flash 电压（MTDI）

### 结论

**ESP32 屏幕 SPI 必须用 VSPI 原生引脚**：

| 信号 | 原生引脚 | 备选 |
|------|---------|------|
| SCK | 18 | - |
| MOSI | 23 | - |
| MISO | 19 | - |

原生引脚的好处：
- IOMUX 直连，不绕 GPIO 矩阵
- 可达 80MHz，复杂动画不撕裂
- 避开所有 Strapping 引脚

## 坑 3：SPI 屏闪烁

**现象**：画面不断闪烁，每次刷新能看到一瞬间的纯色背景。

**根因**：`drawAll()` 第一句 `fillScreen(bg)` 把整个屏幕瞬间清成背景色，然后逐个函数绘制 UI。SPI 屏每个像素都是串行传输，用户能看到"先清屏 → 后画 UI"的过程。

**错误思路**（本次实际走的弯路）：
1. 以为是引脚问题 → 折腾 SPI 调试
2. 以为是频率太高 → 降频 40MHz → 20MHz
3. 以为是 readPixel 返回值不对 → 加诊断
4. 最后才想到 Sprite 帧缓冲

**正确思路**：
1. 判断渲染管线是否有帧缓冲
2. 没有 → 上 Sprite
3. 有了 → 才排查 SPI 硬件

**解决**：`TFT_eSprite` 离屏帧缓冲，整帧在内存中合成后一次推送：

```cpp
TFT_eSprite spr(&tft);               // 创建离屏画布
spr.createSprite(320, 240);          // 分配 150KB 帧缓冲
// ... 所有绘制到 spr 上 ...
spr.pushSprite(0, 0);               // 一次性推送到物理屏幕
```

ESP32 328KB 堆内存，150KB 帧缓冲完全够用（剩余 ~128KB）。

## 坑 4：canvas 指针技巧

**问题**：要支持 Sprite / 直绘两种模式，但所有绘制函数（`drawStatusBar`、`drawClock` 等 100+ 处）都写死了 `tft.xxx()`。

**解决**：利用 `TFT_eSprite` 继承自 `TFT_eSPI`，统一用一个基类指针：

```cpp
TFT_eSPI tft;
TFT_eSprite spr(&tft);
TFT_eSPI* canvas = &tft;   // 默认直绘

// 切到离屏渲染
canvas = &spr;
// ... 所有绘制操作都走 canvas->xxx() ...
spr.pushSprite(0, 0);
canvas = &tft;              // 切回直绘
```

这样 `canvas->fillScreen()`、`canvas->drawString()` 等**一次替换，双模式通用**。

## 排查清单 (下次用)

嵌入式 ESP32 + 屏幕项目开发顺序：

1. **审引脚表**
   - [ ] 避开 GPIO 0, 2, 5, 12, 15（Strapping 引脚）
   - [ ] SPI 用 VSPI 原生 (18/19/23)
   - [ ] 其它外设用剩余普通 GPIO

2. **定渲染架构**
   - [ ] 是否需要动画？→ 是 → TFT_eSprite 帧缓冲
   - [ ] 静态显示？→ 直绘即可

3. **定编译方案**
   - [ ] PlatformIO + TFT_eSPI → build_flags 直定义，不用 `USER_SETUP_HEADER`
   - [ ] 必须加 `-DUSER_SETUP_LOADED=1`

4. **写功能代码**
   - [ ] SHT20 → I2C
   - [ ] WiFi → NTP → 天气 API
   - [ ] UI 布局 → 动画 → 暗屏

## License

MIT
