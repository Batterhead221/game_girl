#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_sleep.h"

// ======================================================
// XIAO pin compatibility for ESP32S3 Dev Module builds
// ======================================================
#ifndef D0
  #define D0 1
#endif
#ifndef D1
  #define D1 2
#endif
#ifndef D2
  #define D2 3
#endif
#ifndef D3
  #define D3 4
#endif
#ifndef D4
  #define D4 5
#endif
#ifndef D5
  #define D5 6
#endif
#ifndef D8
  #define D8 7
#endif

// ======================================================
// PIN MAP
// ======================================================
static const int PIN_ENC_CLK    = D0;
static const int PIN_ENC_DT     = D1;
static const int PIN_ENC_SW     = D2;
static const int PIN_BTN_MAIN   = D3;
static const int PIN_OLED_SDA   = D4;
static const int PIN_OLED_SCL   = D5;
static const int PIN_BUZZER     = D8;

// ======================================================
// OLED
// ======================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ======================================================
// BUTTONS
// ======================================================
struct Button {
  int pin;
  bool stableState;
  bool lastReading;
  unsigned long lastDebounceTime;
  const unsigned long debounceDelay = 25;
  bool pressedEvent;
};

Button btnMain { PIN_BTN_MAIN, HIGH, HIGH, 0, 25, false };
Button btnEnc  { PIN_ENC_SW,   HIGH, HIGH, 0, 25, false };

// ======================================================
// ENCODER
// Relative left/right only
// ======================================================
volatile int encoderSteps = 0;
int lastEncoded = 0;
int encoderAcc = 0;
unsigned long lastEncoderTransitionUs = 0;
const unsigned long ENCODER_FILTER_US = 400;

// ======================================================
// SOFT POWER / SLEEP
// ======================================================
unsigned long powerHoldStart = 0;
bool powerHoldActive = false;
const unsigned long POWER_HOLD_MS = 3000;

// ======================================================
// BUZZER
// ======================================================
void buzzerOn()  { digitalWrite(PIN_BUZZER, HIGH); }
void buzzerOff() { digitalWrite(PIN_BUZZER, LOW); }

void beep(uint16_t durationMs) {
  buzzerOn();
  delay(durationMs);
  buzzerOff();
}

void chirp(uint16_t onMs, uint16_t offMs, uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    buzzerOn();
    delay(onMs);
    buzzerOff();
    if (i < count - 1) delay(offMs);
  }
}

void tickSound()    { beep(3); }
void selectSound()  { beep(5); }
void happySound()   { chirp(4, 8, 2); }
void sparkleSound() { chirp(4, 6, 3); }

// ======================================================
// APP STATES
// ======================================================
enum AppState {
  STATE_SPLASH,
  STATE_HOME,
  STATE_DRESSUP,
  STATE_DRESSUP_FINISH,
  STATE_CATCH,
  STATE_CATCH_GAME_OVER,
  STATE_PET,
  STATE_PET_MENU
};

AppState appState = STATE_SPLASH;

// ======================================================
// HOME MENU
// ======================================================
const char* homeItems[] = {
  "DRESS UP",
  "CATCH STARS",
  "POCKET PET"
};
const int HOME_COUNT = sizeof(homeItems) / sizeof(homeItems[0]);
int homeIndex = 0;

// ======================================================
// DRESS UP DATA
// ======================================================
enum Category {
  CAT_FACE,
  CAT_HAIR,
  CAT_OUTFIT,
  CAT_ACCESSORY,
  CAT_PET,
  CAT_BACKGROUND,
  CAT_COUNT
};

const char* categoryNames[CAT_COUNT] = {
  "FACE",
  "HAIR",
  "OUTFIT",
  "ACCESSORY",
  "PET",
  "BACKGROUND"
};

const int FACE_COUNT = 3;
const int HAIR_COUNT = 4;
const int OUTFIT_COUNT = 4;
const int ACCESSORY_COUNT = 4;
const int PET_COUNT = 4;
const int BG_COUNT = 4;

int selectedFace = 0;
int selectedHair = 0;
int selectedOutfit = 0;
int selectedAccessory = 0;
int selectedPet = 0;
int selectedBg = 0;

Category currentCategory = CAT_FACE;

const char* finishItems[] = {
  "BACK TO MENU",
  "RANDOM LOOK"
};
const int FINISH_COUNT = 2;
int finishIndex = 0;

// ======================================================
// CATCH STARS
// ======================================================
int basketX = 52;
const int basketY = 55;
const int basketW = 20;
const int basketH = 6;

int starX = 40;
int starY = 8;
int starVX = 0;
int scoreCatch = 0;
int missesCatch = 0;
const int maxMissesCatch = 3;
unsigned long lastCatchTick = 0;
const unsigned long catchTickMs = 40;

bool magnetActive = false;
unsigned long magnetStart = 0;
const unsigned long magnetMs = 350;

// ======================================================
// POCKET PET
// ======================================================
int petX = 56;
int petY = 46;
int petVY = 0;
bool petOnGround = true;
bool petFacingRight = true;

int petHunger = 2;
int petHappy  = 4;
int petEnergy = 4;

