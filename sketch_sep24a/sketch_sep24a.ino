#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <DHT.h>

// --- 1. ตั้งค่าการเชื่อมต่อ Wi-Fi ---
const char* ssid = "Honey";
const char* password = "bb150020";

// --- 2. ตั้งค่าการเชื่อมต่อ HiveMQ Cloud ---
const char* mqtt_server = "340a042cf4e44973a1aac9b1a254d881.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "Eiei";
const char* mqtt_pass = "12345678";

// --- 3. กำหนดขา Pin ของ ESP32 ---
const int PIN_SERVO = 18;
const int PIN_TRIG  = 5;
const int PIN_ECHO  = 19;
const int PIN_DHT   = 4;
const int PIN_LED   = 2;

// --- 4. ปรับระยะ Calibrate ตามขนาดจริงของกล่องปัจจุบัน ---
const float DIST_EMPTY = 19.3; // ระยะตอนอาหารหมดเกลี้ยง (0%)
const float DIST_FULL  = 13.0; // ระยะตอนอาหารเต็มตามระดับปัจจุบัน (100%)

WiFiClientSecure espClient;
PubSubClient client(espClient);
Servo feederServo;
DHT dht(PIN_DHT, DHT11);

// ฟังก์ชันสั่งปล่อยอาหารด้วย Servo
void dispenseFood() {
  Serial.println("Action: Dispensing Food...");
  digitalWrite(PIN_LED, HIGH);
  
  feederServo.write(0);        // หมุนเปิดช่องปล่อยอาหาร
  delay(500);                  // รอให้อาหารไหล 0.6 วินาที
  feederServo.write(45);       // หมุนกลับมาปิดล็อกที่ 45 องศา
  delay(400);                  // หน่วงเวลาให้เข้าล็อก
  
  digitalWrite(PIN_LED, LOW);
  
  // แจ้งสถานะสำเร็จกลับไปยัง Dashboard
  client.publish("petfeeder/status/event", "{\"status\": \"fed_success\"}");
}

// ฟังก์ชันรับ Callback คำสั่งจาก MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Incoming Topic: ");
  Serial.println(topic);
  Serial.println("Payload: " + message);

  if (String(topic) == "petfeeder/cmd/feed") {
    dispenseFood();
  }
}

// ฟังก์ชันยิงสัญญาณ Ultrasonic วัดระยะทาง 1 ครั้ง (หน่วย: ซม.)
float readSingleDistance() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 30000); // timeout 30ms
  if (duration == 0) return -1.0;

  return (duration * 0.0343) / 2.0;
}

// สลับค่าตัวแปรสำหรับเรียงลำดับ
void swap(float &a, float &b) {
  float temp = a;
  a = b;
  b = temp;
}

// ฟังก์ชันคำนวณ % อาหารคงเหลือด้วย Median Filter 7 ค่า
float getRemainingPercent() {
  const int READ_COUNT = 7;
  float readings[READ_COUNT];
  int validCount = 0;

  for (int i = 0; i < READ_COUNT; i++) {
    float d = readSingleDistance();
    if (d >= 4.0 && d <= 30.0) {
      readings[validCount] = d;
      validCount++;
    }
    delay(30);
  }

  if (validCount == 0) return 0.0;

  // เรียงลำดับค่าน้อยไปมาก
  for (int i = 0; i < validCount - 1; i++) {
    for (int j = 0; j < validCount - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        swap(readings[j], readings[j + 1]);
      }
    }
  }

  // เลือกค่ามัธยฐานตำแหน่งตรงกลางเพื่อตัดค่าสวิง
  float medianDist = readings[validCount / 2];

  Serial.print("Filtered Median Distance: ");
  Serial.print(medianDist);
  Serial.println(" cm");

  // แปลงค่าระยะทางเป็นร้อยละ 0 - 100%
  float percent = ((DIST_EMPTY - medianDist) / (DIST_EMPTY - DIST_FULL)) * 100.0;

  if (percent <= 2.0) percent = 0.0;
  if (percent > 100.0) percent = 100.0;

  return percent;
}

// ฟังก์ชันเชื่อมต่อ HiveMQ Cloud
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to HiveMQ Cloud...");
    String clientId = "ESP32_PetFeeder_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("Connected!");
      client.subscribe("petfeeder/cmd/feed");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 3 seconds...");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED, OUTPUT);

  dht.begin();
  feederServo.attach(PIN_SERVO);
  feederServo.write(45); // ตั้งตำแหน่งปิดเริ่มต้นที่ 45 องศา

  // เชื่อมต่อ Wi-Fi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());

  // ตั้งค่าความปลอดภัย SSL สำหรับ HiveMQ
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

unsigned long lastTime = 0;
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // ส่งข้อมูล Telemetry อัปเดตขึ้น Dashboard ทุก 3 วินาที
  if (millis() - lastTime > 3000) {
    lastTime = millis();

    float pct = getRemainingPercent();
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (isnan(temp)) temp = 0.0;
    if (isnan(hum)) hum = 0.0;

    char msg[128];
    snprintf(msg, sizeof(msg), "{\"food_pct\": %.1f, \"temp\": %.1f, \"hum\": %.1f}", pct, temp, hum);
    client.publish("petfeeder/telemetry/data", msg);
    Serial.print("Telemetry Sent: ");
    Serial.println(msg);
  }
}