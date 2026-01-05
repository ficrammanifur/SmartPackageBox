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

mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

def send_response_to_esp32(response_json):
    mqtt_client.publish(MQTT_PUB_TOPIC, json.dumps(response_json))
    print(f"📡 Response ke ESP32: {response_json}")

# Subscribe untuk nerima text dari ESP32
def on_mqtt_message(client, userdata, msg):
    try:
        user_text = msg.payload.decode().strip()
        print(f"👤 Text dari ESP32: {user_text}")
        decision = extract_name_with_gemini(user_text)
        send_response_to_esp32(decision)
    except Exception as e:
        print(f"[MQTT Error]: {e}")
        send_response_to_esp32({"action": "error", "message": "Server error"})

mqtt_client.on_message = on_mqtt_message
mqtt_client.subscribe(MQTT_SUB_TOPIC)

# ================= GEMINI AI SETUP (sama seperti punyamu) =================
genai.configure(api_key=os.getenv('GEMINI_API_KEY'))

MODEL_NAME = "gemini-1.5-flash"

model = genai.GenerativeModel(
    MODEL_NAME,
    system_instruction="""  # [Sama persis seperti punyamu – daftar nama: aisyah, rabiathul, nadia ]
    Kamu adalah asisten pintar untuk Smart Package Box.
    Tugasmu: dari ucapan pengguna, tentukan apakah nama yang disebutkan termasuk dalam daftar penerima paket resmi.
    
    Daftar nama resmi (case-insensitive):
    - aisyah
    - rabiathul
    - nadia
    
    Aturan:
    1. Jika ucapan mengandung salah satu nama di atas (bisa dengan tambahan kata seperti "untuk", "buat", "mbak", "paket untuk", dll), kembalikan JSON:
       {"action": "open", "name": "NamaAsli"}
    
    2. Jika nama tidak ada di daftar, kembalikan:
       {"action": "deny", "message": "Maaf, nama tidak terdaftar."}
    
    3. Jika belum jelas atau tidak ada nama sama sekali, kembalikan:
       {"action": "ask_name", "message": "Paket atas nama siapa ya?"}
    
    Hanya kembalikan JSON valid. Jangan tambahkan teks lain.
    Contoh:
    Input: "Paket untuk Aisyah"
    Output: {"action": "open", "name": "Aisyah"}
    
    Input: "Ini buat Budi"
    Output: {"action": "deny", "message": "Maaf, nama tidak terdaftar."}
    """
)

WHITELIST = ["aisyah", "rabiathul", "nadia"]

def extract_name_with_gemini(text):
    try:
        response = model.generate_content(text)
        result = json.loads(response.text.strip())
        return result
    except Exception as e:
        print(f"[Gemini Error] Fallback: {e}")
        text_lower = text.lower()
        for name in WHITELIST:
            if name in text_lower:
                return {"action": "open", "name": name.capitalize()}
        return {"action": "deny", "message": "Maaf, nama tidak terdaftar."}

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