unsigned long lastPetTick = 0;
unsigned long lastNeedTick = 0;
const unsigned long petTickMs = 30;
const unsigned long petNeedMs = 6000;

const char* petMenuItems[] = {
  "FEED",
  "PET",
  "NAP",
  "PLAY",
  "BACK"
};
const int PET_MENU_COUNT = 5;
int petMenuIndex = 0;

bool petBlink = false;
unsigned long lastBlinkMs = 0;

bool showHeart = false;
unsigned long heartStartMs = 0;
const unsigned long heartMs = 700;

bool showZzz = false;
unsigned long zzzStartMs = 0;
const unsigned long zzzMs = 1200;

int ballX = 102;
int ballY = 54;
int ballVX = 0;

// ======================================================
// HELPERS
// ======================================================
void resetPressedEvent(Button &b) {
  b.pressedEvent = false;
}

void updateButton(Button &b) {
  bool reading = digitalRead(b.pin);

  if (reading != b.lastReading) {
    b.lastDebounceTime = millis();
  }

  if ((millis() - b.lastDebounceTime) > b.debounceDelay) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) {
        b.pressedEvent = true;
      }
    }
  }

  b.lastReading = reading;
}

void updateEncoder() {
  int msb = digitalRead(PIN_ENC_CLK);
  int lsb = digitalRead(PIN_ENC_DT);

  int encoded = (msb << 1) | lsb;
  if (encoded == lastEncoded) return;

  unsigned long nowUs = micros();
  if (nowUs - lastEncoderTransitionUs < ENCODER_FILTER_US) {
    return;
  }
  lastEncoderTransitionUs = nowUs;

  int sum = (lastEncoded << 2) | encoded;
  int step = 0;

  // If direction is backwards, swap +1 and -1
  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
    step = +1;
  } else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) {
    step = -1;
  } else {
    lastEncoded = encoded;
    return;
  }

  if ((encoderAcc > 0 && step < 0) || (encoderAcc < 0 && step > 0)) {
    encoderAcc = 0;
  }

  encoderAcc += step;

  if (encoderAcc >= 2) {
    encoderSteps++;
    encoderAcc = 0;
  } else if (encoderAcc <= -2) {
    encoderSteps--;
    encoderAcc = 0;
  }

  lastEncoded = encoded;
}

int consumeEncoderDelta() {
  if (encoderSteps > 0) {
    encoderSteps--;
    return 1;
  }

  if (encoderSteps < 0) {
    encoderSteps++;
    return -1;
  }

  return 0;
}

int getEncoderStep() {
  int delta = consumeEncoderDelta();
  if (delta > 0) return 1;
  if (delta < 0) return -1;
  return 0;
}

void drawCenteredText(const String &text, int y, int textSize = 1) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void wrapValue(int &value, int maxCount) {
  if (value < 0) value = maxCount - 1;
  if (value >= maxCount) value = 0;
}

void drawFrame() {
  display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 6, SSD1306_WHITE);
}

void seedRandom() {
  randomSeed(micros());
}

// ======================================================
// SLEEP
// ======================================================
void enterSleep() {
  display.clearDisplay();
  drawCenteredText("SLEEPING...", 20, 1);
  drawCenteredText("GOOD NIGHT", 34, 1);
  display.display();

  beep(12);
  delay(80);

  display.ssd1306_command(SSD1306_DISPLAYOFF);
  delay(50);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_MAIN, 0);
  esp_deep_sleep_start();
}

void handlePowerHold() {
  if (digitalRead(PIN_BTN_MAIN) == LOW) {
    if (!powerHoldActive) {
      powerHoldActive = true;
      powerHoldStart = millis();
    } else if (millis() - powerHoldStart >= POWER_HOLD_MS) {
      enterSleep();
    }
  } else {
    powerHoldActive = false;
  }
}

// ======================================================
// DRESS UP HELPERS
// ======================================================
void randomizeLook() {
  selectedFace = random(0, FACE_COUNT);
  selectedHair = random(0, HAIR_COUNT);
  selectedOutfit = random(0, OUTFIT_COUNT);
  selectedAccessory = random(0, ACCESSORY_COUNT);
  selectedPet = random(0, PET_COUNT);
  selectedBg = random(0, BG_COUNT);
}

void resetLook() {
  selectedFace = 0;
  selectedHair = 0;
  selectedOutfit = 0;
  selectedAccessory = 0;
  selectedPet = 0;
  selectedBg = 0;
  currentCategory = CAT_FACE;
  finishIndex = 0;
}

int getCurrentValue() {
  switch (currentCategory) {
    case CAT_FACE: return selectedFace;
    case CAT_HAIR: return selectedHair;
    case CAT_OUTFIT: return selectedOutfit;
    case CAT_ACCESSORY: return selectedAccessory;
    case CAT_PET: return selectedPet;
    case CAT_BACKGROUND: return selectedBg;
    default: return 0;
  }
}

