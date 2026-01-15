import os
import json
import paho.mqtt.client as mqtt
from db import get_program_settings

def mqtt_publish(topic, payload_obj, retain=True):
    mqtt_host = os.getenv("MQTT_HOST")
    mqtt_port = int(os.getenv("MQTT_PORT"))
    mqtt_user = os.getenv("MQTT_USERNAME")  # "the golden flip"
    mqtt_pass = os.getenv("MQTT_PASSWORD")

    payload = json.dumps(payload_obj)

    client = mqtt.Client(client_id="flask-pancake")
    client.username_pw_set(mqtt_user, mqtt_pass)
    client.connect(mqtt_host, mqtt_port, 60)
    client.publish(topic, payload, qos=1, retain=retain)
    client.disconnect()

def publish_program_to_arduino(user_id, program_id, session_id):
    phases = get_program_settings(user_id, program_id)
    if isinstance(phases, str):
        return phases

    payload_obj = {
        "program_id": int(program_id),
        "session_id": int(session_id),
        "phases": [
            {
                "phase": row["phase"],
                "target_temperature": float(row["target_temperature"])
            }
            for row in phases
        ]
    }

    mqtt_publish("/cmd", payload_obj, retain=True)
    return True

def publish_wifi_settings(ssid, password):
    payload_obj = {
        "ssid": ssid,
        "password": password
    }
    mqtt_publish("/wifi", payload_obj, retain=False)
    return True