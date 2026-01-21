#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90632.h>
#include "DFRobot_RGBLCD1602.h"

// UNO R4 WiFi
#include <WiFiS3.h>

#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "secret.h"
#include "config.h"

/* ===== PINS =====
   Encoder CONTROL: spin (used only in manual program), press toggles session start/stop
   Encoder EVENT: press-only, sends event "point" when something interesting happens
*/
static constexpr uint8_t PIN_ENC_CTRL_A  = 2;
static constexpr uint8_t PIN_ENC_CTRL_B  = 3;
static constexpr uint8_t PIN_ENC_CTRL_SW = 4;

static constexpr uint8_t PIN_ENC_EVT_SW  = 7;

static constexpr uint8_t PIN_RELAY = 8;

/* ===== RELAY POLARITY =====
   If your heating pad turns ON at boot, your relay is likely ACTIVE-LOW.
   For most relay modules with IN pulled up internally: set RELAY_ACTIVE_HIGH = false.
*/
static constexpr bool RELAY_ACTIVE_HIGH = true; // change to false if your relay is active-low

/* ===== WIFI PERSISTENCE ===== */
static constexpr uint32_t WIFI_MAGIC = 0xC0FFEE01;
static constexpr int EEPROM_MAGIC_ADDR = 0;
static constexpr int EEPROM_SSID_ADDR  = 4;        // 32 + null
static constexpr int EEPROM_PASS_ADDR  = 4 + 33;   // 64 + null

static uint8_t wifiFailCount = 0;
static bool wifiWasConnected = false;

static bool loadWifiFromEEPROM(char* ssidOut, size_t ssidLen, char* passOut, size_t passLen) {
  uint32_t magic = 0;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);
  if (magic != WIFI_MAGIC) return false;

  char ssidBuf[33] = {0};
  char passBuf[65] = {0};

  EEPROM.get(EEPROM_SSID_ADDR, ssidBuf);
  EEPROM.get(EEPROM_PASS_ADDR, passBuf);

  if (ssidBuf[0] == '\0') return false;

  strncpy(ssidOut, ssidBuf, ssidLen - 1);
  ssidOut[ssidLen - 1] = '\0';

  strncpy(passOut, passBuf, passLen - 1);
  passOut[passLen - 1] = '\0';

  return true;
}

static void saveWifiToEEPROM(const char* ssid, const char* pass) {
  uint32_t magic = WIFI_MAGIC;
  EEPROM.put(EEPROM_MAGIC_ADDR, magic);

  char ssidBuf[33] = {0};
  char passBuf[65] = {0};

  strncpy(ssidBuf, ssid ? ssid : "", 32);
  strncpy(passBuf, pass ? pass : "", 64);

  EEPROM.put(EEPROM_SSID_ADDR, ssidBuf);
  EEPROM.put(EEPROM_PASS_ADDR, passBuf);
}

static void clearStoredWifi() {
  uint32_t zero = 0;
  EEPROM.put(EEPROM_MAGIC_ADDR, zero);
  Serial.println("[WiFi] Stored creds cleared");
}