void setCurrentValue(int value) {
  switch (currentCategory) {
    case CAT_FACE: selectedFace = value; wrapValue(selectedFace, FACE_COUNT); break;
    case CAT_HAIR: selectedHair = value; wrapValue(selectedHair, HAIR_COUNT); break;
    case CAT_OUTFIT: selectedOutfit = value; wrapValue(selectedOutfit, OUTFIT_COUNT); break;
    case CAT_ACCESSORY: selectedAccessory = value; wrapValue(selectedAccessory, ACCESSORY_COUNT); break;
    case CAT_PET: selectedPet = value; wrapValue(selectedPet, PET_COUNT); break;
    case CAT_BACKGROUND: selectedBg = value; wrapValue(selectedBg, BG_COUNT); break;
    default: break;
  }
}

// ======================================================
// CATCH HELPERS
// ======================================================
void resetCatchGame() {
  basketX = 52;
  starX = random(10, SCREEN_WIDTH - 10);
  starY = 10;
  starVX = 0;
  scoreCatch = 0;
  missesCatch = 0;
  magnetActive = false;
  magnetStart = 0;
  lastCatchTick = millis();
}

void respawnStar() {
  starX = random(8, SCREEN_WIDTH - 8);
  starY = 10;
  starVX = random(-1, 2);
}

bool starCaught() {
  int left = basketX;
  int right = basketX + basketW;
  int sLeft = starX - 2;
  int sRight = starX + 2;
  int sBottom = starY + 2;
  return (sBottom >= basketY) && (sRight >= left) && (sLeft <= right);
}

// ======================================================
// PET HELPERS
// ======================================================
void resetPetGame() {
  petX = 56;
  petY = 46;
  petVY = 0;
  petOnGround = true;
  petFacingRight = true;

  petHunger = 2;
  petHappy = 4;
  petEnergy = 4;

  petMenuIndex = 0;
  lastPetTick = millis();
  lastNeedTick = millis();
  lastBlinkMs = millis();
  petBlink = false;

  showHeart = false;
  showZzz = false;

  ballX = 102;
  ballY = 54;
  ballVX = 0;
}

void clampPetStats() {
  if (petHunger < 0) petHunger = 0;
  if (petHunger > 5) petHunger = 5;
  if (petHappy < 0) petHappy = 0;
  if (petHappy > 5) petHappy = 5;
  if (petEnergy < 0) petEnergy = 0;
  if (petEnergy > 5) petEnergy = 5;
}

void triggerHeart() {
  showHeart = true;
  heartStartMs = millis();
}

void triggerZzz() {
  showZzz = true;
  zzzStartMs = millis();
}

void doPetAction() {
  switch (petMenuIndex) {
    case 0: // FEED
      petHunger -= 2;
      petHappy += 1;
      happySound();
      break;

    case 1: // PET
      petHappy += 1;
      triggerHeart();
      tickSound();
      break;

    case 2: // NAP
      petEnergy += 2;
      petHappy += 1;
      triggerZzz();
      chirp(3, 10, 2);
      break;

    case 3: // PLAY
      petHappy += 2;
      petEnergy -= 1;
      petHunger += 1;
      ballVX = petFacingRight ? 3 : -3;
      sparkleSound();
      break;

    case 4: // BACK
      appState = STATE_PET;
      selectSound();
      return;
  }

  clampPetStats();
}

// ======================================================
// DRESS UP DRAWING
// ======================================================
void drawBackgroundStyle() {
  switch (selectedBg) {
    case 0:
      display.drawPixel(8, 10, SSD1306_WHITE);
      display.drawPixel(20, 18, SSD1306_WHITE);
      display.drawPixel(112, 12, SSD1306_WHITE);
      display.drawPixel(100, 22, SSD1306_WHITE);
      display.drawPixel(14, 52, SSD1306_WHITE);
      display.drawPixel(108, 50, SSD1306_WHITE);
      break;

    case 1:
      display.fillCircle(10, 12, 1, SSD1306_WHITE);
      display.fillCircle(13, 12, 1, SSD1306_WHITE);
      display.drawPixel(11, 14, SSD1306_WHITE);
      display.drawPixel(12, 14, SSD1306_WHITE);
      display.fillCircle(111, 14, 1, SSD1306_WHITE);
      display.fillCircle(114, 14, 1, SSD1306_WHITE);
      display.drawPixel(112, 16, SSD1306_WHITE);
      display.drawPixel(113, 16, SSD1306_WHITE);
      break;

    case 2:
      display.drawCircle(12, 12, 2, SSD1306_WHITE);
      display.drawPixel(12, 9, SSD1306_WHITE);
      display.drawPixel(12, 15, SSD1306_WHITE);
      display.drawPixel(9, 12, SSD1306_WHITE);
      display.drawPixel(15, 12, SSD1306_WHITE);

      display.drawCircle(114, 14, 2, SSD1306_WHITE);
      display.drawPixel(114, 11, SSD1306_WHITE);
      display.drawPixel(114, 17, SSD1306_WHITE);
      display.drawPixel(111, 14, SSD1306_WHITE);
      display.drawPixel(117, 14, SSD1306_WHITE);
      break;

    case 3:
      display.drawCircle(12, 14, 3, SSD1306_WHITE);
      display.drawCircle(16, 14, 3, SSD1306_WHITE);
      display.drawCircle(20, 14, 3, SSD1306_WHITE);

      display.drawCircle(103, 16, 3, SSD1306_WHITE);
      display.drawCircle(107, 16, 3, SSD1306_WHITE);
      display.drawCircle(111, 16, 3, SSD1306_WHITE);
      break;
  }
}

