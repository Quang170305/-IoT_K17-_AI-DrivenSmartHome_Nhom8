/*
 * ============================================================
 *  SMART HOME — ESP32S NodeMCU CP2102
 *  Keypad 4×4 + Servo SG90 + RFID RC522 + MQTT
 *  DHT22 ×2 + Quạt DC 12V (L298N Mini)
 * ============================================================
 *  Mật khẩu mặc định : 1234  (đổi được qua MQTT)
 *  Nhấn # để xác nhận | * để xóa
 * ============================================================
 *  KEYPAD → ESP32S:
 *    ROW1 → GPIO13 | ROW2 → GPIO12
 *    ROW3 → GPIO14 | ROW4 → GPIO26
 *    COL1 → GPIO27 | COL2 → GPIO33
 *    COL3 → GPIO32 | COL4 → GPIO25
 *
 *  SERVO SG90:
 *    Signal → GPIO17 | VCC → VIN(5V) | GND → GND
 *
 *  RFID RC522:
 *    SDA  → GPIO5  | SCK  → GPIO18
 *    MOSI → GPIO23 | MISO → GPIO19
 *    RST  → GPIO22 | 3.3V → 3.3V | GND → GND
 *
 *  DHT22 Phòng 1:
 *    DATA → GPIO4  | VCC → 3.3V | GND → GND
 *    (điện trở 10kΩ pull-up từ DATA lên 3.3V)
 *
 *  DHT22 Phòng 2:
 *    DATA → GPIO16 | VCC → 3.3V | GND → GND
 *    (điện trở 10kΩ pull-up từ DATA lên 3.3V)
 *
 *  L298N Mini:
 *    IN1 → GPIO25  (Quạt phòng 1)
 *    IN2 → GPIO2   (Quạt phòng 2)
 *    12V → Adapter 12V
 *    GND → GND chung
 * ============================================================
 */

#include <Arduino.h>
#include <Keypad.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>
#include "mqtt_handler.h"

// ============================================================
//  CẤU HÌNH CỬA
// ============================================================
String currentPassword = "1234";
#define MAX_DIGITS      10

#define SERVO_PIN    17
#define SERVO_LOCK   0
#define SERVO_UNLOCK 90

#define RFID_SS_PIN  5
#define RFID_RST_PIN 22

// ============================================================
//  CẤU HÌNH NHIỆT ĐỘ
// ============================================================
#define DHT_PIN_1     4       // DHT22 phòng 1
#define DHT_PIN_2     16      // DHT22 phòng 2
#define DHT_TYPE      DHT22

#define FAN_PIN_1     25      // L298N IN1 → quạt phòng 1
#define FAN_PIN_2     2       // L298N IN2 → quạt phòng 2

#define TEMP_ON       30.0    // °C → bật quạt
#define TEMP_OFF      28.0    // °C → tắt quạt
#define DHT_INTERVAL  5000    // Đọc DHT mỗi 5 giây

// ============================================================
//  ĐỐI TƯỢNG
// ============================================================
Servo   doorServo;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
DHT     dht1(DHT_PIN_1, DHT_TYPE);
DHT     dht2(DHT_PIN_2, DHT_TYPE);

// ── Keypad 4×4 ──────────────────────────────────────────────
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {26, 14, 12, 13};
byte colPins[COLS] = {25, 32, 33, 27};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ── UID thẻ RFID được phép ──────────────────────────────────
byte authorizedUID[][4] = {
  // {0xA1, 0xB2, 0xC3, 0xD4},   // ← điền UID thực vào đây
};
const int NUM_CARDS = sizeof(authorizedUID) / sizeof(authorizedUID[0]);

// ============================================================
//  BIẾN TRẠNG THÁI
// ============================================================
String        inputBuffer = "";
bool          doorOpen    = false;
unsigned long openTime    = 0;

// DHT22
float         temp1 = 0, humi1 = 0;
float         temp2 = 0, humi2 = 0;
bool          fan1On = false;
bool          fan2On = false;
unsigned long lastDHTRead = 0;

