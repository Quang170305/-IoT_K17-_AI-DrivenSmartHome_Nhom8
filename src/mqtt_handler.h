#pragma once
// ============================================================
//  mqtt_handler.h — SMART HOME
//  WiFi + MQTT + DHT22 ×2 + Quạt DC + Emergency Lockdown
// ============================================================
//  Subscribe (Web → ESP32):
//    home/cmd/door      → {"action":"unlock"|"lock"}
//    home/cmd/password  → {"new":"5678"}
//    home/cmd/fan       → {"room":1,"action":"on"|"off"}
//    home/cmd/lockdown  → {"active":true|false}   ← MỚI
//
//  Publish (ESP32 → Web):
//    home/door/event    → {type, detail, granted, time}
//    home/temp/room1    → {temp, humi, fan, time}
//    home/temp/room2    → {temp, humi, fan, time}
//    home/status        → {door, fan1, fan2, lockdown, uptime, ip}
//    home/online        → "1"
//    home/lockdown      → {"active":true|false}   ← MỚI
// ============================================================

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID   "Quang1703"
#define WIFI_PASS   "passkhongco"
#define MQTT_SERVER "67c33f1aca2d4e9aad823a04f6ea8563.s1.eu.hivemq.cloud"
#define MQTT_PORT   8883
#define MQTT_CLIENT "SmartHome_ESP32"
const char* mqtt_user = "Quang1703";
const char* mqtt_pass = "Passkhongco1";

// ── Topics ──────────────────────────────────────────────────
#define T_CMD_DOOR      "home/cmd/door"
#define T_CMD_PASS      "home/cmd/password"
#define T_CMD_FAN       "home/cmd/fan"
#define T_CMD_LOCKDOWN  "home/cmd/lockdown"   // ← MỚI
#define T_DOOR_EVT      "home/door/event"
#define T_TEMP_1        "home/temp/room1"
#define T_TEMP_2        "home/temp/room2"
#define T_STATUS        "home/status"
#define T_ONLINE        "home/online"
#define T_LOCKDOWN      "home/lockdown"        // ← MỚI

// ── Extern từ main.cpp ──────────────────────────────────────
extern bool    doorOpen;
extern String  currentPassword;
extern float   temp1, humi1, temp2, humi2;
extern bool    fan1On, fan2On;
extern bool    fan1Manual, fan2Manual;
extern bool    emergencyLock;                  // ← MỚI
extern void    unlockDoor(String method);
extern void    lockDoor();
extern void    setFan1(bool on, bool manual);
extern void    setFan2(bool on, bool manual);
extern void    setEmergencyLock(bool active);  // ← MỚI

// ── Objects ─────────────────────────────────────────────────
WiFiClientSecure wifiClient;
PubSubClient     mqttClient(wifiClient);

static unsigned long _lastReconnect = 0;
static unsigned long _lastStatus    = 0;

// ============================================================
//  PUBLISH
// ============================================================
void publishEvent(const char* type, const char* detail, bool granted) {
  StaticJsonDocument<160> doc;
  doc["type"]    = type;
  doc["detail"]  = detail;
  doc["granted"] = granted;
  doc["time"]    = millis() / 1000;
  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(T_DOOR_EVT, buf);
  Serial.printf("[MQTT] ↑ event: %s — %s\n", type, detail);
}

void publishTempFan(int room, float temp, float humi, bool fanOn) {
  StaticJsonDocument<128> doc;
  doc["temp"] = serialized(String(temp, 1));
  doc["humi"] = serialized(String(humi, 1));
  doc["fan"]  = fanOn ? "on" : "off";
  doc["time"] = millis() / 1000;
  char buf[200];
  serializeJson(doc, buf);
  const char* topic = (room == 1) ? T_TEMP_1 : T_TEMP_2;
  mqttClient.publish(topic, buf, true);
  Serial.printf("[MQTT] ↑ room%d: %.1f°C %.1f%% fan=%s\n",
                room, temp, humi, fanOn ? "ON" : "OFF");
}

// ── MỚI: Publish trạng thái lockdown riêng ─────────────────
void publishLockdownStatus() {
  StaticJsonDocument<64> doc;
  doc["active"] = emergencyLock;
  doc["time"]   = millis() / 1000;
  char buf[128];
  serializeJson(doc, buf);
  mqttClient.publish(T_LOCKDOWN, buf, true);  // retain=true để web biết ngay khi kết nối
  Serial.printf("[MQTT] ↑ lockdown: %s\n", emergencyLock ? "ACTIVE" : "INACTIVE");
}