void drawFaceBase(int cx, int cy) {
  display.drawCircle(cx, cy, 10, SSD1306_WHITE);
}

void drawFaceFeatures(int cx, int cy) {
  switch (selectedFace) {
    case 0:
      display.fillCircle(cx - 3, cy - 2, 1, SSD1306_WHITE);
      display.fillCircle(cx + 3, cy - 2, 1, SSD1306_WHITE);
      display.drawPixel(cx - 2, cy + 3, SSD1306_WHITE);
      display.drawPixel(cx - 1, cy + 4, SSD1306_WHITE);
      display.drawPixel(cx,     cy + 4, SSD1306_WHITE);
      display.drawPixel(cx + 1, cy + 4, SSD1306_WHITE);
      display.drawPixel(cx + 2, cy + 3, SSD1306_WHITE);
      break;

    case 1:
      display.drawLine(cx - 4, cy - 2, cx - 2, cy - 2, SSD1306_WHITE);
      display.fillCircle(cx + 3, cy - 2, 1, SSD1306_WHITE);
      display.drawPixel(cx - 1, cy + 3, SSD1306_WHITE);
      display.drawPixel(cx,     cy + 4, SSD1306_WHITE);
      display.drawPixel(cx + 1, cy + 3, SSD1306_WHITE);
      break;

    case 2:
      display.drawLine(cx - 4, cy - 2, cx - 2, cy - 1, SSD1306_WHITE);
      display.drawLine(cx + 2, cy - 1, cx + 4, cy - 2, SSD1306_WHITE);
      display.drawLine(cx - 2, cy + 3, cx + 2, cy + 3, SSD1306_WHITE);
      break;
  }
}

void drawHair(int cx, int cy) {
  switch (selectedHair) {
    case 0:
      display.drawFastHLine(cx - 8, cy - 8, 16, SSD1306_WHITE);
      display.drawLine(cx - 8, cy - 8, cx - 9, cy + 2, SSD1306_WHITE);
      display.drawLine(cx + 8, cy - 8, cx + 9, cy + 2, SSD1306_WHITE);
      break;

    case 1:
      display.drawFastHLine(cx - 8, cy - 8, 16, SSD1306_WHITE);
      display.drawLine(cx - 8, cy - 8, cx - 7, cy + 1, SSD1306_WHITE);
      display.drawLine(cx + 8, cy - 8, cx + 7, cy + 1, SSD1306_WHITE);
      display.drawLine(cx + 8, cy - 3, cx + 13, cy - 6, SSD1306_WHITE);
      display.drawLine(cx + 13, cy - 6, cx + 12, cy, SSD1306_WHITE);
      break;

    case 2:
      display.drawFastHLine(cx - 8, cy - 8, 16, SSD1306_WHITE);
      display.drawLine(cx - 8, cy - 4, cx - 13, cy - 7, SSD1306_WHITE);
      display.drawLine(cx + 8, cy - 4, cx + 13, cy - 7, SSD1306_WHITE);
      display.drawLine(cx - 13, cy - 7, cx - 12, cy - 1, SSD1306_WHITE);
      display.drawLine(cx + 13, cy - 7, cx + 12, cy - 1, SSD1306_WHITE);
      break;

    case 3:
      display.drawFastHLine(cx - 8, cy - 8, 16, SSD1306_WHITE);
      display.drawLine(cx - 8, cy - 8, cx - 10, cy + 10, SSD1306_WHITE);
      display.drawLine(cx + 8, cy - 8, cx + 10, cy + 10, SSD1306_WHITE);
      break;
  }
}

void drawOutfit(int cx, int cy) {
  switch (selectedOutfit) {
    case 0:
      display.drawLine(cx, cy + 12, cx - 8, cy + 22, SSD1306_WHITE);
      display.drawLine(cx, cy + 12, cx + 8, cy + 22, SSD1306_WHITE);
      display.drawLine(cx - 8, cy + 22, cx + 8, cy + 22, SSD1306_WHITE);
      break;

    case 1:
      display.drawLine(cx, cy + 12, cx - 9, cy + 18, SSD1306_WHITE);
      display.drawLine(cx, cy + 12, cx + 9, cy + 18, SSD1306_WHITE);
      display.drawFastHLine(cx - 10, cy + 18, 20, SSD1306_WHITE);
      break;

    case 2:
      display.drawRect(cx - 6, cy + 12, 12, 10, SSD1306_WHITE);
      display.drawLine(cx - 4, cy + 12, cx - 4, cy + 9, SSD1306_WHITE);
      display.drawLine(cx + 4, cy + 12, cx + 4, cy + 9, SSD1306_WHITE);
      break;

    case 3:
      display.drawLine(cx, cy + 12, cx - 10, cy + 24, SSD1306_WHITE);
      display.drawLine(cx, cy + 12, cx + 10, cy + 24, SSD1306_WHITE);
      display.drawFastHLine(cx - 10, cy + 24, 20, SSD1306_WHITE);
      display.drawPixel(cx, cy + 18, SSD1306_WHITE);
      break;
  }

  display.drawLine(cx - 6, cy + 13, cx - 11, cy + 17, SSD1306_WHITE);
  display.drawLine(cx + 6, cy + 13, cx + 11, cy + 17, SSD1306_WHITE);
  display.drawLine(cx - 3, cy + 23, cx - 3, cy + 29, SSD1306_WHITE);
  display.drawLine(cx + 3, cy + 23, cx + 3, cy + 29, SSD1306_WHITE);
}

