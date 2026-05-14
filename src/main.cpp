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
#define DHT_PIN_1     4
#define DHT_PIN_2     16
#define DHT_TYPE      DHT22

#define FAN_PIN_1     15
#define FAN_PIN_2     2

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

#define TEMP_ON       35.0
#define TEMP_OFF      33.0
#define DHT_INTERVAL  5000

#define AUTO_LOCK_SEC  10

// ============================================================
//  ĐỐI TƯỢNG
// ============================================================
Servo   doorServo;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
DHT     dht1(DHT_PIN_1, DHT_TYPE);
DHT     dht2(DHT_PIN_2, DHT_TYPE);

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'D','#','0','*'},
  {'C','9','8','7'},
  {'B','6','5','4'},
  {'A','3','2','1'}
};
byte rowPins[ROWS] = {26, 14, 12, 13};
byte colPins[COLS] = {25, 32, 33, 27};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

byte authorizedUID[][4] = {
  {0xA6, 0x3F, 0xDF, 0x00}
};
const int NUM_CARDS = sizeof(authorizedUID) / sizeof(authorizedUID[0]);

// ============================================================
//  BIẾN TRẠNG THÁI
// ============================================================
String        inputBuffer = "";
bool          doorOpen    = false;
unsigned long openTime    = 0;

float         temp1 = 0, humi1 = 0;
float         temp2 = 0, humi2 = 0;
bool          fan1On = false;
bool          fan2On = false;
unsigned long lastDHTRead = 0;

bool          fan1Manual = false;
bool          fan2Manual = false;

// ============================================================
//  🔴 EMERGENCY LOCKDOWN
//  Khi emergencyLock = true:
//    - Keypad KHÔNG thể mở cửa
//    - RFID KHÔNG thể mở cửa
//    - Lệnh MQTT unlock BỊ TỪ CHỐI
//    - Chỉ có thể tắt qua MQTT: home/cmd/lockdown {"active":false}
// ============================================================
bool emergencyLock = false;

void setEmergencyLock(bool active) {
  emergencyLock = active;
  if (active) {
    // Đảm bảo cửa đóng khi bật lockdown
    doorServo.write(SERVO_LOCK);
    doorOpen = false;
    Serial.println("🚨 EMERGENCY LOCKDOWN BẬT — Mọi phương thức mở khóa bị chặn!");
  } else {
    Serial.println("✅ EMERGENCY LOCKDOWN TẮT — Hệ thống hoạt động bình thường.");
  }
  if (mqttClient.connected()) {
    publishLockdownStatus();
    publishStatus();
  }
}

// ============================================================
//  ĐIỀU KHIỂN CỬA
// ============================================================
void lockDoor() {
  doorServo.write(SERVO_LOCK);
  doorOpen = false;
  Serial.println("🔒 CỬA ĐÃ KHÓA");
  Serial.println("────────────────────────────────");
  if (!emergencyLock) {
    Serial.println("Nhập mật khẩu + #  hoặc  quẹt thẻ:");
  } else {
    Serial.println("⛔ Chế độ khóa khẩn — mọi mở khóa bị chặn!");
  }
}

