#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_RGBLCD1602.h>
#include <Adafruit_MLX90632.h>

// -------------------- PIN CONFIG --------------------
const uint8_t PIN_ENC_A   = 2;
const uint8_t PIN_ENC_B   = 3; 
const uint8_t PIN_ENC_BTN = 4; 

// -------------------- LCD --------------------
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2);

// -------------------- MLX90632 --------------------
Adafruit_MLX90632 mlx;
const uint8_t MLX_I2C_ADDR = 0x3A;
const float BRANDWONDE_TEMP = 25.0; // °C → risico op brandwonden

// -------------------- MODES --------------------
enum Mode { OPWARMEN, BAKKEN, STOP };
Mode mode = STOP;

// -------------------- PARAMETERS --------------------
float setpointC = 180.0;
float tempC = 0.0;

uint32_t bakeStartMs = 0;
uint32_t bakeTimeSec = 0;

// Encoder
volatile int8_t encoderDelta = 0;
volatile uint8_t lastState = 0;

// UI
bool editSetpoint = false;

// -------------------- ENCODER ISR --------------------
void ISR_encoder() {
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);
  uint8_t state = (a << 1) | b;

  if ((lastState == 0b00 && state == 0b01) ||
      (lastState == 0b01 && state == 0b11) ||
      (lastState == 0b11 && state == 0b10) ||
      (lastState == 0b10 && state == 0b00)) {
    encoderDelta++;
  }
  else if ((lastState == 0b00 && state == 0b10) ||
           (lastState == 0b10 && state == 0b11) ||
           (lastState == 0b11 && state == 0b01) ||
           (lastState == 0b01 && state == 0b00)) {
    encoderDelta--;
  }

  lastState = state;
}

// -------------------- LCD --------------------
void updateLCD() {

  // --------- ACHTERGROND KLEUR (veiligheid) ----------
  if (tempC >= BRANDWONDE_TEMP) {
    lcd.setRGB(255, 0, 0);   // ROOD = gevaar
  } else {
    lcd.setRGB(0, 120, 255); // NORMAAL (blauw)
  }

  lcd.clear();

  // --------- BOVENSTE RIJ: START / STOP ----------
  lcd.setCursor(0, 0);

  if (mode == BAKKEN) {
    lcd.print("START");
  } else {
    lcd.print("start");
  }

  lcd.setCursor(11, 0);

  if (mode == STOP) {
    lcd.print("STOP");
  } else {
    lcd.print("stop");
  }

  // --------- ONDERSTE RIJ: TEMP LINKS ----------
  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(tempC, 0);
  lcd.print((char)223); // °
  lcd.print("C");

  // --------- TIMER RECHTS ----------
  lcd.setCursor(9, 1);
  lcd.print("t:");

  if (mode == BAKKEN) {
    if (bakeTimeSec < 100) lcd.print("0");
    if (bakeTimeSec < 10)  lcd.print("0");
    lcd.print(bakeTimeSec);
    lcd.print("s");
  } else {
    lcd.print("---s");
  }
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);

  lastState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);

  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), ISR_encoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), ISR_encoder, CHANGE);

  lcd.init();
  lcd.setRGB(0, 120, 255);
  lcd.clear();
  lcd.print("Slimme Kookplaat");

  if (!mlx.begin(MLX_I2C_ADDR)) {
    lcd.setCursor(0, 1);
    lcd.print("MLX FAIL");
    while (1);
  }

  delay(1000);
  lcd.clear();
}

// -------------------- LOOP --------------------
void loop() {

  // Encoder draaien
  if (encoderDelta != 0) {
    if (editSetpoint) {
      setpointC += encoderDelta * 5;
      setpointC = constrain(setpointC, 80, 260);
    } else {
      mode = (Mode)((mode + (encoderDelta > 0 ? 1 : 2)) % 3);
    }
    encoderDelta = 0;
  }

  // Knop
  static bool lastBtn = HIGH;
  bool btn = digitalRead(PIN_ENC_BTN);

  if (lastBtn == HIGH && btn == LOW) {
    if (editSetpoint) {
      editSetpoint = false;
    } else {
      if (mode == BAKKEN) {
        bakeStartMs = millis();
      }
    }
  }

  if (lastBtn == LOW && btn == HIGH) {
    editSetpoint = !editSetpoint;
    delay(300);
  }

  lastBtn = btn;

  // Temperatuur uitlezen
  tempC = mlx.getObjectTemperature();

  // Timer
  if (mode == BAKKEN) {
    bakeTimeSec = (millis() - bakeStartMs) / 1000;
  } else {
    bakeTimeSec = 0;
  }

  updateLCD();
  delay(200);
}