void drawAccessory(int cx, int cy) {
  switch (selectedAccessory) {
    case 0:
      display.drawPixel(cx + 7, cy - 10, SSD1306_WHITE);
      break;

    case 1:
      display.drawTriangle(cx - 2, cy - 11, cx - 7, cy - 9, cx - 2, cy - 7, SSD1306_WHITE);
      display.drawTriangle(cx + 2, cy - 11, cx + 7, cy - 9, cx + 2, cy - 7, SSD1306_WHITE);
      display.fillCircle(cx, cy - 9, 1, SSD1306_WHITE);
      break;

    case 2:
      display.drawLine(cx - 5, cy - 10, cx + 5, cy - 10, SSD1306_WHITE);
      display.drawLine(cx - 5, cy - 10, cx - 3, cy - 14, SSD1306_WHITE);
      display.drawLine(cx - 3, cy - 14, cx, cy - 10, SSD1306_WHITE);
      display.drawLine(cx, cy - 10, cx + 3, cy - 14, SSD1306_WHITE);
      display.drawLine(cx + 3, cy - 14, cx + 5, cy - 10, SSD1306_WHITE);
      break;

    case 3:
      display.drawCircle(cx + 8, cy - 9, 2, SSD1306_WHITE);
      display.drawPixel(cx + 8, cy - 12, SSD1306_WHITE);
      display.drawPixel(cx + 8, cy - 6, SSD1306_WHITE);
      display.drawPixel(cx + 5, cy - 9, SSD1306_WHITE);
      display.drawPixel(cx + 11, cy - 9, SSD1306_WHITE);
      break;
  }
}

void drawPetAccessory(int x, int y) {
  switch (selectedPet) {
    case 0:
      break;

    case 1:
      display.drawCircle(x, y, 5, SSD1306_WHITE);
      display.drawLine(x - 2, y - 5, x - 2, y - 11, SSD1306_WHITE);
      display.drawLine(x + 2, y - 5, x + 2, y - 11, SSD1306_WHITE);
      display.fillCircle(x - 2, y, 1, SSD1306_WHITE);
      display.fillCircle(x + 2, y, 1, SSD1306_WHITE);
      break;

    case 2:
      display.drawCircle(x, y, 5, SSD1306_WHITE);
      display.drawLine(x - 4, y - 3, x - 6, y - 7, SSD1306_WHITE);
      display.drawLine(x + 4, y - 3, x + 6, y - 7, SSD1306_WHITE);
      display.fillCircle(x - 2, y, 1, SSD1306_WHITE);
      display.fillCircle(x + 2, y, 1, SSD1306_WHITE);
      break;

    case 3:
      display.drawCircle(x, y, 5, SSD1306_WHITE);
      display.drawLine(x + 5, y, x + 8, y + 1, SSD1306_WHITE);
      display.fillCircle(x - 1, y - 1, 1, SSD1306_WHITE);
      display.fillCircle(x + 2, y - 1, 1, SSD1306_WHITE);
      break;
  }
}

void drawCharacterPreview() {
  int cx = 64;
  int cy = 24;

  drawBackgroundStyle();
  drawFaceBase(cx, cy);
  drawHair(cx, cy);
  drawFaceFeatures(cx, cy);
  drawAccessory(cx, cy);
  drawOutfit(cx, cy);

  if (selectedPet != 0) {
    drawPetAccessory(96, 48);
  }
}

// ======================================================
// PET DRAWING
// ======================================================
void drawPetStats() {
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("H:");
  display.print(petHunger);

  display.setCursor(42, 2);
  display.print("P:");
  display.print(petHappy);

  display.setCursor(82, 2);
  display.print("E:");
  display.print(petEnergy);
}

