from flask import Flask, render_template, jsonify
import paho.mqtt.client as mqtt
import json
import ssl
import datetime

app = Flask(__name__)

# --- ตั้งค่า HiveMQ Cloud (ให้ตรงกับใน ESP32) ---
HIVEMQ_HOST = "340a042cf4e44973a1aac9b1a254d881.s1.eu.hivemq.cloud"
HIVEMQ_PORT = 8883
HIVEMQ_USER = "Eiei"
HIVEMQ_PASS = "12345678"

# ตัวแปรเก็บข้อมูลเรียลไทม์
system_data = {
    "food_pct": 0.0,
    "temp": 0.0,
    "hum": 0.0,
    "last_feed": "ยังไม่มีประวัติ"
}

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to HiveMQ Broker successfully!")
        client.subscribe("petfeeder/telemetry/data")
        client.subscribe("petfeeder/status/event")
    else:
        print(f"Failed to connect, return code: {rc}")

def on_message(client, userdata, msg):
    global system_data
    try:
        payload = json.loads(msg.payload.decode())
        topic = msg.topic

        if topic == "petfeeder/telemetry/data":
            system_data["food_pct"] = payload.get("food_pct", 0.0)
            system_data["temp"] = payload.get("temp", 0.0)
            system_data["hum"] = payload.get("hum", 0.0)
        elif topic == "petfeeder/status/event":
            now = datetime.datetime.now().strftime("%H:%M:%S")
            system_data["last_feed"] = f"สำเร็จเมื่อ {now}"
    except Exception as e:
        print("Error parsing payload:", e)

# เริ่มต้นการเชื่อมต่อ MQTT Client
mqtt_client = mqtt.Client()
mqtt_client.username_pw_set(HIVEMQ_USER, HIVEMQ_PASS)
mqtt_client.tls_set(cert_reqs=ssl.CERT_NONE)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

mqtt_client.connect(HIVEMQ_HOST, HIVEMQ_PORT, 60)
mqtt_client.loop_start()

@app.route('/')
def home():
    return render_template('index.html')

# API เมื่อกดปุ่มสั่งอาหารจากหน้าเว็บ
@app.route('/api/feed', methods=['POST'])
def feed():
    mqtt_client.publish("petfeeder/cmd/feed", json.dumps({"action": "dispense"}))
    return jsonify({"status": "command_sent"})

# API ให้หน้าเว็บดึงข้อมูลล่าสุด
@app.route('/api/status', methods=['GET'])
def get_status():
    return jsonify(system_data)

import os

if __name__ == '__main__':
    port = int(os.environ.get("PORT", 8080))
    app.run(host='0.0.0.0', port=port)