static void connectWifiBestEffort() {
  char ssid[33] = {0};
  char pass[65] = {0};

  if (loadWifiFromEEPROM(ssid, sizeof(ssid), pass, sizeof(pass))) {
    Serial.print("[WiFi] Using stored SSID: ");
    Serial.println(ssid);
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
  } else {
    Serial.print("[WiFi] Using compiled SSID: ");
    Serial.println(WIFI_SSID);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

/* ===== DEVICES ===== */
Adafruit_MLX90632 mlx;
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2);
WiFiClient network;
MQTTClient mqtt(256);

/* ===== PROGRAM SETTINGS CACHE ===== */
struct PhaseTarget {
  char phase[16];
  double target; // real target in C (capped <= 50)
};

static int currentSessionId = 0;
static int currentProgramId = 0;

static PhaseTarget phaseTargets[8];
static uint8_t phaseCount = 0;

static int currentPhaseIndex = 0;
static char currentPhase[16] = "opwarmen";
static double currentTargetTemp = NAN;

/* ===== SESSION + MODE ===== */
static bool sessionArmed = false;     // got settings, waiting for start press
static bool sessionStarted = false;   // running
static bool manualProgram = false;    // set by received program (not by buttons)
static double manualTargetTemp = NAN; // real target in C (capped <= 50)

/* ===== RUNTIME STATE ===== */
// Control encoder spin decode
static int ctrlLastEncoded = 0;
static unsigned long ctrlLastStepTime = 0;

// LCD + publish timing
static unsigned long lastLcdUpdate = 0;
static unsigned long lastPublish = 0;
static unsigned long lastMqttReconnectAttempt = 0;
static unsigned long lastWifiReconnectAttempt = 0;

static bool relayOn = false;

static double lastTemp = NAN;
static double lastTempShown = NAN;

// Events: one-shot, only these allowed:
// "session start", "point", "session end"
static char pendingEvent[24] = "";

// CONTROL press debounce
static int ctrlBtnLastReading = HIGH;
static int ctrlBtnStableState = HIGH;
static unsigned long ctrlBtnLastChange = 0;

// EVENT press debounce
static int evtBtnLastReading = HIGH;
static int evtBtnStableState = HIGH;
static unsigned long evtBtnLastChange = 0;

/* ===== HELPERS ===== */
static void setRelay(bool on) {
  relayOn = on;

  if (RELAY_ACTIVE_HIGH) {
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  } else {
    digitalWrite(PIN_RELAY, on ? LOW : HIGH);
  }
}

static double clampDouble(double v, double lo, double hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void updateTargetForCurrentPhase() {
  currentTargetTemp = NAN;

  for (uint8_t i = 0; i < phaseCount; i++) {
    if (strcmp(phaseTargets[i].phase, currentPhase) == 0) {
      currentTargetTemp = phaseTargets[i].target;
      break;
    }
  }

  Serial.print("[CFG] Phase=");
  Serial.print(currentPhase);
  Serial.print(" Target=");
  if (isfinite(currentTargetTemp)) Serial.println(currentTargetTemp);
  else Serial.println("N/A");
}

static void printReceivedPhases() {
  Serial.println("[CFG] Received phases:");
  for (uint8_t i = 0; i < phaseCount; i++) {
    Serial.print("  ");
    Serial.print(i);
    Serial.print(": ");
    Serial.print(phaseTargets[i].phase);
    Serial.print(" -> ");
    Serial.println(phaseTargets[i].target, 1);
  }
}

static void startSession() {
  if (!sessionArmed) return;

  sessionStarted = true;
  sessionArmed = false;

  // Reset for auto program
  currentPhaseIndex = 0;
  if (!manualProgram) {
    if (phaseCount > 0) {
      strncpy(currentPhase, phaseTargets[0].phase, 15);
      currentPhase[15] = '\0';
    }
    updateTargetForCurrentPhase();
  } else {
    if (!isfinite(manualTargetTemp)) manualTargetTemp = 30.0;
    manualTargetTemp = clampDouble(manualTargetTemp, MANUAL_MIN_C, MAX_REAL_TEMP_C);
    Serial.print("[MODE] Manual program, start target=");
    Serial.println(manualTargetTemp, 1);
  }

  // Always start with relay OFF
  setRelay(false);

  strncpy(pendingEvent, "session start", sizeof(pendingEvent) - 1);
  pendingEvent[sizeof(pendingEvent) - 1] = '\0';

  Serial.println("[CFG] Session started");
}

static void endSession() {
  if (!sessionStarted) return;

  sessionStarted = false;
  sessionArmed = false;

  // Always stop heating immediately
  setRelay(false);

  // Clear targets so LCD doesn't show old values while idle
  currentTargetTemp = NAN;
  manualTargetTemp = NAN;

  strncpy(pendingEvent, "session end", sizeof(pendingEvent) - 1);
  pendingEvent[sizeof(pendingEvent) - 1] = '\0';

  Serial.println("[CFG] Session ended");
}

static double activeTargetTemp() {
  double t = manualProgram ? manualTargetTemp : currentTargetTemp;
  if (isfinite(t)) {
    t = clampDouble(t, MANUAL_MIN_C, MAX_REAL_TEMP_C);
  }
  return t;
}

static void advancePhaseIfReady(double t) {
  if (!sessionStarted) return;
  if (manualProgram) return;
  if (phaseCount == 0) return;
  if (!isfinite(t)) return;
  if (!isfinite(currentTargetTemp)) return;

  if (t < currentTargetTemp) return;
  if (currentPhaseIndex >= (int)phaseCount - 1) return;

  currentPhaseIndex++;

  strncpy(currentPhase, phaseTargets[currentPhaseIndex].phase, 15);
  currentPhase[15] = '\0';

  Serial.print("[CFG] Auto phase -> ");
  Serial.println(currentPhase);

  updateTargetForCurrentPhase();

  // IMPORTANT: no event for phase changes
}

// Deadband control: keep within target +/- 2C
static void updateRelayWithDeadband(double t) {
  // Hard failsafe: never heat without a running session
  if (!sessionStarted) {
    if (relayOn) setRelay(false);
    return;
  }

  // Absolute safety limit: never go above 50C in real temp
  if (isfinite(t) && t >= MAX_REAL_TEMP_C) {
    setRelay(false);
    return;
  }

  double target = activeTargetTemp(); // already capped <= 50
  if (!isfinite(t)) return;
  if (!isfinite(target)) return;

  double low  = target - TEMP_BAND_C; // below: turn ON
  double high = target + TEMP_BAND_C; // above: turn OFF

  if (!relayOn && t <= low) setRelay(true);
  else if (relayOn && t >= high) setRelay(false);
}

// CONTROL encoder spin: only affects manualTargetTemp, and only when manual program is running
static void updateControlEncoderSpin(unsigned long now) {
  int MSB = digitalRead(PIN_ENC_CTRL_A);
  int LSB = digitalRead(PIN_ENC_CTRL_B);
  int encoded = (MSB << 1) | LSB;
  int sum = (ctrlLastEncoded << 2) | encoded;

  if (now - ctrlLastStepTime > DEBOUNCE_MS) {
    int delta = 0;
    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) delta = +1;
    else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) delta = -1;

    if (delta != 0) {
      ctrlLastStepTime = now;

      if (sessionStarted && manualProgram) {
        if (!isfinite(manualTargetTemp)) manualTargetTemp = 30.0;
        manualTargetTemp += (double)delta * MANUAL_STEP_C;
        manualTargetTemp = clampDouble(manualTargetTemp, MANUAL_MIN_C, MAX_REAL_TEMP_C);

        Serial.print("[MANUAL] target=");
        Serial.println(manualTargetTemp, 1);
      }
    }
  }

  ctrlLastEncoded = encoded;
}