void drawBunny(int x, int y) {
  display.drawRoundRect(x - 7, y - 18, 4, 8, 2, SSD1306_WHITE);
  display.drawRoundRect(x + 3, y - 17, 4, 8, 2, SSD1306_WHITE);

  display.drawRoundRect(x - 9, y - 12, 18, 15, 6, SSD1306_WHITE);
  display.drawRoundRect(x - 6, y + 2, 12, 10, 4, SSD1306_WHITE);

  if (!petBlink) {
    display.fillCircle(x - 3, y - 6, 1, SSD1306_WHITE);
    display.fillCircle(x + 3, y - 6, 1, SSD1306_WHITE);
  } else {
    display.drawLine(x - 4, y - 6, x - 2, y - 6, SSD1306_WHITE);
    display.drawLine(x + 2, y - 6, x + 4, y - 6, SSD1306_WHITE);
  }

  display.drawPixel(x, y - 3, SSD1306_WHITE);
  display.drawPixel(x - 1, y - 1, SSD1306_WHITE);
  display.drawPixel(x,     y,     SSD1306_WHITE);
  display.drawPixel(x + 1, y - 1, SSD1306_WHITE);

  display.drawPixel(x - 6, y - 3, SSD1306_WHITE);
  display.drawPixel(x + 6, y - 3, SSD1306_WHITE);

  display.drawPixel(x - 2, y + 12, SSD1306_WHITE);
  display.drawPixel(x + 2, y + 12, SSD1306_WHITE);

  display.fillCircle(x + 7, y + 7, 1, SSD1306_WHITE);
}

void drawHeartAbovePet(int x, int y) {
  int hy = y - 22;
  display.fillCircle(x - 2, hy, 1, SSD1306_WHITE);
  display.fillCircle(x + 2, hy, 1, SSD1306_WHITE);
  display.drawPixel(x - 3, hy + 1, SSD1306_WHITE);
  display.drawPixel(x + 3, hy + 1, SSD1306_WHITE);
  display.drawPixel(x - 1, hy + 2, SSD1306_WHITE);
  display.drawPixel(x,     hy + 3, SSD1306_WHITE);
  display.drawPixel(x + 1, hy + 2, SSD1306_WHITE);
}

void drawZzzAbovePet(int x, int y) {
  int zx = x + 10;
  int zy = y - 20;
  display.drawLine(zx, zy, zx + 4, zy, SSD1306_WHITE);
  display.drawLine(zx + 4, zy, zx, zy + 4, SSD1306_WHITE);
  display.drawLine(zx, zy + 4, zx + 4, zy + 4, SSD1306_WHITE);

  display.drawLine(zx + 6, zy - 4, zx + 9, zy - 4, SSD1306_WHITE);
  display.drawLine(zx + 9, zy - 4, zx + 6, zy - 1, SSD1306_WHITE);
  display.drawLine(zx + 6, zy - 1, zx + 9, zy - 1, SSD1306_WHITE);
}

void drawBall() {
  display.drawCircle(ballX, ballY, 3, SSD1306_WHITE);
  display.drawPixel(ballX - 1, ballY - 1, SSD1306_WHITE);
  display.drawPixel(ballX + 1, ballY + 1, SSD1306_WHITE);
}

void drawPetRoom() {
  display.clearDisplay();

  drawPetStats();
  display.drawLine(0, 14, SCREEN_WIDTH - 1, 14, SSD1306_WHITE);
  display.drawLine(0, 58, SCREEN_WIDTH - 1, 58, SSD1306_WHITE);

  drawBall();
  drawBunny(petX, petY);

  if (showHeart) {
    drawHeartAbovePet(petX, petY);
  }

  if (showZzz) {
    drawZzzAbovePet(petX, petY);
  }

  display.setCursor(4, 54);
  display.print("MOVE");

  display.setCursor(48, 54);
  display.print("JUMP");

  display.setCursor(92, 54);
  display.print("MENU");

  display.display();
}