void unlockDoor(String method) {
  // ── Kiểm tra Emergency Lockdown TRƯỚC ──
  if (emergencyLock) {
    Serial.println("⛔ [" + method + "] BỊ CHẶN — Emergency Lockdown đang bật!");
    if (mqttClient.connected()) {
      publishEvent("lockdown", ("blocked:" + method).c_str(), false);
    }
    // Rung servo báo hiệu bị chặn (3 lần)
    for (int i = 0; i < 3; i++) {
      doorServo.write(SERVO_LOCK + 10); delay(100);
      doorServo.write(SERVO_LOCK);      delay(100);
    }
    return;
  }

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
void setFan1(bool on, bool manual = false) {
  fan1Manual = manual;
  if (fan1On == on) return;
  fan1On = on;
  digitalWrite(FAN_PIN_1, on ? RELAY_ON : RELAY_OFF);
  Serial.printf("🌀 Quạt P1: %s (%.1f°C) [%s]\n",
    on ? "BẬT" : "TẮT", temp1, manual ? "MANUAL" : "AUTO");
  if (mqttClient.connected()) publishTempFan(1, temp1, humi1, fan1On);
}

void setFan2(bool on, bool manual = false) {
  fan2Manual = manual;
  if (fan2On == on) return;
  fan2On = on;
  digitalWrite(FAN_PIN_2, on ? RELAY_ON : RELAY_OFF);
  Serial.printf("🌀 Quạt P2: %s (%.1f°C) [%s]\n",
    on ? "BẬT" : "TẮT", temp2, manual ? "MANUAL" : "AUTO");
  if (mqttClient.connected()) publishTempFan(2, temp2, humi2, fan2On);
}

// ============================================================
//  ĐỌC DHT22
// ============================================================
void readDHT() {
  float t1 = dht1.readTemperature();
  float h1 = dht1.readHumidity();
  if (!isnan(t1) && !isnan(h1)) {
    temp1 = t1; humi1 = h1;
    Serial.printf("🌡️  P1: %.1f°C  💧%.1f%%\n", temp1, humi1);
    if (!fan1Manual) {
      if (temp1 >= TEMP_ON)  setFan1(true,  false);
      if (temp1 <= TEMP_OFF) setFan1(false, false);
    } else {
      if (temp1 >= TEMP_ON && !fan1On) {
        Serial.println("⚠️  P1 quá nóng! Bật quạt bảo vệ.");
        setFan1(true, true);
      }
    }
  } else {
    Serial.println("⚠️  DHT22 Phòng 1 đọc lỗi!");
  }

  float t2 = dht2.readTemperature();
  float h2 = dht2.readHumidity();
  if (!isnan(t2) && !isnan(h2)) {
    temp2 = t2; humi2 = h2;
    Serial.printf("🌡️  P2: %.1f°C  💧%.1f%%\n", temp2, humi2);
    if (!fan2Manual) {
      if (temp2 >= TEMP_ON)  setFan2(true,  false);
      if (temp2 <= TEMP_OFF) setFan2(false, false);
    } else {
      if (temp2 >= TEMP_ON && !fan2On) {
        Serial.println("⚠️  P2 quá nóng! Bật quạt bảo vệ.");
        setFan2(true, true);
      }
    }
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

  pinMode(FAN_PIN_1, OUTPUT);
  pinMode(FAN_PIN_2, OUTPUT);
  digitalWrite(FAN_PIN_1, RELAY_OFF);
  digitalWrite(FAN_PIN_2, RELAY_OFF);

  Serial.println("🔧 Test relay...");
  delay(500);
  digitalWrite(FAN_PIN_1, RELAY_ON);  Serial.println("  Relay 1 BẬT"); delay(600);
  digitalWrite(FAN_PIN_1, RELAY_OFF); Serial.println("  Relay 1 TẮT"); delay(400);
  digitalWrite(FAN_PIN_2, RELAY_ON);  Serial.println("  Relay 2 BẬT"); delay(600);
  digitalWrite(FAN_PIN_2, RELAY_OFF); Serial.println("  Relay 2 TẮT"); delay(400);
  Serial.println("✓ Test relay xong");

  dht1.begin();
  dht2.begin();
  Serial.println("✓ DHT22 ×2 khởi động");

  SPI.begin(18, 19, 23, RFID_SS_PIN);
  rfid.PCD_Init();
  Serial.printf("✓ RFID RC522 v%X\n", rfid.PCD_ReadRegister(rfid.VersionReg));

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN, 500, 2400);
  lockDoor();
  delay(300);

  mqttSetup();

  Serial.println("\n================================================");
  Serial.println("   SMART HOME sẵn sàng!");
  Serial.printf ("   Mật khẩu : %s\n", currentPassword.c_str());
  Serial.printf ("   Số thẻ   : %d\n", NUM_CARDS);
  Serial.printf ("   Bật quạt : >= %.0f°C | Tắt: <= %.0f°C\n", TEMP_ON, TEMP_OFF);
  Serial.println("   🔴 EMERGENCY LOCKDOWN:");
  Serial.println("   ← home/cmd/lockdown : {\"active\":true|false}");
  Serial.println("   → home/lockdown     : {\"active\":bool}");
  Serial.println("================================================\n");

  delay(2000);
  readDHT();
  lastDHTRead = millis();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {

  mqttLoop();

  if (millis() - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = millis();
    readDHT();
  }

#if AUTO_LOCK_SEC > 0
  if (doorOpen && !emergencyLock &&
      (millis() - openTime >= (unsigned long)AUTO_LOCK_SEC * 1000)) {
    Serial.println("⏱️  Tự động khóa cửa!");
    lockDoor();
    if (mqttClient.connected()) {
      publishEvent("door", "auto_locked", true);
      publishStatus();
    }
  }
#endif

  // ── KEYPAD ────────────────────────────────────────────────
  char key = keypad.getKey();
  if (key) {
    switch (key) {

      case '#':
        // Hiển thị trạng thái lockdown nếu bị chặn
        if (emergencyLock) {
          Serial.println("⛔ KHÓA KHẨN CẤP — Keypad bị vô hiệu hóa!");
          for (int i = 0; i < 3; i++) {
            doorServo.write(SERVO_LOCK + 10); delay(100);
            doorServo.write(SERVO_LOCK);      delay(100);
          }
          inputBuffer = "";
          break;
        }
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
          Serial.println(key);
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

  // Kiểm tra Emergency Lockdown cho RFID
  if (emergencyLock) {
    Serial.println("⛔ RFID BỊ CHẶN — Emergency Lockdown đang bật!");
    if (mqttClient.connected()) {
      publishEvent("lockdown", ("rfid_blocked:" + uid).c_str(), false);
    }
    for (int i = 0; i < 3; i++) {
      doorServo.write(SERVO_LOCK + 10); delay(100);
      doorServo.write(SERVO_LOCK);      delay(100);
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

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