static double readTemperatureNonBlocking(double lastKnown) {
  if (mlx.isNewData()) {
    double t = mlx.getObjectTemperature();
    mlx.resetNewData();
    return t;
  }
  return lastKnown;
}

static void updateLCD(unsigned long now, double temp) {
  bool needUpdate =
    (!isfinite(lastTempShown) && isfinite(temp)) ||
    (isfinite(temp) && (!isfinite(lastTempShown) || fabs(temp - lastTempShown) >= 0.05)) ||
    (now - lastLcdUpdate > LCD_INTERVAL_MS);

  if (!needUpdate) return;

  lcd.setCursor(0, 0);
  lcd.print("S:");
  if (currentSessionId > 0) lcd.print(currentSessionId);
  else lcd.print("-");
  lcd.print(" ");

  if (sessionArmed && !sessionStarted) lcd.print("WAIT");
  else if (sessionStarted) lcd.print("RUN ");
  else lcd.print("IDLE");

  lcd.print(" ");
  if (manualProgram) lcd.print("MAN");
  else lcd.print("AUT");
  lcd.print("  ");

  lcd.setCursor(0, 1);
  if (isfinite(temp)) {
    lcd.print("T:");
    lcd.print(temp, 1);
    lcd.print((char)223);
    lcd.print("C ");

    double target = activeTargetTemp();
    if (sessionStarted && isfinite(target)) {
      lcd.print(">");
      lcd.print(target, 0);
      lcd.print(" ");
    } else if (sessionArmed && !sessionStarted) {
      lcd.print("BTN ");
    } else {
      lcd.print(">-- ");
    }

    if (sessionArmed && !sessionStarted) lcd.setRGB(255, 255, 0);
    else if (relayOn) lcd.setRGB(255, 0, 0);
    else lcd.setRGB(0, 255, 0);
  } else {
    lcd.print("T:---.-C >--    ");
    lcd.setRGB(255, 255, 0);
  }

  lastLcdUpdate = now;
  lastTempShown = temp;
}