void drawPetMenu() {
  display.clearDisplay();
  drawFrame();

  drawCenteredText("POCKET PET", 5, 1);

  for (int i = 0; i < PET_MENU_COUNT; i++) {
    int y = 18 + i * 9;

    if (i == petMenuIndex) {
      display.fillRect(14, y - 1, 100, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(20, y);
      display.setTextSize(1);
      display.print(petMenuItems[i]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(20, y);
      display.setTextSize(1);
      display.print(petMenuItems[i]);
    }
  }

  display.display();
}

// ======================================================
// SCREENS
// ======================================================
void drawSplash() {
  display.clearDisplay();

  display.drawPixel(18, 10, SSD1306_WHITE);
  display.drawPixel(108, 12, SSD1306_WHITE);
  display.drawPixel(22, 20, SSD1306_WHITE);
  display.drawPixel(102, 22, SSD1306_WHITE);

  drawCenteredText("GAME GIRL", 14, 2);
  drawCenteredText("mini arcade", 36, 1);
  drawCenteredText("PRESS BUTTON", 54, 1);

  display.display();
}

void drawHome() {
  display.clearDisplay();
  drawFrame();

  drawCenteredText("HOME", 6, 1);

  for (int i = 0; i < HOME_COUNT; i++) {
    int y = 18 + i * 12;
    if (i == homeIndex) {
      display.fillRoundRect(10, y - 2, 108, 10, 3, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(16, y);
      display.setTextSize(1);
      display.print(homeItems[i]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(16, y);
      display.setTextSize(1);
      display.print(homeItems[i]);
    }
  }

  display.display();
}

void drawDressUp() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print(categoryNames[currentCategory]);

  display.setCursor(86, 2);
  display.print((int)currentCategory + 1);
  display.print("/");
  display.print(CAT_COUNT);

  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  drawCharacterPreview();

  display.setCursor(4, 54);
  display.print("TURN");

  display.setCursor(45, 54);
  display.print("NEXT");

  display.setCursor(92, 54);
  display.print("NEXT");

  display.display();
}

void drawDressUpFinish() {
  display.clearDisplay();

  drawCenteredText("ALL DONE!", 4, 2);
  drawCharacterPreview();

  for (int i = 0; i < FINISH_COUNT; i++) {
    int y = 46 + i * 9;

    if (i == finishIndex) {
      display.fillRect(10, y - 1, 108, 8, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(16, y);
      display.setTextSize(1);
      display.print(finishItems[i]);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(16, y);
      display.setTextSize(1);
      display.print(finishItems[i]);
    }
  }

  display.display();
}

void drawCatch() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("STAR CATCH");

  display.setCursor(2, 12);
  display.print("S:");
  display.print(scoreCatch);

  display.setCursor(64, 12);
  display.print("M:");
  display.print(missesCatch);

  display.drawLine(0, 22, SCREEN_WIDTH - 1, 22, SSD1306_WHITE);

  display.fillCircle(starX, starY, 2, SSD1306_WHITE);
  display.drawPixel(starX, starY - 3, SSD1306_WHITE);
  display.drawPixel(starX, starY + 3, SSD1306_WHITE);
  display.drawPixel(starX - 3, starY, SSD1306_WHITE);
  display.drawPixel(starX + 3, starY, SSD1306_WHITE);

  if (magnetActive) {
    display.drawCircle(basketX + basketW / 2, basketY - 4, 12, SSD1306_WHITE);
  }

  display.drawRoundRect(basketX, basketY, basketW, basketH, 2, SSD1306_WHITE);
  display.drawFastHLine(basketX + 2, basketY + 1, basketW - 4, SSD1306_WHITE);

  display.display();
}

void drawCatchGameOver() {
  display.clearDisplay();
  drawFrame();
  drawCenteredText("NICE TRY!", 10, 2);
  drawCenteredText("SCORE: " + String(scoreCatch), 36, 1);
  drawCenteredText("PRESS FOR MENU", 52, 1);
  display.display();
}

// ======================================================
// LOGIC
// ======================================================
void handleSplash() {
  if (btnMain.pressedEvent || btnEnc.pressedEvent) {
    happySound();
    appState = STATE_HOME;
  }
}

void handleHome() {
  int step = getEncoderStep();

  if (step > 0) {
    homeIndex++;
    if (homeIndex >= HOME_COUNT) homeIndex = 0;
    tickSound();
  } else if (step < 0) {
    homeIndex--;
    if (homeIndex < 0) homeIndex = HOME_COUNT - 1;
    tickSound();
  }

  if (btnMain.pressedEvent || btnEnc.pressedEvent) {
    selectSound();

    if (homeIndex == 0) {
      resetLook();
      appState = STATE_DRESSUP;
    } else if (homeIndex == 1) {
      resetCatchGame();
      appState = STATE_CATCH;
    } else if (homeIndex == 2) {
      resetPetGame();
      appState = STATE_PET;
    }
  }
}

void handleDressUp() {
  int step = getEncoderStep();
  if (step != 0) {
    int v = getCurrentValue();
    v += step;
    setCurrentValue(v);
    tickSound();
  }

  if (btnMain.pressedEvent || btnEnc.pressedEvent) {
    selectSound();

    if (currentCategory == CAT_BACKGROUND) {
      appState = STATE_DRESSUP_FINISH;
      finishIndex = 0;
      sparkleSound();
    } else {
      currentCategory = (Category)((int)currentCategory + 1);
    }
  }
}

void handleDressUpFinish() {
  int step = getEncoderStep();

  if (step > 0) {
    finishIndex++;
    if (finishIndex >= FINISH_COUNT) finishIndex = 0;
    tickSound();
  } else if (step < 0) {
    finishIndex--;
    if (finishIndex < 0) finishIndex = FINISH_COUNT - 1;
    tickSound();
  }

  if (btnMain.pressedEvent || btnEnc.pressedEvent) {
    selectSound();

    if (finishIndex == 0) {
      appState = STATE_HOME;
    } else if (finishIndex == 1) {
      randomizeLook();
      sparkleSound();
    }
  }
}

void handleCatch() {
  int step = getEncoderStep();
  if (step > 0) basketX += 5;
  if (step < 0) basketX -= 5;

  if (basketX < 0) basketX = 0;
  if (basketX > SCREEN_WIDTH - basketW) basketX = SCREEN_WIDTH - basketW;

  if (btnMain.pressedEvent) {
    magnetActive = true;
    magnetStart = millis();
    happySound();
  }

  if (btnEnc.pressedEvent) {
    selectSound();
    appState = STATE_HOME;
    return;
  }

  if (magnetActive && millis() - magnetStart >= magnetMs) {
    magnetActive = false;
  }

  if (millis() - lastCatchTick >= catchTickMs) {
    lastCatchTick = millis();

    if (magnetActive) {
      int basketCenter = basketX + basketW / 2;
      if (starX < basketCenter) starX++;
      if (starX > basketCenter) starX--;
    } else {
      starX += starVX;
      if (starX < 5) starX = 5;
      if (starX > SCREEN_WIDTH - 5) starX = SCREEN_WIDTH - 5;
    }

    starY += 2;

    if (starCaught()) {
      scoreCatch++;
      tickSound();
      respawnStar();
    } else if (starY > SCREEN_HEIGHT) {
      missesCatch++;
      chirp(3, 8, 2);
      respawnStar();
    }

    if (missesCatch >= maxMissesCatch) {
      sparkleSound();
      appState = STATE_CATCH_GAME_OVER;
    }
  }
}

void handleCatchGameOver() {
  if (btnMain.pressedEvent || btnEnc.pressedEvent) {
    selectSound();
    appState = STATE_HOME;
  }
}

void handlePet() {
  int step = getEncoderStep();

  if (step > 0) {
    petX += 4;
    petFacingRight = true;
  } else if (step < 0) {
    petX -= 4;
    petFacingRight = false;
  }

  if (petX < 10) petX = 10;
  if (petX > SCREEN_WIDTH - 10) petX = SCREEN_WIDTH - 10;

  if (btnMain.pressedEvent && petOnGround && petEnergy > 0) {
    petVY = -7;
    petOnGround = false;
    petEnergy--;
    tickSound();
  }

  if (btnEnc.pressedEvent) {
    selectSound();
    appState = STATE_PET_MENU;
    return;
  }

  if (millis() - lastPetTick >= petTickMs) {
    lastPetTick = millis();

    if (!petOnGround) {
      petY += petVY;
      petVY += 1;

      if (petY >= 46) {
        petY = 46;
        petVY = 0;
        petOnGround = true;
      }
    }

    ballX += ballVX;
    if (ballX < 6) {
      ballX = 6;
      ballVX = 0;
    }
    if (ballX > SCREEN_WIDTH - 6) {
      ballX = SCREEN_WIDTH - 6;
      ballVX = 0;
    }
    if (ballVX > 0) ballVX--;
    if (ballVX < 0) ballVX++;

    if (abs(petX - ballX) < 10 && petOnGround) {
      ballVX = petFacingRight ? 2 : -2;
    }
  }

  if (millis() - lastNeedTick >= petNeedMs) {
    lastNeedTick = millis();

    petHunger++;
    petEnergy--;
    petHappy--;

    clampPetStats();
  }

  if (millis() - lastBlinkMs >= 1200) {
    lastBlinkMs = millis();
    petBlink = !petBlink;
  }

  if (showHeart && millis() - heartStartMs >= heartMs) {
    showHeart = false;
  }

  if (showZzz && millis() - zzzStartMs >= zzzMs) {
    showZzz = false;
  }
}

void handlePetMenu() {
  int step = getEncoderStep();

  if (step > 0) {
    petMenuIndex++;
    if (petMenuIndex >= PET_MENU_COUNT) petMenuIndex = 0;
    tickSound();
  } else if (step < 0) {
    petMenuIndex--;
    if (petMenuIndex < 0) petMenuIndex = PET_MENU_COUNT - 1;
    tickSound();
  }

  if (btnMain.pressedEvent) {
    doPetAction();
    if (petMenuIndex != 4) {
      appState = STATE_PET;
    }
  }

  if (btnEnc.pressedEvent) {
    selectSound();
    appState = STATE_PET;
  }
}

// ======================================================
// SETUP / LOOP
// ======================================================
void setup() {
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_BTN_MAIN, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  buzzerOff();

  int msb = digitalRead(PIN_ENC_CLK);
  int lsb = digitalRead(PIN_ENC_DT);
  lastEncoded = (msb << 1) | lsb;
  encoderAcc = 0;
  encoderSteps = 0;
  lastEncoderTransitionUs = 0;

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) {
      delay(100);
    }
  }

  seedRandom();
  resetLook();
  resetCatchGame();
  resetPetGame();

  display.clearDisplay();
  display.display();

  happySound();
}

void loop() {
  updateButton(btnMain);
  updateButton(btnEnc);
  updateEncoder();

  handlePowerHold();

  switch (appState) {
    case STATE_SPLASH:
      handleSplash();
      drawSplash();
      break;

    case STATE_HOME:
      handleHome();
      drawHome();
      break;

    case STATE_DRESSUP:
      handleDressUp();
      drawDressUp();
      break;

    case STATE_DRESSUP_FINISH:
      handleDressUpFinish();
      drawDressUpFinish();
      break;

    case STATE_CATCH:
      handleCatch();
      drawCatch();
      break;

    case STATE_CATCH_GAME_OVER:
      handleCatchGameOver();
      drawCatchGameOver();
      break;

    case STATE_PET:
      handlePet();
      drawPetRoom();
      break;

    case STATE_PET_MENU:
      handlePetMenu();
      drawPetMenu();
      break;
  }

  resetPressedEvent(btnMain);
  resetPressedEvent(btnEnc);

  delay(2);
}