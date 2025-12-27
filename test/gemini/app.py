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

# ================= MQTT SETUP =================
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_PUB_TOPIC = "package/chat"        # Kirim perintah ke ESP32
MQTT_SUB_TOPIC = "package/confirm"     # Opsional: terima konfirmasi

mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

def send_to_esp32(command):
    mqtt_client.publish(MQTT_PUB_TOPIC, command)
    print(f"📡 → ESP32: {command}")

# Tambahkan subscribe untuk konfirmasi
MQTT_SUB_TOPIC_CONFIRM = "package/confirm"

def on_mqtt_message(client, userdata, msg):
    payload = msg.payload.decode()
    print(f"📥 Konfirmasi dari ESP32: {payload}")
    userdata['last_confirm'] = payload  # Simpan untuk kurir_suara cek

mqtt_client.on_message = on_mqtt_message
mqtt_client.user_data_set({'last_confirm': None})
mqtt_client.subscribe(MQTT_SUB_TOPIC_CONFIRM)

# ================= GEMINI AI SETUP =================
genai.configure(api_key=os.getenv('GEMINI_API_KEY'))

MODEL_NAME = "gemini-1.5-flash"

model = genai.GenerativeModel(
    MODEL_NAME,
    system_instruction="""
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

# Fallback whitelist
WHITELIST = ["aisyah", "rabiathul", "nadia"]

def extract_name_with_gemini(text):
    try:
        response = model.generate_content(text)
        result = json.loads(response.text.strip())
        return result
    except Exception as e:
        print(f"[Gemini Error] Fallback aktif: {e}")
        text_lower = text.lower()
        for name in WHITELIST:
            if name in text_lower:
                return {"action": "open", "name": name.capitalize()}
        return {"action": "deny", "message": "Maaf, nama tidak terdaftar."}

# ================= ROUTE UTAMA =================
@app.route('/package-voice', methods=['POST'])
def handle_voice_input():
    try:
        data = request.get_json()
        if not data or 'text' not in data:
            return jsonify({"error": "Text input required"}), 400

        user_speech = data['text'].strip()
        print(f"👤 Pengguna berkata: {user_speech}")

        decision = extract_name_with_gemini(user_speech)

        action = decision.get("action")
        name = decision.get("name")
        message = decision.get("message", "")

        if action == "open":
            # KIRIM PERINTAH KE ESP32: buka kotak
            send_to_esp32(f"name:{name.lower()}")
            return jsonify({
                "status": "success",
                "action": "open",
                "name": name,
                "speak": f"Selamat! Paket untuk {name}. Kotak akan terbuka sekarang."
            })

        elif action == "deny":
            send_to_esp32("invalid_name")
            return jsonify({
                "status": "success",
                "action": "deny",
                "speak": message or "Maaf, nama tidak terdaftar. Paket tidak bisa diambil."
            })

        elif action == "ask_name":
            send_to_esp32("ask_name")
            return jsonify({
                "status": "success",
                "action": "ask_name",
                "speak": message
            })

        else:
            return jsonify({"error": "Unknown action"}), 500

    except Exception as e:
        print(f"[ERROR] {e}")
        return jsonify({"error": "Server error"}), 500

@app.route('/health')
def health():
    return jsonify({"status": "OK", "model": MODEL_NAME})

if __name__ == '__main__':
    print("🚀 Smart Package Box AI Bridge dengan Gemini aktif!")
    print(f"   Model: {MODEL_NAME}")
    print("   Siap menerima suara → analisis → kontrol ESP32")
    app.run(host='0.0.0.0', port=5000, debug=True)
