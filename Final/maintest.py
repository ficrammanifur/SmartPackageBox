from flask import Flask, request, jsonify
from flask_cors import CORS
import google.generativeai as genai
import os
from dotenv import load_dotenv
import paho.mqtt.client as mqtt
import json

load_dotenv()
app = Flask(__name__)
CORS(app)

# ================= MQTT SETUP (untuk balas ke ESP32) =================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_SUB_TOPIC = "package/text"  # ESP32 kirim text ke sini
MQTT_PUB_TOPIC = "package/response"  # Laptop balas JSON ke ESP32
MQTT_STATUS_TOPIC = "package/status"  # Subscribe ke status ESP32 (boot, opened, dll)

mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

def send_response_to_esp32(response_json):
    mqtt_client.publish(MQTT_PUB_TOPIC, json.dumps(response_json))
    print(f"📡 Response ke ESP32: {response_json}")

# Subscribe untuk nerima text dari ESP32 DAN status
def on_mqtt_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode().strip()
    
    if topic == MQTT_STATUS_TOPIC:
        print(f"📟 Status ESP32: {payload}")
        if payload == "boot_ready":
            print("🔊 TTS: Alat aktif")
            os.system('espeak "Permisi. Smart Package Box aktif dan siap digunakan."')
        return
    
    try:
        # Kalau text dari ESP32
        user_text = payload
        print(f"👤 Text dari ESP32: {user_text}")
        decision = extract_name_with_gemini(user_text)
        send_response_to_esp32(decision)
    except Exception as e:
        print(f"[MQTT Error]: {e}")
        send_response_to_esp32({"action": "error", "message": "Server error"})

mqtt_client.on_message = on_mqtt_message
mqtt_client.subscribe(MQTT_SUB_TOPIC)
mqtt_client.subscribe(MQTT_STATUS_TOPIC)  # Tambah subscribe status

# ================= GEMINI AI SETUP (dengan prompt baru) =================
genai.configure(api_key=os.getenv('GEMINI_API_KEY'))
MODEL_NAME = "gemini-1.5-flash"
model = genai.GenerativeModel(
    MODEL_NAME,
    system_instruction="""Kamu adalah sebuah AI kurir paket pintar. Kamu TIDAK memiliki nama dan TIDAK pernah menyebutkan identitas pribadi.
Peran utama kamu:
* Bertindak seperti kurir paket yang cerdas, sopan, dan efisien
* Berkomunikasi secara natural dalam Bahasa Indonesia
* Menjawab singkat, jelas, dan kontekstual (maksimal 2–3 kalimat)

KONDISI SISTEM:
* Kamu terhubung dengan sebuah kotak paket pintar (ESP32 + OLED)
* Status sistem akan ditampilkan di OLED sebagai: 
  - "Mendengarkan" → saat user berbicara 
  - "Berpikir" → saat kamu memproses jawaban 
  - "Menjawab" → saat kamu berbicara

ATURAN PERILAKU WAJIB:
1. Percakapan SELALU dimulai dengan:    "Permisi, ada paket."
2. Setelah itu kamu WAJIB menanyakan:    "Dengan siapa saya berbicara?"
3. HANYA ada 3 nama valid untuk membuka paket:    - aisyah    - rabiathul    - nadia
4. Jika user menyebutkan SALAH SATU dari nama valid:    - Akui nama tersebut    - Konfirmasi paket    - Gunakan nada profesional kurir    - Contoh respons:      "Baik, paket atas nama Aisyah. Silakan diambil."
5. Jika nama TIDAK valid:    - Jangan membuka paket    - Jangan menyebut kata 'akses ditolak'    - Jawab sopan dan netral    - Contoh:      "Maaf, nama tersebut tidak terdaftar pada paket ini."
6. Setelah paket berhasil diambil:    - Katakan satu kalimat penutup    - Masuk ke MODE TIDUR (sleep mode)    - Jangan berbicara lagi sampai wake word diterima

MODE TIDUR:
* Saat sleep mode, kamu TIDAK berbicara dan TIDAK merespons percakapan normal
* Satu-satunya kalimat untuk membangunkan kamu adalah:   "hallo"

SETELAH WAKE WORD "hallo":
* Kembali ke kondisi awal
* Ulangi lagi dari:   "Permisi, ada paket."

LARANGAN KERAS:
* Jangan bercanda
* Jangan menjawab di luar konteks kurir paket
* Jangan menjelaskan sistem, AI, atau teknologi
* Jangan menyebut kata Gemini, API, ESP32, atau OLED
* Jangan berbicara panjang

Tugasmu: dari ucapan pengguna, tentukan action berdasarkan aturan di atas. Kembalikan JSON valid:
- Jika nama valid: {"action": "open", "name": "NamaAsli", "tts": "Teks bicara lengkap"}
- Jika nama tidak valid: {"action": "deny", "message": "Pesan sopan", "tts": "Teks bicara lengkap"}
- Jika perlu tanya nama atau mulai: {"action": "ask_name", "message": "Pertanyaan", "tts": "Teks bicara lengkap"}
- Jika sleep: {"action": "sleep", "tts": "Penutup singkat"}
- Jika wake "hallo": {"action": "wake", "tts": "Permisi, ada paket."}

Hanya kembalikan JSON valid. Jangan tambahkan teks lain."""
)

