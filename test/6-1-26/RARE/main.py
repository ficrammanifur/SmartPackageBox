from flask import Flask, request, jsonify, send_file
from flask_cors import CORS
import google.generativeai as genai
import os
from dotenv import load_dotenv
import paho.mqtt.client as mqtt
import json
import speech_recognition as sr
from gtts import gTTS
from pydub import AudioSegment
from io import BytesIO

load_dotenv()
app = Flask(__name__)
CORS(app)

# MQTT
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_PUB_TOPIC = "package/command"
MQTT_STATUS_TOPIC = "package/status"
mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

def send_cmd_to_esp(cmd_json):
    mqtt_client.publish(MQTT_PUB_TOPIC, json.dumps(cmd_json))
    print(f"📡 Cmd ke ESP32: {cmd_json}")

# Gemini
genai.configure(api_key=os.getenv('GEMINI_API_KEY'))
MODEL_NAME = "gemini-1.5-flash"
model = genai.GenerativeModel(MODEL_NAME, system_instruction="""Anda adalah kurir pintar berbasis AI.
Dialog natural seperti manusia, dengan empati dan profesional.
Whitelist nama penerima dari database internal.
Output SELALU JSON dengan keys: 'cmd': 'set_status'/'open_box'/'sleep', 'state'/'name', 'tts_text', 'file': 'response.wav'""")

SLEEP_MODE = False

def process_voice(text):
    global SLEEP_MODE
    if SLEEP_MODE and "hallo" not in text.lower():
        return {"cmd": "sleep", "tts_text": "Saya sedang istirahat. Katakan 'hallo'."}
    if "hallo" in text.lower():
        SLEEP_MODE = False
        return {"cmd": "set_status", "state": "Mendengarkan", "tts_text": "Permisi, ada paket. Dengan siapa saya berbicara?"}
    response = model.generate_content(text)
    result = json.loads(response.text.strip())
    if result.get("action") == "sleep": SLEEP_MODE = True
    return result

# STT
recognizer = sr.Recognizer()
recognizer.energy_threshold = 300

# Generate TTS WAV
def generate_tts_wav(text, filename):
    tts = gTTS(text, lang='id', slow=False)
    tts_io = BytesIO()
    tts.write_to_fp(tts_io)
    tts_io.seek(0)
    response_audio = AudioSegment.from_mp3(tts_io)
    response_audio = response_audio.set_frame_rate(16000).set_channels(1).set_sample_width(2)
    response_path = os.path.join("static", filename)
    os.makedirs("static", exist_ok=True)
    response_audio.export(response_path, format="wav")
    print(f"Generated {filename}: {text}")
    return filename

# ================= ROUTES =================
@app.route('/audio_stream', methods=['POST'])
def audio_stream():
    try:
        raw_pcm = request.get_data()
        if not raw_pcm:
            return jsonify({"error": "No audio"}), 400
        # Convert raw PCM to WAV
        audio = AudioSegment(
            raw_pcm,
            frame_rate=16000,
            sample_width=2,
            channels=1
        )
        wav_io = BytesIO()
        audio.export(wav_io, format="wav")
        wav_io.seek(0)
        # STT
        with sr.AudioFile(wav_io) as source:
            audio_data = recognizer.record(source)
            text = recognizer.recognize_google(audio_data, language='id-ID')
        print(f"👤 STT: {text}")
        # Gemini
        decision = process_voice(text)
        tts_text = decision.get("tts_text", "Respons default")
        # TTS
        filename = "response.wav"
        generate_tts_wav(tts_text, filename)
        # MQTT cmds
        send_cmd_to_esp({"cmd": "set_status", "state": "Berpikir"})
        if decision.get("cmd") == "open_box":
            send_cmd_to_esp({"cmd": "open_box", "name": decision.get("name")})
        send_cmd_to_esp({"cmd": "play_audio", "file": filename})
        send_cmd_to_esp({"cmd": "set_status", "state": "Menjawab"})
        return jsonify({"status": "processed", "text": text}), 200
    except Exception as e:
        print(f"[Error]: {e}")
        return jsonify({"error": str(e)}), 500

@app.route('/audio/<filename>')
def serve_audio(filename):
    return send_file(os.path.join("static", filename))

@app.route('/health')
def health():
    return jsonify({"status": "OK"})

# ================= MQTT HANDLER =================
def on_mqtt_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode().strip()
    if topic == MQTT_STATUS_TOPIC and payload == "boot_ready":
        print("🤖 ESP32 Online — kirim suara sambutan")
        send_cmd_to_esp({"cmd": "set_status", "state": "Menyapa"})  # Fase 3: Update OLED
        greeting_text = "Halo, perangkat siap digunakan. Silakan berbicara."
        filename = "welcome.wav"
        generate_tts_wav(greeting_text, filename)
        send_cmd_to_esp({"cmd": "play_audio", "file": filename})

mqtt_client.on_message = on_mqtt_message
mqtt_client.subscribe(MQTT_STATUS_TOPIC)

if __name__ == '__main__':
    print("🚀 PC Server Aktif! (HTTP + MQTT)")
    app.run(host='0.0.0.0', port=5000, debug=True)