static void publishMeasurement(unsigned long now, double temp) {
  if (!sessionStarted) return;
  if (now - lastPublish < PUBLISH_INTERVAL_MS) return;
  lastPublish = now;

  if (!mqtt.connected()) return;
  if (currentSessionId <= 0) return;
  if (!isfinite(temp)) return;

  // Publish simulated values (x2)
  double tempSim = temp * TEMP_SIM_SCALE;

  StaticJsonDocument<240> doc;
  doc["session_id"] = currentSessionId;
  doc["temperature"] = tempSim;

  if (manualProgram) {
    doc["phase"] = "manual";
    if (isfinite(manualTargetTemp)) doc["target_temperature"] = manualTargetTemp * TEMP_SIM_SCALE;
  } else {
    doc["phase"] = currentPhase;
    if (isfinite(currentTargetTemp)) doc["target_temperature"] = currentTargetTemp * TEMP_SIM_SCALE;
  }

  // Only include event when there is a one-shot event pending
  if (pendingEvent[0] != '\0') {
    doc["event"] = pendingEvent;
    pendingEvent[0] = '\0';
  }

  char buffer[256];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));
  bool ok = mqtt.publish(PUBLISH_TOPIC, buffer, n);

  if (!ok) Serial.println("[MQTT] publish failed");
  else {
    Serial.print("[MQTT] pub ");
    Serial.println(buffer);
  }
}

static bool ensureWiFiConnected(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) Serial.println("[WiFi] connected");
    wifiWasConnected = true;
    wifiFailCount = 0;
    return true;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    wifiFailCount = 0;
  }

  if (now - lastWifiReconnectAttempt < WIFI_RECONNECT_MS) return false;
  lastWifiReconnectAttempt = now;

  wifiFailCount++;
  Serial.print("[WiFi] reconnect attempt ");
  Serial.print(wifiFailCount);
  Serial.println("/10");

  if (wifiFailCount >= 10) {
    clearStoredWifi();
    wifiFailCount = 0;

    Serial.println("[WiFi] fallback to compiled creds");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    return false;
  }

  Serial.println("[WiFi] reconnecting...");
  connectWifiBestEffort();
  return false;
}