// ============================================================
//  ĐIỀU KHIỂN CỬA
// ============================================================
void lockDoor() {
  doorServo.write(SERVO_LOCK);
  doorOpen = false;
  Serial.println("🔒 CỬA ĐÃ KHÓA");
  Serial.println("────────────────────────────────");
  Serial.println("Nhập mật khẩu + #  hoặc  quẹt thẻ:");
}

void unlockDoor(String method) {
  doorServo.write(SERVO_UNLOCK);
  doorOpen = true;
  openTime = millis();
  Serial.println("✅ " + method + " — CỬA MỞ!");
  if (mqttClient.connected()) {
    publishEvent("door", method.c_str(), true);
    publishStatus();
  }
}

void wrongPassword() {
  Serial.println("❌ MẬT KHẨU SAI! Thử lại.");
  if (mqttClient.connected())
    publishEvent("keypad", "wrong_password", false);
  for (int i = 0; i < 2; i++) {
    doorServo.write(SERVO_LOCK + 15); delay(150);
    doorServo.write(SERVO_LOCK);      delay(150);
  }
}

void wrongCard(String uid) {
  Serial.println("❌ THẺ KHÔNG HỢP LỆ: " + uid);
  if (mqttClient.connected())
    publishEvent("rfid", ("wrong_card:" + uid).c_str(), false);
  for (int i = 0; i < 2; i++) {
    doorServo.write(SERVO_LOCK + 15); delay(150);
    doorServo.write(SERVO_LOCK);      delay(150);
  }
}

// ============================================================
//  RFID HELPERS
// ============================================================
bool isAuthorizedCard() {
  for (int i = 0; i < NUM_CARDS; i++) {
    bool ok = true;
    for (int j = 0; j < 4; j++)
      if (rfid.uid.uidByte[j] != authorizedUID[i][j]) { ok = false; break; }
    if (ok) return true;
  }
  return false;
}

