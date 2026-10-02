# 1.69寸 IPS 屏 + ESP32-S3 网络显示终端

1.69 寸 ST7789V IPS 屏（240×280）配 ESP32-S3，通过手机浏览器控制显示中文文字和图片。

## 硬件

| 部件 | 型号 |
|---|---|
| 主控 | ESP32-S3（16MB Flash + 8MB PSRAM） |
| 屏幕 | 1.69寸 ST7789V，240×280，4线SPI，IPS |
| 屏型号 | XSJ170SA1501 V01 / YH-169CG7048N0 |
| 触摸 | CTP（FT6146，IIC）—— **当前未使用** |

## 接线（已验证可用）

| 屏上丝印 | ESP32-S3 | 说明 |
|---|---|---|
| 3V3 | 3.3V | **绝不能接 5V** |
| GND | GND | |
| SCK | GPIO 12 | SPI 时钟 |
| SDI | GPIO 11 | SPI 数据 (MOSI) |
| CS | GPIO 10 | 片选 |
| DC | GPIO 9 | 数据/命令 |
| BLK | GPIO 4 | 背光（HIGH = 亮） |
| **RST** | **不接！** | 见下方说明 |

### 触摸（未接线，备用）
| 屏上丝印 | 建议 GPIO |
|---|---|
| CTP_SCL | 5 |
| CTP_SDA | 6 |
| CTP_RST | 7 |
| CTP_INT | 15 |

## 三个必须遵守的坑（血泪教训）

### 1. RST 必须不接任何 GPIO

厂商 Demo（`firmware/graph_test/graph_test.ino`）里 RST 参数传的是 `-1`。
**如果把 RST 接到某个 GPIO，ST7789 会被持续复位。**

症状：**背光正常亮、能 PWM 调光、串口日志一切正常、但屏幕完全不显示图像**。
这个坑排查了很久，一度误判为排线接触不良。

### 2. row offset = 20 必须设

240×280 的屏在 240×320 的 GRAM 上。不设偏移，画面会错位或被裁掉。

### 3. 必须用 Arduino_GFX，不要用 TFT_eSPI

TFT_eSPI 对 240×280 支持不完整（缺 row offset 参数）。厂商 Demo 用的是 Arduino_GFX。

```cpp
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    9  /* DC */, 10 /* CS */, 12 /* SCK */, 11 /* MOSI */);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, -1 /* RST 不接 */, 0 /* rotation */, true /* IPS */,
    240, 280, 0 /* col offset */, 20 /* row offset */, 0, 0);
```

## PlatformIO 环境的坑

### core dir 在 D 盘

用户机器的 PlatformIO core 目录是 `D:\.platformio`，而 `pio.exe` 默认找
`C:\Users\<user>\.platformio`。**每次运行前必须设环境变量**：

```powershell
$env:PLATFORMIO_CORE_DIR = 'D:\.platformio'
```

否则会去 C 盘找 framework 找不到，然后尝试下载残缺的包。

### 平台版本选 espressif32@5.4.0

| 平台版本 | 需要的 framework | 本机状态 |
|---|---|---|
| **5.4.0** | ~3.20005.0 | ✅ 已缓存完整，**零下载** |
| 6.5.0 | ~3.20014.0 | ❌ 需下载（国内易卡） |
| 6.9.0 | ~3.20017.0 | ❌ 本机那份残缺（缺 esp32s3 目录） |

### 串口走 UART，不要开 USB CDC

板子有两个 USB 口：CH343 的 UART 口（COM3）和原生 USB 口。
`build_flags` **不要**加 `-DARDUINO_USB_CDC_ON_BOOT=1`，
否则 `Serial` 重定向到原生 USB，COM3 上看不到任何日志。

