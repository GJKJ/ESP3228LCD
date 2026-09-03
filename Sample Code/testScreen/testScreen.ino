#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

/* ---------- LCD Screen Port---------- */
#define LCD_DC    2  // Data Command control pin on ESP32 D2
#define LCD_RST   4  // Reset pin (could connect to RST pin) on ESP32 D4
#define LCD_CS    15  // Chip select control pin on ESP32 D15
#define LCD_BLK   32  // Black Light Pin on ESP32 D32

/* ---------- LCD Screen ,Touch and SD Shared VSPI bus ---------- */
#define SPI_SCLK 18 // SCL Pin on ESP32 D18
#define SPI_MISO 19 // MISO Pin on ESP32 D19
#define SPI_MOSI 23 // SDA Pin on ESP32 D23


// Panel resolution
#define LCD_WIDTH  240
#define LCD_HEIGHT 320

/*  -------------------- Panel colour setting ------------------------
  Two panel types are used on this board. If the background of the
  demo screen comes up WHITE instead of BLACK, change "true" to "false".
  This is a setting, not a fault      */
bool isDisplayInverted = true;  

Adafruit_ST7789 lcdScreen(&SPI, LCD_CS, LCD_DC, LCD_RST);

void setup(void) {
  Serial.begin(115200);
  
  pinMode(LCD_BLK, OUTPUT);
  digitalWrite(LCD_BLK, HIGH);
  SPI.begin();
  
  lcdScreen.init(LCD_WIDTH, LCD_HEIGHT);   
  lcdScreen.setRotation(2);
  lcdScreen.fillScreen(ST77XX_BLACK);
  if(isDisplayInverted){
    lcdScreen.invertDisplay(true);
  }else{
    lcdScreen.invertDisplay(false);
  }
  
  lcdScreen.setTextColor(ST77XX_GREEN);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 10);
  lcdScreen.println("Hello,ideaspark!");

  lcdScreen.setTextColor(ST77XX_CYAN);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 120);
  lcdScreen.println("Tutorial/Code URL:");

  lcdScreen.setTextColor(ST77XX_YELLOW);
  lcdScreen.setTextSize(2);
  lcdScreen.setCursor(10, 140);
  lcdScreen.println("www.github.com/GJKJ");
  lcdScreen.setCursor(10, 160);
  lcdScreen.println("/ESP3228LCD");
  
  delay(1000);
}

void loop() {
  delay(600000);
}
