#ifndef CALIBRATEANDENABLETOUCH_H
#define CALIBRATEANDENABLETOUCH_H

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>

/* =====================================================================
 *  Touch calibration module
 *
 *  This file holds declarations only. It defines no objects and no setup()/loop().
 *  The display object lcdScreen and the touch object touchScreen are created by the main
 *  sketch ESP32_28LCD_TOUCH_SDCard.ino and are only referenced here with extern.
 *  All pin and resolution parameters come from config.h; nothing is re-#defined here.
 * ===================================================================== */

extern Adafruit_ST7789     lcdScreen;     // defined in the .ino
extern XPT2046_Touchscreen touchScreen;   // defined in the .ino

// Load the saved coefficients from NVS; returns true on success.
// If the saved rotation differs from the current lcdScreen.getRotation(), they count as stale and false is returned.
bool loadCalibration();

// Run the calibration (blocking, may be called again at any time). It retries up to 3 times internally;
// on success it writes to NVS and prints the coefficients over serial, otherwise it stays uncalibrated.
void runCalibration();

// Whether usable coefficients are available. While uncalibrated, getTouch() always returns false.
bool isCalibrated();

// Get the calibrated screen coordinates; returns false when untouched or uncalibrated
bool getTouch(int16_t &x, int16_t &y);

// Show the verification screen (border + corner squares + crosshair)
void drawVerifyScreen();

// Erase the coefficients saved in NVS (the next power-up will recalibrate)
void clearCalibration();

#endif
