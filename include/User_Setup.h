// ============================================================
//  TFT_eSPI 引脚配置 —— 1.69寸 ST7789 240x280 + ESP32-S3
//
//  PlatformIO 的 TFT_eSPI 会自动优先使用 include/User_Setup.h，
//  不会去动 .pio/libdeps 里那份，升级库不怕被覆盖。
//
//  接线：SCK=12 SDI=11 CS=10 DC=9 RST=8 BLK=4
// ============================================================

#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 280

// ---- 接线 ----
#define TFT_MOSI 11   // 屏 SDI
#define TFT_SCLK 12   // 屏 SCK
#define TFT_CS   10   // 屏 CS
#define TFT_DC    9   // 屏 DC
#define TFT_RST   8   // 屏 RST
#define TFT_BL    4   // 屏 BLK（背光）

// ---- 字体 ----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// ---- SPI 速度：花屏/白屏就往下调 ----
#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

// ---- 颜色反了（红显示成蓝）就取消下面注释 ----
// #define TFT_RGB_ORDER TFT_BGR

// ---- 上下错位/顶部彩条，取消下面注释 ----
// #define ST7789_240x280_INIT
