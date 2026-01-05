#include <Wire.h>
#include <DFRobot_RGBLCD1602.h>
#include <SparkFunMLX90632.h>   // SparkFun MLX90632 library

// -------------------- PIN CONFIG (pas aan indien nodig) --------------------
const uint8_t PIN_ENC_A   = 2;   // Encoder A (CLK)  -> interrupt pin aanbevolen
const uint8_t PIN_ENC_B   = 3;   // Encoder B (DT)   -> interrupt pin aanbevolen
const uint8_t PIN_ENC_BTN = 4;   // Encoder drukknop

// -------------------- LCD CONFIG --------------------
// DFRobot RGB LCD1602 (I2C)
DFRobot_RGBLCD1602 lcd(/*lcdCols*/16, /*lcdRows*/2);

// -------------------- MLX90632 CONFIG --------------------
MLX90632 mlx;
const uint8_t MLX_I2C_ADDR = 0x3A; // default 0x3A

// -------------------- APP STATE --------------------
enum Mode : uint8_t { MODE_OPWARMEN, MODE_BAKKEN, MODE_STOP };
Mode mode = MODE_STOP;

float setpointC = 180.0;          // drempelwaarde (nog vrij te kiezen)
const float SETPOINT_MIN = 80.0;
const float SETPOINT_MAX = 260.0;
const float SETPOINT_STEP = 5.0;  // stapgrootte encoder

// Timer (voor bakkentijd)
uint32_t bakeStartMs = 0;
uint32_t bakeElapsedSec = 0;

// Temperatuur
float tempObjC = NAN;

// -------------------- ENCODER (simple decoder) --------------------
volatile int8_t encDelta = 0;     // +1 / -1 ticks
volatile uint8_t lastEncState = 0;

// Debounce button
bool btnPressed = false;
bool btnLast = true;
uint32_t btnLastChangeMs = 0;
const uint32_t BTN_DEBOUNCE_MS = 35;

// UI editing state
bool editingSetpoint = false;     // korte druk: start/stop; lange druk: setpoint edit (simpel)
uint32_t btnPressStartMs = 0;
const uint32_t LONGPRESS_MS = 600;

// LCD refresh
uint32_t lastLcdMs = 0;
const uint32_t LCD_PERIOD_MS = 200;

// Temp read interval
uint32_t lastTempMs = 0;
const uint32_t TEMP_PERIOD_MS = 200;

// -------------------- FUNCTIONS --------------------
void IRAM_ATTR isrEncoder()
{
  // Gray-code based decode (very common approach)
  uint8_t a = digitalRead(PIN_ENC_A);
  uint8_t b = digitalRead(PIN_ENC_B);
  uint8_t state = (a << 1) | b;

  // Transition table-like logic
  // lastEncState -> state
  // 00->01 +, 01->11 +, 11->10 +, 10->00 +
  // reverse is -
  if ((lastEncState == 0b00 && state == 0b01) ||
      (lastEncState == 0b01 && state == 0b11) ||
      (lastEncState == 0b11 && state == 0b10) ||
      (lastEncState == 0b10 && state == 0b00)) {
    encDelta++;
  } else if ((lastEncState == 0b00 && state == 0b10) ||
             (lastEncState == 0b10 && state == 0b11) ||
             (lastEncState == 0b11 && state == 0b01) ||
             (lastEncState == 0b01 && state == 0b00)) {
    encDelta--;
  }

  lastEncState = state;
}

void setBacklightForMode()
{
  // RGB backlight: (R,G,B) 0..255
  // Je kan dit aanpassen naar je voorkeur
  switch (mode) {
    case MODE_OPWARMEN: lcd.setRGB(255, 160, 0); break;  // oranje
    case MODE_BAKKEN:   lcd.setRGB(0, 200, 0);   break;  // groen
    case MODE_STOP:     lcd.setRGB(0, 80, 255);  break;  // blauw
  }
}

const char* modeName()
{
  switch (mode) {
    case MODE_OPWARMEN: return "OPWARM";
    case MODE_BAKKEN:   return "START ";
    case MODE_STOP:     return "STOP  ";
    default:            return "----- ";
  }
}

void updateModeLogic()
{
  // Hier zou later je echte kookplaat-aansturing komen (heating pad aan/uit)
  // Voor nu enkel logica op basis van temperatuur en setpoint.

  if (mode == MODE_OPWARMEN) {
    // Zodra we (bijna) setpoint bereiken, blijven we op opwarmen (of je kan auto-naar START doen)
    // Optioneel: auto switch naar START wanneer temp >= setpoint
  }

  if (mode == MODE_BAKKEN) {
    // Timer bijhouden
    bakeElapsedSec = (millis() - bakeStartMs) / 1000;
  } else {
    bakeElapsedSec = 0;
  }
}

