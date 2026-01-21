#ifndef MYSECRETS_H
#define MYSECRETS_H

/* ===== WIFI ===== */
#define WIFI_SSID "IoT"
#define WIFI_PASSWORD "KdGIoT13!"

/* setup network (uncomment to use) */
// #define WIFI_SSID "PancakeScience-Setup"
// #define WIFI_PASSWORD "pannenkoek123"

/* ===== MQTT ===== */
#define MQTT_BROKER "meowpi.local"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "pancake-uno-r4-01"
#define MQTT_USERNAME "the golden flip"
#define MQTT_PASSWORD "pannenkoek"

/* ===== MQTT TOPICS ===== */
#define PUBLISH_TOPIC "/measurement"
#define SUBSCRIBE_TOPIC "/cmd"
#define SUBSCRIBE_WIFI_TOPIC "/wifi"

#endif // MYSECRETS_H