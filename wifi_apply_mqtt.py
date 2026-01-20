# THIS FILE SHOULD BE IN THE ROOT OF THE SYSTEM OR IT WONT WORK
import json
import subprocess
import paho.mqtt.client as mqtt

MQTT_HOST = "127.0.0.1"
MQTT_PORT = 1883
MQTT_USER = "CHANGE_ME"
MQTT_PASS = "CHANGE_ME"
TOPIC = "CHANGE_ME"


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = (p.stdout or "") + (p.stderr or "")
    return p.returncode, out.strip()


def get_wifi_dev():
    code, out = run(["nmcli", "-t", "-f", "DEVICE,TYPE", "device"])
    if code != 0:
        return None

    for line in out.splitlines():
        dev, dev_type = line.split(":")
        if dev_type == "wifi":
            return dev

    return None

def connect_wifi(ssid, password):
    if not ssid or not password:
        return False, "Missing ssid/password"
    if len(password) < 8:
        return False, "Password must be at least 8 chars"

    dev = get_wifi_dev()
    if not dev:
        return False, "No WiFi device found"

    # Fresh scan helps a lot
    run(["nmcli", "dev", "wifi", "rescan", "ifname", dev])

    # First try the simple way
    code, out = run([
        "nmcli", "dev", "wifi", "connect",
        ssid, "password", password,
        "ifname", dev
    ])

    # If NM can't infer security, force WPA-PSK by creating/updating a profile
    if code != 0 and ("key-mgmt" in out or "property is missing" in out):
        # Add profile if it doesn't exist
        run(["nmcli", "con", "add", "type", "wifi", "ifname", dev, "con-name", ssid, "ssid", ssid])
        run(["nmcli", "con", "modify", ssid, "wifi-sec.key-mgmt", "wpa-psk"])
        run(["nmcli", "con", "modify", ssid, "wifi-sec.psk", password])
        run(["nmcli", "con", "modify", ssid, "connection.autoconnect", "yes"])

        code, out = run(["nmcli", "con", "up", ssid])

    if code != 0:
        return False, out

    return True, f"Connected to {ssid}"


def on_connect(client, userdata, flags, rc, properties=None):
    client.subscribe(TOPIC)
    print(f"[WiFi] Subscribed to {TOPIC}")


def on_message(client, userdata, msg):
    try:
        print(f"[WiFi] Received payload: {msg.payload!r}")
        payload = json.loads(msg.payload.decode("utf-8"))

        ssid = (payload.get("ssid") or "").strip()
        password = (payload.get("password") or "").strip()

        ok, info = connect_wifi(ssid, password)
        print(f"[WiFi] ok={ok} info={info}")

    except Exception as e:
        print(f"[WiFi] Error: {e}")


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.username_pw_set(MQTT_USER, MQTT_PASS)
client.on_connect = on_connect
client.on_message = on_message

client.connect(MQTT_HOST, MQTT_PORT, 60)
client.loop_forever()