WHITELIST = ["aisyah", "rabiathul", "nadia"]
SLEEP_MODE = False  # Global flag untuk mode sleep

def extract_name_with_gemini(text):
    global SLEEP_MODE
    try:
        if SLEEP_MODE and "hallo" not in text.lower():
            return {"action": "sleep", "tts": "Saya sedang istirahat. Katakan 'hallo' untuk bangun."}
        
        if "hallo" in text.lower():
            SLEEP_MODE = False
            return {"action": "wake", "tts": "Permisi, ada paket. Dengan siapa saya berbicara?"}
        
        response = model.generate_content(text)
        result = json.loads(response.text.strip())
        
        # Update sleep flag jika action sleep
        if result.get("action") == "sleep":
            SLEEP_MODE = True
        
        # Tambah TTS ke response jika ada
        if "tts" not in result:
            result["tts"] = result.get("message", "Respons default")
        
        return result
    except Exception as e:
        print(f"[Gemini Error] Fallback: {e}")
        text_lower = text.lower()
        if SLEEP_MODE and "hallo" not in text_lower:
            return {"action": "sleep", "tts": "Saya sedang istirahat."}
        if "hallo" in text_lower:
            SLEEP_MODE = False
            return {"action": "wake", "tts": "Permisi, ada paket."}
        
        for name in WHITELIST:
            if name in text_lower:
                SLEEP_MODE = False
                return {"action": "open", "name": name.capitalize(), "tts": f"Baik, paket atas nama {name.capitalize()}. Silakan diambil."}
        if SLEEP_MODE:
            return {"action": "sleep", "tts": "Maaf, sedang istirahat."}
        return {"action": "deny", "message": "Maaf, nama tidak terdaftar.", "tts": "Maaf, nama tersebut tidak terdaftar pada paket ini."}

# ================= ROUTE (HTTP fallback, kalau MQTT gagal) =================
@app.route('/package-voice', methods=['POST'])
def handle_voice_input():
    try:
        data = request.get_json()
        if not data or 'text' not in data:
            return jsonify({"error": "Text input required"}), 400
        user_speech = data['text'].strip()
        print(f"👤 HTTP Input: {user_speech}")
        decision = extract_name_with_gemini(user_speech)
        # Simulate TTS via espeak untuk HTTP juga
        if "tts" in decision:
            os.system(f'espeak "{decision["tts"]}"')
        return jsonify(decision)
    except Exception as e:
        print(f"[ERROR] {e}")
        return jsonify({"error": "Server error"}), 500

@app.route('/health')
def health():
    return jsonify({"status": "OK", "model": MODEL_NAME})

if __name__ == '__main__':
    print("🚀 Laptop: Gemini AI Server Aktif! (MQTT ready)")
    print(f"Model: {MODEL_NAME}")
    print("ESP32 kirim text ke 'package/text' via MQTT")
    app.run(host='0.0.0.0', port=5000, debug=True)