### 烧录

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$env:PLATFORMIO_CORE_DIR = 'D:\.platformio'
cd D:\学习\TFT169_ESP32S3
& $pio run -e esp32s3 -t upload --upload-port COM3
```

约 8 秒完成，**不需要按 BOOT 键**。

## Arduino_GFX 版本约束

**必须用 1.4.9**。1.5+ 需要 arduino-esp32 core 3.x，
会报 `fatal error: esp32-hal-periman.h: No such file or directory`。

## 中文字库

`include/cn_font.h` 由 `tools/gen_font.py` 生成，**不要手动编辑**。

- 16×16 点阵，GB2312 一级汉字 3755 个
- 覆盖 U+4E00 ~ U+9F9F
- 约 750 KB，编译进 Flash
- 二分查找，运行时无额外内存占用

重新生成：

```powershell
& "C:\Users\MOONFISH\AppData\Local\Programs\Python\Python311\python.exe" tools\gen_font.py
```

依赖：`Pillow`、`fontTools`，字体用 `C:\Windows\Fonts\simhei.ttf`。

**注意**：`cn_font.h` 已加入 `.gitignore`（750KB 生成物），克隆后需自己跑一遍
`gen_font.py` 生成。

## 功能

手机浏览器打开 ESP32 的 IP，可控制：

| 功能 | 说明 |
|---|---|
| 显示文字 | 中文（3755 常用字）+ 英文 |
| 字号 | 1 / 2 / 3 倍 |
| 颜色 | 8 种 |
| 清屏 / 换行 | |
| **图片上传** | 网页端自动缩放到 240×280 以内，转 JPEG（质量 75%），POST 上传 |
| 图片解码 | TJpg_Decoder，缓冲区在 PSRAM（400 KB） |

### 图片上传的技术要点

ESP32 core 2.x 的 `WebServer` **没有** `server.raw()`（那是 ESPAsyncWebServer 的 API）。
正确做法是用 `HTTPUpload`：

- 网页用 `FormData` 发送（multipart/form-data）
- 注册 `server.on("/image", HTTP_POST, handleDone, handleUpload)`
- 上传回调里按 `UPLOAD_FILE_START / UPLOAD_FILE_WRITE / UPLOAD_FILE_END` 分块收
- 每块 `HTTP_UPLOAD_BUFLEN` = 1436 字节

## 编译流程

```powershell
$py = "C:\Users\MOONFISH\AppData\Local\Programs\Python\Python311\python.exe"

# 1. 生成中文字库（首次或改字号时）
& $py tools\gen_font.py

# 2. 编译 + 烧录
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
$env:PLATFORMIO_CORE_DIR = 'D:\.platformio'
cd D:\学习\TFT169_ESP32S3
& $pio run -e esp32s3 -t upload --upload-port COM3

# 3. 看串口
& $pio device monitor --port COM3 --baud 115200
```

## 厂商 Demo 参考

原始资料在 `D:\学习\1.69寸TFT-XSJ170SA1501 V01\`，
解压后的 Demo 在 `D:\学习\_demo_tmp\esp32c3\`。

三个 Demo 的屏幕配置完全一致（引脚是 ESP32-C3 的，要换成自己的）：

```
firmware/graph_test/graph_test.ino        纯显示测试
firmware/clock/clock.ino                  时钟界面
firmware/ESPWiFiAnalyzer/ESPWiFiAnalyzer.ino  WiFi 分析仪
firmware/lvgl_music_demo/                 LVGL + 触摸 + 传感器
```

**这是权威参考**，遇到显示问题优先看它。

## 待办 / 想法

- [ ] 触摸（FT6146，I2C）—— 需要先扫 I2C 确认地址和芯片 ID
- [ ] 视频播放（MJPEG，240×280 @ 20fps）
- [ ] 从服务器拉数据（股票行情 / 天气）
- [ ] 中文字库扩展（GB2312 二级，或 GBK）
- [ ] 更高分辨率字库（24×24 / 32×32），解决放大锯齿

## 许可

个人学习项目。
```

{"tool_use": [{"name": "Write", "input": {"file_path": "D:\\学习\\TFT169_ESP32S3\\.gitignore"}}]}
```
# PlatformIO build output
.pio/
.vscode/

# Generated font (750KB) - regenerate with tools/gen_font.py
include/cn_font.h

# Helper scripts (dev-only, not part of the firmware)
write_main.py
patch_image.py

# OS / editor
Thumbs.db
desktop.ini
*.swp
*~
