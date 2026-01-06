# 📦 PAKET PINTAR – AI Delivery Assistant

Sistem IoT otomatis untuk paket delivery dengan AI conversational, voice recognition, dan robotics. Solusi cerdas untuk delivery yang aman dan efisien.

## 🌟 Features

✅ **AI Conversational** - Interaksi natural dengan Gemini AI  
✅ **Speech Recognition** - STT real-time (Google Speech Recognition)  
✅ **Text-to-Speech** - TTS natural sounding (gTTS)  
✅ **Servo Automation** - Buka kotak paket otomatis  
✅ **Real-time Monitoring** - Dashboard web live dengan MQTT WebSocket  
✅ **Hardware Integration** - Microphone, Speaker, OLED Display, Servo Motor  
✅ **Dark Mode UI** - Modern, futuristic dashboard design  

## 📊 Sistem Architecture

```
Web Dashboard (HTML/CSS/JS)
        ↕ MQTT WebSocket
PC Server (Flask + Gemini AI)
        ↕ HTTP TCP + MQTT
ESP32 Microcontroller (Firmware)
        ↕ I2S Audio + GPIO
Hardware (Mic, Speaker, Servo, OLED)
```

## 🚀 Quick Start

### 1. Hardware Setup
Lihat file `wiring.md` untuk petunjuk koneksi hardware:
- INMP441 Microphone
- MAX98357A Speaker Amplifier
- SSD1306 OLED Display (I2C)
- SG90 Servo Motor
- ESP32 DevKit v1

### 2. ESP32 Firmware
```bash
# Install Arduino IDE & ESP32 board support
# Libraries: PubSubClient, Adafruit_SSD1306, ESP32Servo, ArduinoJson

# Update config di sketch.ino:
const char* ssid = "Your-WiFi-SSID";
const char* password = "Your-WiFi-Password";
const char* pc_ip = "192.168.1.100";  // Laptop IP

# Upload ke ESP32
```

### 3. PC Server
```bash
cd pc-server
pip install -r requirements.txt
export GEMINI_API_KEY="your-gemini-api-key"
python main.py
# Server akan berjalan di http://localhost:5000
```

### 4. Web Dashboard
```bash
cd web-dashboard
# Opsi A: Jalankan dengan Python
python -m http.server 8000
# Opsi B: Deploy ke GitHub Pages

# Buka di browser: http://localhost:8000
```

## 📋 File Structure

```
paket-pintar/
├── esp32-firmware/
│   └── sketch.ino                    # ESP32 Arduino code
├── pc-server/
│   ├── main.py                       # Flask server + AI
│   └── requirements.txt              # Python dependencies
├── web-dashboard/
│   ├── index.html                    # Main page
│   ├── style.css                     # Styling (dark mode)
│   └── main.js                       # MQTT + interactions
├── wiring.md                         # Hardware connections
├── DOKUMENTASI.md                    # Full documentation
└── README.md                         # This file
```

## 🔧 Configuration

### ESP32 Config (sketch.ino)
```cpp
const char* ssid = "Your-WiFi";
const char* password = "Your-Password";
const char* mqtt_server = "broker.hivemq.com";
const char* pc_ip = "192.168.1.100";
const int pc_port = 5000;
```

### PC Server Config (main.py)
```python
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MODEL_NAME = "gemini-1.5-flash"
```

### Web Dashboard Config (main.js)
```javascript
const MQTT_BROKER_URL = 'wss://test.mosquitto.org:8081';
const MQTT_TOPIC_STATUS = 'package/status';
```

## 🎯 How It Works

1. **Button Press** → ESP32 records audio from microphone
2. **Audio Stream** → Send to PC via TCP connection
3. **Speech-to-Text** → Convert audio to text using Google STT
4. **AI Processing** → Process with Gemini AI
5. **Text-to-Speech** → Convert response to audio
6. **Playback** → Play audio through speaker
7. **Action** → Servo opens package if recipient matches
8. **Monitoring** → Dashboard updates in real-time via MQTT

## 📱 Dashboard Features

- **AI Status Monitor** - Real-time status (Listening, Thinking, Speaking, Sleep)
- **Package Info** - Recipient name, delivery time, status
- **Activity Log** - Timestamped event log
- **Responsive Design** - Works on mobile & desktop
- **Dark Mode** - Modern, eye-friendly interface
- **Live Updates** - MQTT WebSocket for real-time sync

## 🔐 Security

- MQTT broker: HIVEmq (public) - upgrade to private for production
- Environment variables for API keys (.env file)
- No hardcoded credentials
- Input validation on server side
- CORS enabled for cross-origin requests

## 🐛 Troubleshooting

| Issue | Solution |
|-------|----------|
| WiFi connection fails | Check SSID/password, ensure 2.4GHz network |
| MQTT not publishing | Verify broker URL, check topic names |
| Audio quality poor | Reduce AUDIO_DURATION_MS, use quality mic |
| Servo jitter | Add 470µF capacitor decoupling |
| STT not working | Check Google Speech Recognition API, internet connection |
| Dashboard not updating | Check MQTT WebSocket URL, broker status |

## 🎨 Design

**Color Scheme:**
- Primary: Deep Blue (#0066ff)
- Accent: Cyan (#00d9ff)
- Background: Dark (#0f172a)
- Success: Green (#10b981)
- Error: Red (#ef4444)

**Typography:**
- System fonts for fast loading
- Monospace for timestamps
- 1.5rem line-height for readability

## 📚 Dependencies

### ESP32
- Arduino IDE 1.8.x+
- ESP32 Board Support 2.x+
- PubSubClient (MQTT)
- Adafruit libraries
- ArduinoJson

### PC Server
- Python 3.8+
- Flask 2.0+
- google-generativeai
- paho-mqtt
- SpeechRecognition
- gTTS, pydub

### Web Dashboard
- Modern browser (Chrome, Firefox, Safari)
- Paho MQTT JavaScript client
- No external frameworks (vanilla JS)

## 🚀 Next Steps

1. Test hardware connections
2. Upload firmware to ESP32
3. Run PC server & test locally
4. Deploy dashboard to GitHub Pages
5. Integrate with delivery database
6. Add authentication & security
7. Scale to multiple devices

## 📖 Documentation

- **Full Documentation**: See `DOKUMENTASI.md`
- **Hardware Setup**: See `wiring.md`
- **API Reference**: See comments in `main.py`
- **Firmware Details**: See comments in `sketch.ino`

## 🤝 Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 License

MIT License - feel free to use for commercial projects

## 🙋 Support

For issues, questions, or suggestions:
- Open an issue on GitHub
- Check the Troubleshooting section
- Review the full documentation

---

**Dibuat dengan ❤️ untuk memudahkan delivery paket yang cerdas dan aman.**

💡 *Smart Package. Smart Delivery. Smart Future.*
