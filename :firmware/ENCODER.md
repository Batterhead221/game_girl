#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int ENC_A  = 1;   // D0
const int ENC_B  = 2;   // D1
const int ENC_SW = 3;   // D2

int count = 0;
int lastAState = HIGH;
unsigned long lastStepTime = 0;
const unsigned long stepDelayMs = 3;

void drawScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GAME_GIRL");
  lcd.setCursor(0, 1);
  lcd.print("COUNT: ");
  lcd.print(count);
}

void setup() {
  Wire.begin(5, 6);   // SDA=D4, SCL=D5
  lcd.init();
  lcd.backlight();

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  lastAState = digitalRead(ENC_A);
  drawScreen();
}

void loop() {
  int aState = digitalRead(ENC_A);

  if (aState != lastAState) {
    if (millis() - lastStepTime > stepDelayMs) {
      if (aState == LOW) {
        if (digitalRead(ENC_B) != aState) {
          count++;
        } else {
          count--;
        }
        drawScreen();
        lastStepTime = millis();
      }
    }
  }

  lastAState = aState;

  if (digitalRead(ENC_SW) == LOW) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("BUTTON PRESS");
    lcd.setCursor(0, 1);
    lcd.print("COUNT: ");
    lcd.print(count);
    delay(250);

    while (digitalRead(ENC_SW) == LOW) {
      delay(10);
    }

    drawScreen();
  }
}