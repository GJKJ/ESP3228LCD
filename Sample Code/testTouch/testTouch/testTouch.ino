#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

/* ---------- LCD Screen Port---------- */
#define LCD_DC    2  // Data Command control pin on ESP32 D2
#define LCD_RST   4  // Reset pin (could connect to RST pin) on ESP32 D4
#define LCD_CS    15  // Chip select control pin on ESP32 D15
#define LCD_BLK   32  // Black Light Pin on ESP32 D32

/* ---------- Touch Port---------- */
#define TOUCH_CS  14
#define TOUCH_IRQ 27

/* ---------- LCD Screen ,Touch and SD Shared VSPI bus ---------- */
#define SPI_SCLK 18 // SCL Pin on ESP32 D18
#define SPI_MISO 19 // MISO Pin on ESP32 D19
#define SPI_MOSI 23 // SDA Pin on ESP32 D23

#define LCD_WIDTH     240
#define LCD_HEIGHT    320

// 触摸原始值范围。跑一次看串口打印的 raw，按实际数字改这四个。
// 想反向就把 MIN / MAX 的值对调。
#define RAW_X_MIN  300
#define RAW_X_MAX 3800
#define RAW_Y_MIN  300
#define RAW_Y_MAX 3800

Adafruit_ST7789     lcdScreen(&SPI, LCD_CS, LCD_DC, LCD_RST);
XPT2046_Touchscreen touchScreen(TOUCH_CS, TOUCH_IRQ);

void setup() {
  Serial.begin(115200);

  pinMode(13, OUTPUT); digitalWrite(13, HIGH);   // SD 不用，CS 拉高让出 SPI 总线

  SPI.begin();
  lcdScreen.init(240, 320);
  lcdScreen.setRotation(2);
  lcdScreen.fillScreen(ST77XX_BLACK);
  lcdScreen.setTextColor(ST77XX_GREEN);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 10);
  lcdScreen.println("Please Touch me:");

  touchScreen.begin(SPI);
  touchScreen.setRotation(0);
}

void loop() {
  if (touchScreen.touched()) {
    TS_Point p = touchScreen.getPoint();
    int16_t rx = p.x, ry = p.y;                  // 方向对不上就把 p.x / p.y 对调
    int16_t x = constrain(map(rx, RAW_X_MIN, RAW_X_MAX, 0, LCD_WIDTH - 1), 0, LCD_WIDTH - 1);
    int16_t y = constrain(map(ry, RAW_Y_MIN, RAW_Y_MAX, 0, LCD_HEIGHT - 1), 0, LCD_HEIGHT - 1);
    lcdScreen.fillCircle(x, y, 3, ST77XX_YELLOW);
    Serial.printf("raw(%4d,%4d) -> (%3d,%3d)\n", p.x, p.y, x, y);
  }
  delay(10);
}
