# 📦 PAKET PINTAR - AI Delivery Assistant

Sistem IoT otomatis untuk paket delivery dengan AI conversational, voice recognition, dan robotics.

## 🏗️ Arsitektur Sistem

```
┌─────────────────────┐
│   Web Dashboard     │  (GitHub Pages - Monitoring)
│  HTML/CSS/JS + MQTT │
└──────────┬──────────┘
           │ MQTT WebSocket
┌─────────────────────┐      ┌────────────────────────┐
│  PC Server (Flask)  │◄────►│  ESP32 Microcontroller │
│  - STT              │ HTTP │  - Microphone (I2S)    │
│  - Gemini AI        │ TCP  │  - Speaker (I2S)       │
│  - TTS              │ MQTT │  - Servo Motor         │
│  - Audio Processing │      │  - OLED Display        │
└─────────────────────┘      │  - Button              │
                             └────────────────────────┘
```

## 🔧 Setup & Installation

### 1. **Hardware Setup**
Ikuti panduan wiring di file `wiring.md`:
- INMP441 Microphone → GPIO 35, 33, 32
- MAX98357A Speaker → GPIO 27, 26, 25
- SSD1306 OLED → GPIO 21, 22 (I2C)
- Servo SG90 → GPIO 19
- Power: 5V ≥2A untuk servo & amplifier

### 2. **ESP32 Firmware**
- Install Arduino IDE + ESP32 boards
- Install libraries: WiFi, PubSubClient, Adafruit_SSD1306, ESP32Servo, ArduinoJson
- Upload file `sketch.ino` ke ESP32
- Ganti WiFi SSID & password di kode

### 3. **PC Server (Python)**
```bash
pip install -r requirements.txt
export GEMINI_API_KEY="your-api-key"
python main.py
```
Server akan berjalan di `http://localhost:5000`

### 4. **Web Dashboard**
- Deploy ke GitHub Pages atau jalankan locally
- Update MQTT broker jika berbeda dari default (hivemq.com)

## 📡 MQTT Topics

| Topic | Direction | Payload |
|-------|-----------|---------|
| `package/command` | PC → ESP32 | JSON command |
| `package/status` | ESP32 → PC | Status perangkat |
| `package/response` | PC → Web | Respon terbaru |

## 🎤 Alur Kerja Sistem

1. **Button Pressed** → ESP32 menangkap audio mic
2. **Streaming** → Audio dikirim ke PC via TCP
3. **STT** → PC mengkonversi ke text (Google Speech Recognition)
4. **AI Processing** → Gemini AI memproses perintah
5. **TTS** → Text dikonversi ke audio (Google TTS)
6. **Playback** → Audio diputar via speaker ESP32
7. **Action** → Servo buka kotak jika paket cocok
8. **Monitoring** → Status real-time di web dashboard

## 🌐 Web Dashboard Features

- **Real-time Status**: Listening, Thinking, Speaking, Sleep
- **Package Info**: Nama penerima, waktu delivery
- **Activity Log**: Timestamp semua event
- **Responsive Design**: Mobile & desktop friendly
- **MQTT WebSocket**: Live update tanpa polling

## 🔐 Security Notes

- Gunakan MQTT dengan auth (ganti broker jika perlu private)
- Set environment variables untuk API keys (.env)
- Jangan hardcode credentials di kode production
- Validasi input dari user untuk mencegah injection

## 🐛 Troubleshooting

| Error | Solusi |
|-------|--------|
| WiFi tidak connect | Cek SSID/password, pastikan 2.4GHz |
| MQTT tidak publish | Cek broker status, topic permissions |
| Audio quality jelek | Reduce AUDIO_DURATION_MS atau pakai mic bagus |
| Servo jitter | Tambah kapasitor decoupling 470µF |
| STT tidak jalan | Check internet, enable Google Speech API |

## 📚 File Structure

```
project/
├── esp32-firmware/
│   └── sketch.ino          (ESP32 code)
├── pc-server/
│   ├── main.py             (Flask server)
│   └── requirements.txt     (Dependencies)
├── web-dashboard/
│   ├── index.html          (Main page)
│   ├── style.css           (Styling)
│   └── main.js             (Logic)
├── wiring.md               (Hardware connections)
└── DOKUMENTASI.md          (This file)
```

## 🚀 Next Steps

1. Test hardware wiring terlebih dahulu
2. Upload firmware ke ESP32
3. Jalankan PC server & test STT/TTS
4. Deploy web dashboard ke GitHub Pages
5. Monitor system via dashboard
6. Integrate dengan database whitelist names

---

**Dibuat dengan ❤️ untuk delivery otomatis yang cerdas**
