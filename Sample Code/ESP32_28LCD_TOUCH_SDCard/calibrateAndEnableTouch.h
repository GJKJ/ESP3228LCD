#ifndef CALIBRATEANDENABLETOUCH_H
#define CALIBRATEANDENABLETOUCH_H

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>

/* =====================================================================
 *  触摸校准模块
 *
 *  本文件只做“声明”，不定义任何对象、不定义 setup()/loop()。
 *  屏幕对象 lcdScreen 和触摸对象 touchScreen 由主程序
 *  ESP32_28LCD_TOUCH_SDCard.ino 创建，这里只用 extern 引用。
 *  所有引脚 / 分辨率参数一律取自 config.h，本模块不再重复 #define。
 * ===================================================================== */

extern Adafruit_ST7789     lcdScreen;     // 定义在 .ino
extern XPT2046_Touchscreen touchScreen;   // 定义在 .ino

// 从 NVS 加载已保存的系数；成功返回 true
// 若保存时的 rotation 与当前 lcdScreen.getRotation() 不一致，视为失效返回 false
bool loadCalibration();

// 执行校准流程（阻塞式，可重复调用）。内部最多重试 3 次，
// 成功后自动写入 NVS 并通过串口打印系数；全部失败则保持未校准状态。
void runCalibration();

// 当前是否已有可用系数。未校准时 getTouch() 恒返回 false。
bool isCalibrated();

// 获取校准后的屏幕坐标，未按下或未校准返回 false
bool getTouch(int16_t &x, int16_t &y);

// 显示验证画面（边框 + 四角方块 + 十字线）
void drawVerifyScreen();

// 清除 NVS 中保存的系数（下次开机会重新校准）
void clearCalibration();

#endif
