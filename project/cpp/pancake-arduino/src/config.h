/* ===== SETTINGS ===== */
static constexpr unsigned long DEBOUNCE_MS = 10;
static constexpr unsigned long LCD_INTERVAL_MS = 500;
static constexpr unsigned long PUBLISH_INTERVAL_MS = 1000;
static constexpr unsigned long MQTT_RECONNECT_MS = 5000;
static constexpr unsigned long WIFI_RECONNECT_MS = 5000;

static constexpr unsigned long BTN_DEBOUNCE_MS = 30;

// Keep within target +/- 2C
static constexpr double TEMP_BAND_C = 2.0;

// Absolute safety limit in real temperature
static constexpr double MAX_REAL_TEMP_C = 50.0;

// MQTT simulation scaling: real 50C -> simulated 100C
static constexpr double TEMP_SIM_SCALE = 2.0;

// Manual program tuning (only used when a manual program is sent)
static constexpr double MANUAL_STEP_C = 1.0;
static constexpr double MANUAL_MIN_C  = 0.0;
static constexpr double MANUAL_MAX_C  = 120.0;

/* ===== RELAY POLARITY =====
   If your heating pad turns ON at boot, your relay is likely ACTIVE-LOW.
   For most relay modules with IN pulled up internally: set RELAY_ACTIVE_HIGH = false.
*/
static constexpr bool RELAY_ACTIVE_HIGH = true; // change to false if your relay is active-low