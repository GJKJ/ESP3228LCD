#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "hasTouch.h"
#include "calibrateAndEnableTouch.h"

// ========== Global objects (defined once here; calibrateAndEnableTouch.cpp refers to them with extern) ==========
Adafruit_ST7789 lcdScreen(&SPI, LCD_CS, LCD_DC, LCD_RST);

// The XPT2046_Touchscreen constructor takes no SPI argument
XPT2046_Touchscreen touchScreen(TOUCH_CS, TOUCH_IRQ);

File SD_file;

/*  -------------------- Panel colour setting ------------------------
  Two panel types are used on this board. If the background of the
  demo screen comes up WHITE instead of BLACK, change "true" to "false".
  This is a setting, not a fault      */
bool isDisplayInverted = true;  

bool isHasTouch      = false;   // true once an XPT2046 has been detected
bool needCalibration = false;   // set when loop() should run a calibration

// ========== Calibration policy ==========
// 1 = always recalibrate at power-up, ignoring the coefficients saved in NVS
// 0 = load the coefficients from NVS if they are there, and calibrate only when they are not
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

  // Keep the backlight on. On some modules BLK floats and stays dark, so drive it HIGH first.
  pinMode(LCD_BLK, OUTPUT);
  digitalWrite(LCD_BLK, HIGH);

  // The display, the touch panel and the microSD card share one VSPI bus.
  // SD_CS and TOUCH_CS must be driven HIGH before probing for the touch chip,
  // otherwise the card keeps driving MISO and hasTouch() reports a touch chip that is not there.
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);

  // Start SPI. The VSPI defaults are already 23, 19 and 18, so no pins need to be passed in.
  SPI.begin();

  // Initialise the display
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

  // Show the start-up report
  lcdScreen.setTextColor(ST77XX_WHITE);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 120);
  lcdScreen.println("(Screen)....OK!");

  lcdScreen.setCursor(10, 140);
  // Initialise the microSD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card init failed!");
    lcdScreen.setTextColor(ST77XX_RED);
    lcdScreen.println("(SD Card)...Failed!");
  } else {
    Serial.println("SD card init OK.");
    lcdScreen.setTextColor(ST77XX_WHITE);
    lcdScreen.println("(SD Card)...OK!");
    /* Write file */
    SD_file = SD.open("/Document.txt", FILE_WRITE);
    if (!SD_file) {
      lcdScreen.setTextColor(ST77XX_RED);
      lcdScreen.println("Open File...Fail!");
      return;
    }
    lcdScreen.setCursor(10, 180);
    lcdScreen.println("Write File...OK!");
    SD_file.println("Source Code,Tutorial,Pin Reference Download URL: www.github.com/GJKJ/ESP3228LCD");
    SD_file.close();
  }

  if (isHasTouch) {
    lcdScreen.setCursor(10, 160);
    // Initialise the touch panel. begin() takes a SPIClass reference.
    if (!touchScreen.begin(SPI)) {
      Serial.println("Touch panel init failed!");
      lcdScreen.setTextColor(ST77XX_RED);
      lcdScreen.println("(Touch).....Failed!");
      isHasTouch = false;
    } else {
      Serial.println("Touch panel init OK.");
      lcdScreen.setTextColor(ST77XX_WHITE);
      lcdScreen.println("(Touch).....OK!");
      // Keep this at 0. Screen orientation is handled by the affine calibration matrix.
      touchScreen.setRotation(0);
    }
  }

  lcdScreen.setTextColor(ST77XX_CYAN);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 220);
  lcdScreen.println("Tutorial/Code URL:");

  lcdScreen.setTextColor(ST77XX_YELLOW);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 240);
  lcdScreen.println("www.github.com/GJKJ");
  lcdScreen.setCursor(10, 260);
  lcdScreen.println("/ESP3228LCD");

  delay(1500);
  lcdScreen.drawRGBBitmap(0, 0, imageData, LCD_WIDTH, LCD_HEIGHT);
  delay(1500);

#if FORCE_CAL_ON_BOOT
  // In forced calibration mode the splash image would be covered by the calibration screen
  // immediately, so it is skipped here. Send 'p' over serial to bring it back at any time.
#else
  lcdScreen.drawRGBBitmap(0, 0, imageData, LCD_WIDTH, LCD_HEIGHT);  // draw the splash image
#endif

  // ---- Decide whether this power-up needs a calibration ----
  if (isHasTouch) {
#if FORCE_CAL_ON_BOOT
    needCalibration = true;
    Serial.println("FORCE_CAL_ON_BOOT = 1, recalibrating on this power-up");
#else
    needCalibration = !loadCalibration();
    if (!needCalibration) {
      Serial.println("Using the saved coefficients. Send 'c' over serial to recalibrate.");
    }
#endif
  }
}

void loop() {
  // ---- Serial commands ----
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'c' || cmd == 'C') {
      needCalibration = true;                                        // recalibrate
    } else if (cmd == 'r' || cmd == 'R') {
      clearCalibration();                                            // erase the coefficients in NVS
      needCalibration = true;
    } else if (cmd == 'p' || cmd == 'P') {
      lcdScreen.drawRGBBitmap(0, 0, imageData, LCD_WIDTH, LCD_HEIGHT); // redraw the splash image
    }
  }

  // Nothing to do when no touch chip is fitted
  if (!isHasTouch) {
    delay(100);
    return;
  }

  // ---- Calibration runs here, inside loop() ----
  if (needCalibration) {
    runCalibration();          // blocking; it handles its own timeouts and retries
    needCalibration = false;   // clear the flag either way, so this cannot loop forever
    if (isCalibrated()) {
      drawVerifyScreen();      // calibration succeeded, show the verification screen
    }
    return;
  }

  // ---- Normal operation: draw a dot where the panel is touched ----
  int16_t x, y;
  if (getTouch(x, y)) {
    lcdScreen.fillCircle(x, y, 2, ST77XX_YELLOW);
  }
  delay(8);
}
