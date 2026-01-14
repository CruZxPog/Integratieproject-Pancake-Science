#include <Arduino.h>

#include <WiFi.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>

#include "secret.h"

WiFiClient network;
MQTTClient mqtt = MQTTClient(256);

unsigned long lastPublishTime = 0;
unsigned long currentInterval;

void messageHandler(String &topic, String &payload) {
    Serial.println("UNO R4 - received from MQTT:");
    Serial.print("- topic: ");
    Serial.println(topic);
    Serial.print("- payload: ");
    Serial.println(payload);

    if (topic == SUBSCRIBE_TOPIC) {
        // arduino will get the settings for the session from mqtt broker save the settings temporarily
        // or the arduino will get a new netword settings from the payload to connect to another network this will only happen on setup so check what kind of payload it is
        // for now just print the payload
        Serial.println("UNO R4 - processing payload...");
        Serial.println(payload);
    }
}

void connectToMQTT() {
    Serial.print("Connecting to MQTT broker at ");
    Serial.print(MQTT_BROKER);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    mqtt.begin(MQTT_BROKER, MQTT_PORT, network);
    mqtt.onMessage(messageHandler);

    Serial.println("UNO R4 - Connecting to MQTT broker");

    while (!mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
        Serial.print("Connect failed. lastError=");
        Serial.print(mqtt.lastError());
        Serial.print(" returnCode=");
        Serial.println(mqtt.returnCode());
        delay(1000);  // slow down retries so you can read
    }

    Serial.println("UNO R4 - MQTT broker Connected!");

    if (mqtt.subscribe(SUBSCRIBE_TOPIC)) {
        Serial.print("UNO R4 - Subscribed to the topic: ");
    } else {
        Serial.print("UNO R4 - Failed to subscribe to the topic: ");
    }
    Serial.println(SUBSCRIBE_TOPIC);
}

void sendToMQTT(const char* buffer, size_t n) {
    bool ok = mqtt.publish(PUBLISH_TOPIC, buffer, n);
    Serial.println("UNO R4 - sent to MQTT:");
    Serial.print("- topic: ");
    Serial.println(PUBLISH_TOPIC);
    Serial.print("- payload: ");
    Serial.println(buffer);
    Serial.print("- status: ");
    Serial.println(ok ? "OK" : "FAILED");
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  connectToMQTT();
}

void loop() {
}