#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "hasTouch.h"

// ========== 检测 XPT2046 的函数 ==========
bool hasTouch() {
  // 1. 设置 IRQ 引脚为输入上拉，检测电平
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  delayMicroseconds(100);
  
  // 如果 IRQ 为低，说明可能正在被触摸（有芯片才可能）
  // 或者引脚被意外拉低，但这种情况大概率是有芯片
  if (digitalRead(TOUCH_IRQ) == LOW) {
    return true;  // 有 XPT2046
  }
  
  // 2. 通过 SPI 尝试读取坐标，看是否有合理响应
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  SPI.begin();                     // 确保 SPI 已启动
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  
  // 读取 X 坐标（命令 0x90）
  digitalWrite(TOUCH_CS, LOW);
  uint16_t x1 = SPI.transfer16(0x9000);  // 发送16位，高8位命令，低8位任意
  digitalWrite(TOUCH_CS, HIGH);
  delayMicroseconds(50);
  
  // 读取 Y 坐标（命令 0xD0）
  digitalWrite(TOUCH_CS, LOW);
  uint16_t y1 = SPI.transfer16(0xD000);
  digitalWrite(TOUCH_CS, HIGH);
  delayMicroseconds(50);

  // 再读取一次 X，看数据是否一成不变
  digitalWrite(TOUCH_CS, LOW);
  uint16_t x2 = SPI.transfer16(0x9000);
  digitalWrite(TOUCH_CS, HIGH);
  
  SPI.endTransaction();
  
  // 3. 判断：如果三次读到的值全部相同，且为 0x0000 或 0xFFFF
  //    说明 MISO 线上没有真实的芯片回应（极可能没有 XPT2046）
  if (x1 == x2 && (x1 == 0x0000 || x1 == 0xFFFF) &&
      y1 == x1) {
    return false;   // 未检测到 XPT2046
  }
  
  // 否则认为有芯片（即使未触摸，ADC 也可能返回非极值）
  return true;
}