String getUID() {
  String uid = "";
  for (int i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  return uid;
}

// ============================================================
//  ĐIỀU KHIỂN QUẠT
// ============================================================
void setFan1(bool on) {
  if (fan1On == on) return;   // không thay đổi nếu đã đúng trạng thái
  fan1On = on;
  digitalWrite(FAN_PIN_1, on ? HIGH : LOW);
  Serial.printf("🌀 Quạt Phòng 1: %s (%.1f°C)\n", on ? "BẬT" : "TẮT", temp1);
  if (mqttClient.connected()) {
    publishTempFan(1, temp1, humi1, fan1On);
  }
}

void setFan2(bool on) {
  if (fan2On == on) return;
  fan2On = on;
  digitalWrite(FAN_PIN_2, on ? HIGH : LOW);
  Serial.printf("🌀 Quạt Phòng 2: %s (%.1f°C)\n", on ? "BẬT" : "TẮT", temp2);
  if (mqttClient.connected()) {
    publishTempFan(2, temp2, humi2, fan2On);
  }
}

// ============================================================
//  ĐỌC DHT22
// ============================================================
void readDHT() {
  // ── Phòng 1 ──
  float t1 = dht1.readTemperature();
  float h1 = dht1.readHumidity();
  if (!isnan(t1) && !isnan(h1)) {
    temp1 = t1; humi1 = h1;
    Serial.printf("🌡️  P1: %.1f°C  💧%.1f%%\n", temp1, humi1);
    // Logic bật/tắt quạt với ngưỡng trễ (hysteresis)
    if (temp1 >= TEMP_ON)  setFan1(true);
    if (temp1 <= TEMP_OFF) setFan1(false);
  } else {
    Serial.println("⚠️  DHT22 Phòng 1 đọc lỗi!");
  }

  // ── Phòng 2 ──
  float t2 = dht2.readTemperature();
  float h2 = dht2.readHumidity();
  if (!isnan(t2) && !isnan(h2)) {
    temp2 = t2; humi2 = h2;
    Serial.printf("🌡️  P2: %.1f°C  💧%.1f%%\n", temp2, humi2);
    if (temp2 >= TEMP_ON)  setFan2(true);
    if (temp2 <= TEMP_OFF) setFan2(false);
  } else {
    Serial.println("⚠️  DHT22 Phòng 2 đọc lỗi!");
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  // Quạt output
  pinMode(FAN_PIN_1, OUTPUT);
  pinMode(FAN_PIN_2, OUTPUT);
  digitalWrite(FAN_PIN_1, LOW);
  digitalWrite(FAN_PIN_2, LOW);

  // DHT22
  dht1.begin();
  dht2.begin();
  Serial.println("✓ DHT22 ×2 khởi động");

  // RFID
  SPI.begin(18, 19, 23, RFID_SS_PIN);
  rfid.PCD_Init();
  Serial.printf("✓ RFID RC522 v%X\n", rfid.PCD_ReadRegister(rfid.VersionReg));

  // Servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN, 500, 2400);
  lockDoor();
  delay(300);

  // WiFi + MQTT
  mqttSetup();

  Serial.println("\n================================================");
  Serial.println("   SMART HOME sẵn sàng!");
  Serial.printf ("   Mật khẩu : %s\n", currentPassword.c_str());
  Serial.printf ("   Số thẻ   : %d\n", NUM_CARDS);
  Serial.printf ("   Bật quạt : >= %.0f°C | Tắt: <= %.0f°C\n", TEMP_ON, TEMP_OFF);
  Serial.println("   MQTT Topics:");
  Serial.println("   ← home/cmd/door     : unlock/lock");
  Serial.println("   ← home/cmd/password : đổi mật khẩu");
  Serial.println("   ← home/cmd/fan      : {room:1,action:on/off}");
  Serial.println("   → home/door/event   : sự kiện cửa");
  Serial.println("   → home/temp/room1   : nhiệt độ phòng 1");
  Serial.println("   → home/temp/room2   : nhiệt độ phòng 2");
  Serial.println("   → home/status       : trạng thái");
  Serial.println("================================================\n");

  // Đọc DHT lần đầu ngay khi khởi động
  delay(2000);
  readDHT();
  lastDHTRead = millis();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {

  mqttLoop();

  // ── ĐỌC DHT22 mỗi 5 giây ─────────────────────────────────
  if (millis() - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = millis();
    readDHT();
  }

  // ── KEYPAD ────────────────────────────────────────────────
  char key = keypad.getKey();
  if (key) {
    switch (key) {

      case '#':
        Serial.print("Đã nhập: [");
        for (int i = 0; i < (int)inputBuffer.length(); i++) Serial.print('*');
        Serial.println("]");
        if (inputBuffer == currentPassword) {
          unlockDoor("KEYPAD");
        } else {
          wrongPassword();
          Serial.println("Nhập lại:");
        }
        inputBuffer = "";
        break;

      case '*':
        inputBuffer = "";
        Serial.println("🔄 Đã xóa. Nhập lại:");
        break;

      case 'A': case 'B': case 'C': case 'D':
        break;

      default:
        if ((int)inputBuffer.length() < MAX_DIGITS) {
          inputBuffer += key;
          Serial.print("Nhập: ");
          for (int i = 0; i < (int)inputBuffer.length(); i++) Serial.print('*');
          Serial.println();
        } else {
          Serial.println("⚠️ Quá ký tự! Nhấn * để xóa.");
        }
        break;
    }
  }

  // ── RFID ──────────────────────────────────────────────────
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  String uid = getUID();
  Serial.println("🪪 UID: " + uid);

  if (NUM_CARDS == 0) {
    Serial.println("ℹ️  Chưa có thẻ đăng ký. Copy UID trên vào authorizedUID[]");
  } else if (isAuthorizedCard()) {
    unlockDoor("RFID");
  } else {
    wrongCard(uid);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}