/* ===== MQTT MESSAGE HANDLER ===== */
static void mqttMessageHandler(String &topic, String &payload) {
  payload.trim();

  if (topic == SUBSCRIBE_TOPIC) {
    StaticJsonDocument<640> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.println("[CFG] invalid JSON");
      return;
    }

    currentProgramId = doc["program_id"] | 0;
    currentSessionId = doc["session_id"] | 0;

    bool explicitManual = false;
    if (doc.containsKey("manual_mode")) {
      explicitManual = (bool)doc["manual_mode"];
    } else if (doc.containsKey("mode")) {
      const char* mode = doc["mode"];
      if (mode && strcmp(mode, "manual") == 0) explicitManual = true;
    }

    JsonArray phases = doc["phases"];
    phaseCount = 0;

    Serial.print("[CFG] program_id=");
    Serial.print(currentProgramId);
    Serial.print(" session_id=");
    Serial.println(currentSessionId);

    if (!phases.isNull()) {
      for (JsonObject p : phases) {
        if (phaseCount >= 8) break;

        const char* name = p["phase"];
        double targetSim = p["target_temperature"];

        if (!name || !isfinite(targetSim)) continue;

        // Convert simulated target to real (divide by 2), then cap to 50C
        double targetReal = targetSim / TEMP_SIM_SCALE;
        targetReal = clampDouble(targetReal, MANUAL_MIN_C, MAX_REAL_TEMP_C);

        strncpy(phaseTargets[phaseCount].phase, name, 15);
        phaseTargets[phaseCount].phase[15] = '\0';
        phaseTargets[phaseCount].target = targetReal;
        phaseCount++;
      }
    }

    Serial.print("[CFG] phases=");
    Serial.println(phaseCount);

    if (phaseCount > 0) printReceivedPhases();

    // Decide program type:
    manualProgram = explicitManual || (phaseCount == 0);

    if (manualProgram) {
      if (doc.containsKey("target_temperature")) {
        double targetSim = (double)doc["target_temperature"];
        manualTargetTemp = targetSim / TEMP_SIM_SCALE; // sim -> real
      } else {
        manualTargetTemp = 30.0;
      }
      manualTargetTemp = clampDouble(manualTargetTemp, MANUAL_MIN_C, MAX_REAL_TEMP_C);
      Serial.print("[MODE] Manual program armed, target=");
      Serial.println(manualTargetTemp, 1);
    } else {
      currentPhaseIndex = 0;
      strncpy(currentPhase, phaseTargets[0].phase, 15);
      currentPhase[15] = '\0';
      updateTargetForCurrentPhase();
    }

    // Clear any leftover event from previous run/settings
    pendingEvent[0] = '\0';

    // Arm session, do NOT auto start
    sessionArmed = true;
    sessionStarted = false;
    setRelay(false);

    Serial.println("[CFG] Waiting for CONTROL press to start (press again to end)");
    return;
  }

  if (topic == SUBSCRIBE_WIFI_TOPIC) {
    Serial.print("[WIFI CMD] ");
    Serial.println(payload);

    if (payload == "reconnect") {
      connectWifiBestEffort();
      Serial.println("[WIFI CMD] reconnecting...");
      return;
    }

    StaticJsonDocument<192> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      Serial.println("[WIFI CMD] invalid JSON");
      return;
    }

    const char* ssid = doc["ssid"];
    const char* pass = doc["password"];

    if (!ssid || ssid[0] == '\0') {
      Serial.println("[WIFI CMD] missing ssid");
      return;
    }

    saveWifiToEEPROM(ssid, pass ? pass : "");
    Serial.println("[WIFI CMD] saved creds, connecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, pass ? pass : "");
    return;
  }
}

static bool ensureMQTTConnected(unsigned long now) {
  if (mqtt.connected()) return true;

  if (WiFi.status() != WL_CONNECTED) return false;
  if (now - lastMqttReconnectAttempt < MQTT_RECONNECT_MS) return false;
  lastMqttReconnectAttempt = now;

  Serial.print("[MQTT] connecting...");
  mqtt.begin(MQTT_BROKER, MQTT_PORT, network);
  mqtt.onMessage(mqttMessageHandler);

  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.println(" OK");
    mqtt.subscribe(SUBSCRIBE_TOPIC);
    mqtt.subscribe(SUBSCRIBE_WIFI_TOPIC);
    return true;
  }

  Serial.println(" failed");
  return false;
}

