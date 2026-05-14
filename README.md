# 🏠 IoT AI-Driven Smart Home - Nhóm 8 (K17)

Một hệ thống nhà thông minh được điều khiển bằng AI, kết hợp hardware ESP32, dịch vụ AI (STT/TTS), giao diện web và MQTT. Hệ thống cung cấp điều khiển toàn diện cho khóa cửa, cảm biến nhiệt độ/độ ẩm, quạt điều hòa, và hơn nữa.

**Các tính năng chính:**
- 🔐 **Khóa cửa thông minh** - Điều khiển qua keypad, RFID card, lệnh voice, hoặc web
- 🌡️ **Giám sát khí hậu** - Cảm biến DHT22 đo nhiệt độ & độ ẩm
- 🔊 **Điều khiển bằng giọng nói** - STT/TTS cho tương tác tự nhiên
- 📱 **Dashboard web** - Giao diện điều khiển thời gian thực
- 🔐 **Emergency Lockdown** - Chế độ khóa toàn bộ hệ thống
- 📡 **MQTT Integration** - Kết nối và tích hợp IoT

---

## 📋 Mục lục

1. [Tổng quan kiến trúc](#tổng-quan-kiến-trúc)
2. [Yêu cầu hệ thống](#yêu-cầu-hệ-thống)
3. [Cài đặt & Setup](#cài-đặt--setup)
4. [Cấu trúc thư mục](#cấu-trúc-thư-mục)
5. [Hướng dẫn sử dụng](#hướng-dẫn-sử-dụng)
6. [Pinout & Kết nối hardware](#pinout--kết-nối-hardware)
7. [API & MQTT Commands](#api--mqtt-commands)
8. [Khắc phục sự cố](#khắc-phục-sự-cố)
9. [Đóng góp](#đóng-góp)

---

## 🏗️ Tổng quan kiến trúc

```
┌─────────────────────────────────────┐
│   Web Dashboard (HTML/JS/CSS)       │
│   - Giao diện điều khiển           │
│   - Hiển thị cảm biến              │
└─────────────┬───────────────────────┘
              │
     ┌────────┴────────┐
     │                 │
┌────▼─────────┐  ┌────▼──────────┐
│  API Bridge  │  │  MQTT Broker  │
│   (Python)   │  │               │
└────┬─────────┘  └────┬──────────┘
     │                 │
     └────────┬────────┘
              │
     ┌────────▼────────┐
     │   ESP32 MCU     │
     │   (Arduino)     │
     │                 │
     │  ├─ Servo (Lock)│
     │  ├─ RFID Reader │
     │  ├─ Keypad      │
     │  ├─ DHT Sensors │
     │  └─ Relay       │
     └────────────────┘
```

---

## 📦 Yêu cầu hệ thống

### Hardware
- **ESP32 NodeMCU-32S** hoặc tương đương
- **Servo Motor** (5V) để khóa cửa
- **RFID Reader** (MFRC522)
- **Keypad** 4x4
- **Cảm biến DHT22** (x2 - 2 khu vực)
- **Relay Module** (2 channel) để điều khiển quạt
- **Dây cấp nguồn**, **Breadboard**, **Resistor**, **LED**

### Software
- **Python 3.8+**
- **PlatformIO** (hoặc Arduino IDE)
- **MQTT Broker** (Mosquitto hoặc tương đương)
- **Node.js** (tùy chọn, cho API bridge)

---

## 🚀 Cài đặt & Setup

### 1. Clone Repository
```bash
git clone https://github.com/Quang170305/-IoT_K17-_AI-DrivenSmartHome_Nhom8.git
cd -IoT_K17-_AI-DrivenSmartHome_Nhom8
```

### 2. Cài đặt Dependencies

#### Firmware ESP32 (PlatformIO)
```bash
# Cài đặt PlatformIO CLI
pip install platformio

# Upload firmware
platformio run --target upload -e nodemcu-32s
```

#### Python Backend
```bash
pip install -r requirements.txt
```

**Packages chính (nếu requirements.txt trống, cài đặt thủ công):**
```bash
pip install paho-mqtt google-cloud-speech google-cloud-texttospeech flask
```

### 3. Cấu hình MQTT Broker
```bash
# Trên Linux/macOS
mosquitto -c /etc/mosquitto/mosquitto.conf

# Trên Docker
docker run -d -p 1883:1883 -p 9001:9001 eclipse-mosquitto
```

### 4. Cấu hình WiFi & MQTT
Chỉnh sửa `src/main.cpp`:
```cpp
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* mqtt_server = "192.168.1.100";  // MQTT broker IP
```

### 5. Chạy AI Engine (Python)
```bash
cd ai_engine
python main_ai.py
```

### 6. Khởi động Dashboard
```bash
# Dùng Python HTTP server
cd web_dashboard
python -m http.server 8000

# Hoặc cấu hình với Node.js/Express
node server.js
```

Truy cập: `http://localhost:8000`

---

## 📁 Cấu trúc thư mục

```
-IoT_K17-_AI-DrivenSmartHome_Nhom8/
│
├── platformio.ini                 # Cấu hình PlatformIO
├── README.md                      # Tài liệu này
├── requirements.txt               # Python dependencies
│
├── src/
│   ├── main.cpp                   # Logic chính ESP32
│   └── mqtt_handler.h             # Handler MQTT
│
├── hardware/
│   ├── include/
│   │   └── config.h               # Cấu hình hardware pins
│   └── lib/                       # Thư viện Arduino
│
├── ai_engine/
│   ├── main_ai.py                 # Entry point AI service
│   ├── processor.py               # Xử lý commands & automation
│   ├── stt_service.py             # Speech-to-Text (Google Cloud)
│   ├── tts_service.py             # Text-to-Speech
│   └── voicecontrol.html          # Interface điều khiển bằng giọng nói
│
└── web_dashboard/
    ├── index.html                 # Giao diện chính
    ├── pinout.html                # Sơ đồ pinout (tham khảo)
    ├── style.css                  # Styling & animations
    ├── app.js                     # Logic frontend
    └── api_bridge.py              # API bridge (nếu cần)
```

---

## 📖 Hướng dẫn sử dụng

### Điều khiển khóa cửa

#### 1. **Bằng Keypad**
- Nhập mật khẩu (mặc định: `1234`)
- Nhấn `#` để xác nhận
- `*` để xóa input

#### 2. **Bằng RFID Card**
- Quẹt card được phép trước đầu đọc
- Cửa sẽ tự mở trong 10 giây rồi đóng lại

#### 3. **Bằng Web Dashboard**
- Truy cập dashboard
- Click nút "🚪 Mở/Đóng"
- Hoặc chuyên sâu: Vào tab "Cửa" để xem chi tiết

#### 4. **Bằng MQTT**
```bash
# Mở cửa
mosquitto_pub -t "home/cmd/lock" -m '{"action":"unlock","source":"admin"}'

# Đóng cửa
mosquitto_pub -t "home/cmd/lock" -m '{"action":"lock"}'

# Emergency lockdown (bật)
mosquitto_pub -t "home/cmd/lockdown" -m '{"active":true}'

# Emergency lockdown (tắt)
mosquitto_pub -t "home/cmd/lockdown" -m '{"active":false}'
```

### Giám sát nhiệt độ & độ ẩm

Dashboard sẽ hiển thị đọc từ 2 cảm biến DHT22 trên các khu vực khác nhau. Quạt sẽ tự động bật khi nhiệt độ vượt quá 35°C.

**Các ngưỡng (cấu hình trong `main.cpp`):**
- `TEMP_ON = 35.0°C` - Kích hoạt quạt
- `TEMP_OFF = 33.0°C` - Tắt quạt

### Điều khiển bằng giọng nói

1. Truy cập `voicecontrol.html` từ browser
2. Click "Bắt đầu ghi âm"
3. Nói lệnh: *"Mở cửa"*, *"Tắt quạt"*, *"Báo cáo tính năng"*
4. Hệ thống sẽ phản hồi bằng giọng nói

---

## 🔌 Pinout & Kết nối hardware

| Thiết bị | Pin ESP32 | Ghi chú |
|---------|-----------|--------|
| **Servo (Lock)** | GPIO 17 | 0° = Khóa, 90° = Mở |
| **RFID SS** | GPIO 5 | Chip Select |
| **RFID RST** | GPIO 22 | Reset |
| **DHT1** | GPIO 4 | Khu vực 1 |
| **DHT2** | GPIO 16 | Khu vực 2 |
| **Fan 1** | GPIO 15 | Relay control |
| **Fan 2** | GPIO 2 | Relay control |
| **Keypad Row 1** | GPIO 26 | |
| **Keypad Row 2** | GPIO 14 | |
| **Keypad Row 3** | GPIO 12 | |
| **Keypad Row 4** | GPIO 13 | |
| **Keypad Col 1** | GPIO 25 | |
| **Keypad Col 2** | GPIO 32 | |
| **Keypad Col 3** | GPIO 33 | |
| **Keypad Col 4** | GPIO 27 | |

Xem thêm: [web_dashboard/pinout.html](web_dashboard/pinout.html)

---

## 📡 API & MQTT Commands

### MQTT Topics

**Publish (Gửi lệnh):**
- `home/cmd/lock` - Điều khiển khóa
- `home/cmd/lockdown` - Chế độ khẩn cấp
- `home/cmd/fan/{1,2}` - Điều khiển quạt

**Subscribe (Nhận dữ liệu):**
- `home/status/lock` - Trạng thái khóa
- `home/status/temp` - Dữ liệu nhiệt độ
- `home/status/humidity` - Dữ liệu độ ẩm
- `home/status/fan/{1,2}` - Trạng thái quạt
- `home/status/lockdown` - Trạng thái Emergency

### Định dạng Payload

```json
{
  "lock": {
    "action": "unlock|lock",
    "source": "keypad|rfid|web|voice",
    "timestamp": 1713427200,
    "success": true
  },
  "temp": {
    "zone1": {"temp": 28.5, "humidity": 65},
    "zone2": {"temp": 27.2, "humidity": 62}
  },
  "fan": {
    "id": 1,
    "state": "on|off",
    "mode": "auto|manual"
  },
  "lockdown": {
    "active": false,
    "activated_at": null
  }
}
```

---

## 🔧 Khắc phục sự cố

### 1. ESP32 không kết nối WiFi
- Kiểm tra SSID & password trong `main.cpp`
- Reboot ESP32: Nút reset hoặc cắt điện 5 giây
- Kiểm tra signal WiFi: Đẩy ESP32 gần router

### 2. MQTT không nhận được messages
```bash
# Test kết nối MQTT
mosquitto_sub -h localhost -t "home/status/#"

# Kiểm tra IP MQTT broker
ping <mqtt-server-ip>
```

### 3. Cảm biến DHT không hoạt động
- Kiểm tra dây nối (VCC, GND, Data)
- Đảm bảo resistor pull-up 4.7kΩ ở chân Data
- Kiểm tra lỗi trong Serial Monitor

### 4. RFID không đọc card
- Sạch đầu đọc (alcohol swab)
- Kiểm tra card/fob có được phép không
- Kiểm tra dây nối SPI (MISO, MOSI, CLK, SS, RST)

### 5. Dashboard không tải
- Kiểm tra CORS headers nếu dùng API
- Browser console (F12) xem lỗi
- Kiểm tra API bridge đang chạy

---

## 📚 Công nghệ sử dụng

- **Hardware:** ESP32, MFRC522 RFID, DHT22 Sensors, SG90 Servo
- **Firmware:** Arduino, PlatformIO
- **Backend:** Python, Flask (nếu cần)
- **IoT:** MQTT (Mosquitto)
- **AI/Voice:** Google Cloud Speech-to-Text, Text-to-Speech
- **Frontend:** HTML5, CSS3, Vanilla JavaScript

---

## 👥 Thành viên nhóm

Nhóm 8 - K17

---

## 📝 License

Dự án này là học tập và nghiên cứu. Có thể sử dụng tự do.

---

## 🤝 Đóng góp

Nếu bạn tìm thấy lỗi hoặc có gợi ý:
1. Fork repository
2. Tạo branch: `git checkout -b feature/your-feature`
3. Commit: `git commit -am 'Add feature'`
4. Push: `git push origin feature/your-feature`
5. Open Pull Request

---

## 📞 Liên hệ & Hỗ trợ

- 📧 Email: (cập nhật)
- 🐛 Issues: GitHub Issues
- 💬 Discussion: GitHub Discussions

---

**Cập nhật lần cuối:** April 18, 2026

