#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("BOOT OK");
  Serial.println("STARTING I2C SCAN");

  Wire.begin(5, 6);   // SDA=D4, SCL=D5 on XIAO ESP32S3
  delay(100);
}

void loop() {
  byte error, address;
  int count = 0;

  Serial.println("SCANNING...");

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("FOUND I2C DEVICE AT 0X");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      count++;
    }
  }

  if (count == 0) {
    Serial.println("NO I2C DEVICES FOUND");
  } else {
    Serial.println("SCAN DONE");
  }

  Serial.println("-----");
  delay(3000);
}