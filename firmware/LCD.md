#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Wire.begin(5, 6);   // SDA=D4, SCL=D5 on XIAO ESP32S3
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GAME_GIRL");

  lcd.setCursor(0, 1);
  lcd.print("PRESS TO PLAY");
}

void loop() {
}