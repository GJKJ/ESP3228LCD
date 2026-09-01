#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "hasTouch.h"
#include "calibrateAndEnableTouch.h"

// ========== 对象初始化（全局唯一，calibrateAndEnableTouch.cpp 用 extern 引用）==========
Adafruit_ST7789 lcdScreen(&SPI, LCD_CS, LCD_DC, LCD_RST);

// XPT2046_Touchscreen 构造函数不需要 SPI 参数
XPT2046_Touchscreen touchScreen(TOUCH_CS, TOUCH_IRQ);

/*  -------------------- Panel colour setting ------------------------
  Two panel types are used on this board. If the background of the
  demo screen comes up WHITE instead of BLACK, change "true" to "false".
  This is a setting, not a fault      */
bool isDisplayInverted = true;  

bool isHasTouch      = false;   // 是否检测到 XPT2046
bool needCalibration = false;   // loop() 里是否要跑一次校准

// ========== 校准策略 ==========
// 1 = 每次开机都强制重新校准，忽略 NVS 里保存的系数
// 0 = 优先读取 NVS 中已保存的系数，读不到才校准
#define FORCE_CAL_ON_BOOT 1

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT); 
  
  digitalWrite(2, HIGH);
  delay(1000);                     
  digitalWrite(2, LOW); 
  delay(100);                     
  
  digitalWrite(2, HIGH);
  delay(100);
  digitalWrite(2, LOW);
  delay(100);
  digitalWrite(2, HIGH);
  delay(100);
  digitalWrite(2, LOW);
  delay(100);
  digitalWrite(2, HIGH);
  delay(100);
  digitalWrite(2, LOW);
  delay(100);
  
  Serial.println();
  Serial.println("===== ESP32 2.8\" ST7789 + XPT2046 =====");

  // 背光常亮。BLK 悬空时部分模组默认不亮，先拉高再说
  pinMode(LCD_BLK, OUTPUT);
  digitalWrite(LCD_BLK, HIGH);

  // 屏幕、触摸、SD 共用一条 VSPI。
  // 检测触摸之前必须先把 SD_CS 和 TOUCH_CS 拉高，
  // 否则 SD 卡会挂在总线上干扰 MISO，hasTouch() 会误判成“有触摸芯片”。
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  // 初始化 SPI（VSPI 默认引脚就是 23,19,18，不需要显式传入）
  SPI.begin();

  // 初始化屏幕
  lcdScreen.init(LCD_WIDTH, LCD_HEIGHT);
  lcdScreen.setRotation(2);
  if(isDisplayInverted){
    lcdScreen.invertDisplay(true);
  }else{
    lcdScreen.invertDisplay(false);
  }
  
  lcdScreen.fillScreen(ST77XX_BLACK);

  isHasTouch = hasTouch();

  lcdScreen.setTextColor(ST77XX_GREEN);
  lcdScreen.setTextSize(3);
  lcdScreen.setCursor(10, 65);
  lcdScreen.println("Hi,ideaspark");

  // 显示启动信息
  lcdScreen.setTextColor(ST77XX_WHITE);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 120);
  lcdScreen.println("(Screen)....OK!");

  lcdScreen.setCursor(10, 140);
  // 初始化 SD 卡
  if (!SD.begin(SD_CS)) {
    Serial.println("SD卡初始化失败！");
    lcdScreen.setTextColor(ST77XX_RED);
    lcdScreen.println("(SD Card)...Failed!");
  } else {
    Serial.println("SD卡初始化成功！");
    lcdScreen.setTextColor(ST77XX_WHITE);
    lcdScreen.println("(SD Card)...OK!");
  }

  if (isHasTouch) {
    lcdScreen.setCursor(10, 160);
    // 初始化触摸屏 —— begin() 传入 SPIClass&（引用）
    if (!touchScreen.begin(SPI)) {
      Serial.println("触摸屏初始化失败！");
      lcdScreen.setTextColor(ST77XX_RED);
      lcdScreen.println("(Touch).....Failed!");
      isHasTouch = false;
    } else {
      Serial.println("触摸屏初始化成功！");
      lcdScreen.setTextColor(ST77XX_WHITE);
      lcdScreen.println("(Touch).....OK!");
      // 固定 0，屏幕方向交给仿射矩阵处理
      touchScreen.setRotation(0);
    }
  }
  delay(1500);
  lcdScreen.drawRGBBitmap(0, 0, imageData, LCD_WIDTH, LCD_HEIGHT);
  delay(1500);

#if FORCE_CAL_ON_BOOT
  // 强制校准模式下，开机图会被校准画面立刻覆盖，画了看不见，所以跳过。
  // 校准结束进验证画面，串口发 'p' 可以随时把开机图调出来。
#else
  lcdScreen.drawRGBBitmap(0, 0, imageData, LCD_WIDTH, LCD_HEIGHT);  // 显示开机图
#endif

  // ---- 决定这次开机要不要校准 ----
  if (isHasTouch) {
#if FORCE_CAL_ON_BOOT
    needCalibration = true;
    Serial.println("FORCE_CAL_ON_BOOT = 1，本次开机强制重新校准");
#else
    needCalibration = !loadCalibration();
    if (!needCalibration) {
      Serial.println("直接使用已保存的系数，串口发 'c' 可重新校准");
    }
#endif
  }
}

void loop() {
  // ---- 串口命令 ----
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'c' || cmd == 'C') {
      needCalibration = true;                                        // 重新校准
    } else if (cmd == 'r' || cmd == 'R') {
      clearCalibration();                                            // 清除 NVS 系数
      needCalibration = true;
    } else if (cmd == 'p' || cmd == 'P') {
      lcdScreen.drawRGBBitmap(0, 0, imageData, LCD_WIDTH, LCD_HEIGHT); // 重画开机图
    }
  }

  // 没有触摸芯片就什么都不做
  if (!isHasTouch) {
    delay(100);
    return;
  }

  // ---- 校准在 loop() 里执行 ----
  if (needCalibration) {
    runCalibration();          // 阻塞式，内部自带超时和重试
    needCalibration = false;   // 无论成功失败都清标志，避免死循环
    if (isCalibrated()) {
      drawVerifyScreen();      // 校准成功，进验证画面
    }
    return;
  }

  // ---- 正常触摸：画点 ----
  int16_t x, y;
  if (getTouch(x, y)) {
    lcdScreen.fillCircle(x, y, 2, ST77XX_YELLOW);
  }
  delay(8);
}
