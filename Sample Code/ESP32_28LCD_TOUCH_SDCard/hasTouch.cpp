#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "hasTouch.h"

// ========== XPT2046 detection ==========
bool hasTouch() {
  // 1. Set the IRQ pin as an input with pull-up and read its level
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
  delayMicroseconds(100);
  
  // A LOW IRQ means the panel may be under a touch right now, which only happens with a chip fitted,
  // or the pin was pulled low by something else, but in practice that still points to a chip being present
  if (digitalRead(TOUCH_IRQ) == LOW) {
    return true;  // XPT2046 present
  }
  
  // 2. Try reading coordinates over SPI and see whether the response is sensible
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  SPI.begin();                     // make sure SPI is running
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  
  // Read the X coordinate (command 0x90)
  digitalWrite(TOUCH_CS, LOW);
  uint16_t x1 = SPI.transfer16(0x9000);  // send 16 bits: command in the high 8, the low 8 are don't care
  digitalWrite(TOUCH_CS, HIGH);
  delayMicroseconds(50);
  
  // Read the Y coordinate (command 0xD0)
  digitalWrite(TOUCH_CS, LOW);
  uint16_t y1 = SPI.transfer16(0xD000);
  digitalWrite(TOUCH_CS, HIGH);
  delayMicroseconds(50);

  // Read X once more and see whether the value never changes
  digitalWrite(TOUCH_CS, LOW);
  uint16_t x2 = SPI.transfer16(0x9000);
  digitalWrite(TOUCH_CS, HIGH);
  
  SPI.endTransaction();
  
  // 3. Decision: if all three reads return the same value and it is 0x0000 or 0xFFFF,
  //    nothing on the MISO line is answering, so there is almost certainly no XPT2046
  if (x1 == x2 && (x1 == 0x0000 || x1 == 0xFFFF) &&
      y1 == x1) {
    return false;   // no XPT2046 detected
  }
  
  // Otherwise assume a chip is fitted (even untouched, the ADC can return non-extreme values)
  return true;
}
