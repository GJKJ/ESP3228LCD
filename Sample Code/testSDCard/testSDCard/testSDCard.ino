#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <SD.h>

/* ---------- SD card Port---------- */
#define SD_CS 13

/* ---------- LCD Screen ,Touch and SD Shared VSPI bus ---------- */
#define SPI_SCLK 18 // SCL Pin on ESP32 D18
#define SPI_MISO 19 // MISO Pin on ESP32 D19
#define SPI_MOSI 23 // SDA Pin on ESP32 D23

File SD_file;

void setup() {
  Serial.begin(115200);
  delay(200);;

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);

  /* Init SD Card */
  if (!SD.begin(SD_CS, SPI, 4000000)) {
    Serial.println("SD Card init failed!");
    while (true) {
      delay(1000);
    }
  }
  Serial.println("SD Card init done.");
  delay(1000);

  /* Write file */
  SD_file = SD.open("/test.txt", FILE_WRITE);
  if (!SD_file) {
    Serial.println("Failed to create or open test.txt");
    return;
  }
  SD_file.println("Hello,ideaspark!");
  SD_file.close();
  Serial.println("Write message:");
  Serial.println("Hello,ideaspark!");
  
  /* Read file */
  Serial.println("Reading file contents:");
  SD_file = SD.open("/test.txt");
  if (SD_file) {
    while (SD_file.available()) {
      Serial.write(SD_file.read());
    }
    SD_file.close();
  } else {
    Serial.println("Failed to open file.");
  }
}

void loop() {
  delay(600000);
}
