import time
import speech_recognition as sr
import pyttsx3
import requests
import paho.mqtt.client as mqtt

# ================= KONFIG =================
FLASK_SERVER_URL = "http://127.0.0.1:5000/package-voice"
MQTT_BROKER = "broker.hivemq.com"
MQTT_CHAT_TOPIC = "package/chat"
MQTT_STATUS_TOPIC = "package/status"

# ================= MQTT HANDLER =================
status = ""

def on_status(client, userdata, msg):
    global status
    payload = msg.payload.decode()
    status = payload
    print(f"Status dari ESP32: {payload}")

mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_BROKER, 1883, 60)
mqtt_client.subscribe(MQTT_STATUS_TOPIC)
mqtt_client.on_message = on_status
mqtt_client.loop_start()

def send_command(command):
    mqtt_client.publish(MQTT_CHAT_TOPIC, command)
    print(f"Perintah dikirim: {command}")

# ================= TTS & STT =================
engine = pyttsx3.init()
engine.setProperty("rate", 145)

def speak(text, delay=1.0):
    print("🤖 Kurir:", text)
    engine.say(text)
    engine.runAndWait()
    time.sleep(delay)

r = sr.Recognizer()
mic = sr.Microphone()

def listen(timeout=10):
    with mic as source:
        print("🎧 Mendengarkan...")
        r.adjust_for_ambient_noise(source, duration=0.5)
        audio = r.listen(source, timeout=timeout, phrase_time_limit=10)
    try:
        text = r.recognize_google(audio, language="id-ID").lower()
        print(f"👤 Pengguna: {text}")
        return text
    except:
        return ""

# ================= FLOW =================
print("🚚 Smart Kurir Paket Aktif!")
speak("Permisi, ada paket untuk diantar.", 0.8)
speak("Paket ini atas nama siapa ya?")

while True:
    user_input = listen()
    if not user_input:
        speak("Maaf, saya tidak mendengar. Bisa diulangi namanya?")
        continue

    try:
        response = requests.post(FLASK_SERVER_URL, json={"text": user_input}, timeout=10)
        result = response.json()

        if result.get("status") == "success":
            speak(result.get("speak"))

            if result.get("action") == "open":
                # Kirim perintah buka ke ESP32
                send_command(f"name:{result.get('name', '').lower()}")

                # Tunggu konfirmasi opened
                status = ""
                for _ in range(20):
                    if status == "opened":
                        break
                    time.sleep(0.5)

                speak("Kotak sudah terbuka. Silakan ambil paketnya sekarang.")

                # Tanya selesai
                speak("Apakah sudah selesai mengambil paketnya?")
                reply = listen(timeout=15)

                if "sudah" in reply or "ya" in reply or "selesai" in reply:
                    send_command("close_box")
                    status = ""
                    for _ in range(20):
                        if status == "closed":
                            break
                        time.sleep(0.5)
                    speak("Terima kasih sudah mengambil paketnya. Selamat ya!")
                else:
                    speak("Waktu habis. Kotak akan ditutup.")
                    send_command("close_box")

                break

            elif result.get("action") == "deny":
                speak("Maaf, paket tidak bisa diambil.")
                break

    except Exception as e:
        print("Error:", e)
        speak("Sistem bermasalah.")
        break

mqtt_client.loop_stop()
print("Sesi selesai.")