void publishStatus() {
  StaticJsonDocument<256> doc;
  doc["door"]      = doorOpen      ? "open"   : "locked";
  doc["fan1"]      = fan1On        ? "on"     : "off";
  doc["fan2"]      = fan2On        ? "on"     : "off";
  doc["fan1Mode"]  = fan1Manual    ? "manual" : "auto";
  doc["fan2Mode"]  = fan2Manual    ? "manual" : "auto";
  doc["lockdown"]  = emergencyLock ? true     : false;   // ← MỚI
  doc["temp1"]     = serialized(String(temp1, 1));
  doc["temp2"]     = serialized(String(temp2, 1));
  doc["uptime"]    = millis() / 1000;
  doc["ip"]        = WiFi.localIP().toString();
  char buf[320];
  serializeJson(doc, buf);
  mqttClient.publish(T_STATUS, buf, true);
}

// ============================================================
//  CALLBACK — nhận lệnh từ Web
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  payload[len] = '\0';
  String msg = String((char*)payload);
  Serial.printf("[MQTT] ← %s : %s\n", topic, msg.c_str());

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) {
    Serial.println("[MQTT] ⚠ JSON lỗi"); return;
  }

  // ── home/cmd/lockdown ── (ƯU TIÊN XỬ LÝ ĐẦU TIÊN)
  if (strcmp(topic, T_CMD_LOCKDOWN) == 0) {
    bool active = doc["active"] | false;
    setEmergencyLock(active);
    // publishLockdownStatus() và publishStatus() đã được gọi trong setEmergencyLock()
    return;
  }

  // ── home/cmd/door ──
  if (strcmp(topic, T_CMD_DOOR) == 0) {
    const char* action = doc["action"] | "";
    if (strcmp(action, "unlock") == 0) {
      // unlockDoor() tự kiểm tra emergencyLock bên trong
      unlockDoor("WEB REMOTE");
    } else if (strcmp(action, "lock") == 0) {
      lockDoor();
      publishEvent("remote", "lock_command", true);
      publishStatus();
    }
  }

  // ── home/cmd/password ──
  else if (strcmp(topic, T_CMD_PASS) == 0) {
    const char* np = doc["new"] | "";
    int pl = strlen(np);
    if (pl >= 4 && pl <= 10) {
      currentPassword = String(np);
      Serial.println("🔑 Mật khẩu mới: " + currentPassword);
      publishEvent("system", "password_changed", true);
    } else {
      publishEvent("system", "password_invalid", false);
    }
  }

  // ── home/cmd/fan ──
  else if (strcmp(topic, T_CMD_FAN) == 0) {
    int room        = doc["room"] | 0;
    const char* act = doc["action"] | "";

    if (strcmp(act, "auto") == 0) {
      if (room == 1) { fan1Manual = false; Serial.println("[MQTT] Quạt P1 → AUTO"); }
      if (room == 2) { fan2Manual = false; Serial.println("[MQTT] Quạt P2 → AUTO"); }
    } else {
      bool turnOn = (strcmp(act, "on") == 0);
      if (room == 1) {
        setFan1(turnOn, true);
        Serial.printf("[MQTT] Quạt P1 → %s [MANUAL]\n", turnOn ? "BẬT" : "TẮT");
      } else if (room == 2) {
        setFan2(turnOn, true);
        Serial.printf("[MQTT] Quạt P2 → %s [MANUAL]\n", turnOn ? "BẬT" : "TẮT");
      }
    }
    publishStatus();
  }
}

// ============================================================
//  CONNECT
// ============================================================
void wifiConnect() {
  Serial.printf("📶 WiFi → %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 24) {
    delay(500); Serial.print("."); tries++;
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n✓ IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\n⚠ WiFi thất bại — chạy offline");
}

bool mqttConnect() {
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("📡 MQTT → %s:%d ... ", MQTT_SERVER, MQTT_PORT);
  if (mqttClient.connect(MQTT_CLIENT, mqtt_user, mqtt_pass,
                         T_ONLINE, 0, true, "0")) {
    Serial.println("✓ OK");
    mqttClient.subscribe(T_CMD_DOOR,     1);
    mqttClient.subscribe(T_CMD_PASS,     1);
    mqttClient.subscribe(T_CMD_FAN,      1);
    mqttClient.subscribe(T_CMD_LOCKDOWN, 1);   // ← MỚI
    mqttClient.publish(T_ONLINE, "1", true);
    publishLockdownStatus();   // ← Gửi trạng thái lockdown hiện tại ngay khi kết nối
    publishStatus();
    return true;
  }
  Serial.printf("✗ rc=%d\n", mqttClient.state());
  return false;
}

// ============================================================
//  SETUP & LOOP
// ============================================================
void mqttSetup() {
  wifiConnect();
  wifiClient.setInsecure();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttConnect();
}

void mqttLoop() {
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnect > 5000) {
      _lastReconnect = now;
      Serial.println("[MQTT] Reconnecting...");
      mqttConnect();
    }
  }
  mqttClient.loop();

  if (millis() - _lastStatus > 15000) {
    _lastStatus = millis();
    if (mqttClient.connected()) publishStatus();
  }
}