void renderLCD()
{
  // Lijn 1: "T:xxx.xC S:xxx"
  // Lijn 2: "M:START t:012s"
  lcd.setCursor(0, 0);

  // Temp
  lcd.print("T:");
  if (isnan(tempObjC)) {
    lcd.print("---.-");
  } else {
    // 5 chars wide-ish
    if (tempObjC < 100) lcd.print(" ");
    lcd.print(tempObjC, 1);
  }
  lcd.print("C ");

  // Setpoint
  lcd.print("S:");
  int sp = (int)(setpointC + 0.5f);
  if (sp < 100) lcd.print(" ");
  lcd.print(sp);

  // Lijn 2
  lcd.setCursor(0, 1);
  lcd.print("M:");
  lcd.print(modeName());

  lcd.print(" ");

  // Timer
  lcd.print("t:");
  if (mode == MODE_BAKKEN) {
    if (bakeElapsedSec < 100) lcd.print("0");
    if (bakeElapsedSec < 10)  lcd.print("0");
    lcd.print(bakeElapsedSec);
    lcd.print("s");
  } else {
    lcd.print("---s");
  }

  // Kleine indicator voor edit mode
  if (editingSetpoint) {
    lcd.setCursor(15, 1);
    lcd.print("*");
  } else {
    lcd.setCursor(15, 1);
    lcd.print(" ");
  }
}

void readTemperature()
{
  // SparkFun library: object temperature read
  // (Functie-namen kunnen per lib-versie verschillen; dit werkt met de typische SparkFun MLX90632 API)
  // Als je compile-error krijgt: zeg mij welke foutmelding en ik pas het exact aan jouw lib-versie aan.
  tempObjC = mlx.getObjectTemp();  // object temp in °C
}

void handleEncoder()
{
  int8_t delta;
  noInterrupts();
  delta = encDelta;
  encDelta = 0;
  interrupts();

  if (delta == 0) return;

  if (editingSetpoint) {
    setpointC += delta * SETPOINT_STEP;
    if (setpointC < SETPOINT_MIN) setpointC = SETPOINT_MIN;
    if (setpointC > SETPOINT_MAX) setpointC = SETPOINT_MAX;
  } else {
    // Scroll door modes
    // delta>0: vooruit, delta<0: achteruit
    int m = (int)mode + (delta > 0 ? 1 : -1);
    if (m < 0) m = 2;
    if (m > 2) m = 0;
    mode = (Mode)m;
    setBacklightForMode();

    // Als je naar START gaat, timer reset/start pas bij bevestiging (knop)
  }
}

void handleButton()
{
  bool now = digitalRead(PIN_ENC_BTN); // INPUT_PULLUP: false=pressed
  uint32_t ms = millis();

  if (now != btnLast && (ms - btnLastChangeMs) > BTN_DEBOUNCE_MS) {
    btnLastChangeMs = ms;
    btnLast = now;

    if (now == false) {
      // pressed
      btnPressStartMs = ms;
      btnPressed = true;
    } else {
      // released
      uint32_t pressDur = ms - btnPressStartMs;
      btnPressed = false;

      if (pressDur >= LONGPRESS_MS) {
        // long press: toggle setpoint edit
        editingSetpoint = !editingSetpoint;
      } else {
        // short press: confirm action
        if (!editingSetpoint) {
          if (mode == MODE_BAKKEN) {
            // start baking timer
            bakeStartMs = ms;
          }
          // STOP: niets extra
          // OPWARMEN: niets extra
        } else {
          // in edit: short press exits edit
          editingSetpoint = false;
        }
      }
    }
  }

  // while holding, you could show something, not needed now
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();

  // Encoder pins
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_BTN, INPUT_PULLUP);

  // init encoder state
  lastEncState = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);

  // interrupts
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), isrEncoder, CHANGE);

  // LCD init
  lcd.init();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Slimme Kookplaat");
  lcd.setCursor(0, 1);
  lcd.print("Pancake mode...");
  delay(900);

  // MLX init
  // SparkFun voorbeeld toont init met address opties :contentReference[oaicite:3]{index=3}
  if (!mlx.begin(MLX_I2C_ADDR, Wire)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("MLX90632 FAIL");
    lcd.setCursor(0, 1);
    lcd.print("Check wiring");
    while (1) {
      delay(200);
    }
  }

  mode = MODE_STOP;
  setBacklightForMode();
  lcd.clear();
}

void loop()
{
  handleButton();
  handleEncoder();
  updateModeLogic();

  uint32_t ms = millis();

  if (ms - lastTempMs >= TEMP_PERIOD_MS) {
    lastTempMs = ms;
    readTemperature();
  }

  if (ms - lastLcdMs >= LCD_PERIOD_MS) {
    lastLcdMs = ms;
    renderLCD();
  }
}