/* ===== BUTTON HANDLERS ===== */

// CONTROL press: start if armed, else end if running
static void handleControlPress(unsigned long now) {
  int reading = digitalRead(PIN_ENC_CTRL_SW);

  if (reading != ctrlBtnLastReading) {
    ctrlBtnLastReading = reading;
    ctrlBtnLastChange = now;
  }

  if (now - ctrlBtnLastChange >= BTN_DEBOUNCE_MS) {
    if (reading != ctrlBtnStableState) {
      ctrlBtnStableState = reading;

      if (ctrlBtnStableState == LOW) {
        if (sessionArmed && !sessionStarted) startSession();
        else if (sessionStarted) endSession();
      }
    }
  }
}

// EVENT press: only send "point"
static void handleEventPress(unsigned long now) {
  int reading = digitalRead(PIN_ENC_EVT_SW);

  if (reading != evtBtnLastReading) {
    evtBtnLastReading = reading;
    evtBtnLastChange = now;
  }

  if (now - evtBtnLastChange >= BTN_DEBOUNCE_MS) {
    if (reading != evtBtnStableState) {
      evtBtnStableState = reading;

      if (evtBtnStableState == LOW) {
        if (sessionStarted) {
          strncpy(pendingEvent, "point", sizeof(pendingEvent) - 1);
          pendingEvent[sizeof(pendingEvent) - 1] = '\0';
          Serial.println("[EVT] point");
        }
      }
    }
  }
}

/* ===== SETUP ===== */
void setup() {
  Serial.begin(115200);

  pinMode(PIN_ENC_CTRL_A, INPUT_PULLUP);
  pinMode(PIN_ENC_CTRL_B, INPUT_PULLUP);
  pinMode(PIN_ENC_CTRL_SW, INPUT_PULLUP);

  pinMode(PIN_ENC_EVT_SW, INPUT_PULLUP);

  // Zet eerst de output-latch naar OFF niveau, dan pas OUTPUT maken
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  pinMode(PIN_RELAY, OUTPUT);

  // Force relay OFF immediately on boot
  setRelay(false);

  Wire.begin();
  Wire.setClock(400000);

  lcd.init();
  lcd.setRGB(0, 255, 0);
  lcd.setCursor(0, 0);
  lcd.print("System start");
  lcd.setCursor(0, 1);
  lcd.print("WiFi...");

  if (!mlx.begin()) {
    lcd.clear();
    lcd.setRGB(255, 0, 0);
    lcd.setCursor(0, 0);
    lcd.print("Sensor fout!");
    while (true) delay(1000);
  }

  mlx.setMode(MLX90632_MODE_CONTINUOUS);
  mlx.setMeasurementSelect(MLX90632_MEAS_MEDICAL);
  mlx.setRefreshRate(MLX90632_REFRESH_2HZ);
  mlx.resetNewData();

  connectWifiBestEffort();
}

/* ===== LOOP ===== */
void loop() {
  unsigned long now = millis();

  // Hard failsafe: never heat without session running
  if (!sessionStarted) {
    setRelay(false);
  }

  ensureWiFiConnected(now);
  ensureMQTTConnected(now);
  mqtt.loop();

  // Control encoder spin: only used for manual program while running
  updateControlEncoderSpin(now);

  // Buttons
  handleControlPress(now);
  handleEventPress(now);

  // Sensor always reads, regardless of session state
  lastTemp = readTemperatureNonBlocking(lastTemp);

  // Auto program phase advance only while running
  advancePhaseIfReady(lastTemp);

  // Relay control only while running (otherwise forced OFF)
  updateRelayWithDeadband(lastTemp);

  // LCD always shows current temperature (and status)
  updateLCD(now, lastTemp);

  // MQTT publishing only while running
  publishMeasurement(now, lastTemp);
}