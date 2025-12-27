import time
import speech_recognition as sr
import pyttsx3
import paho.mqtt.client as mqtt

# ================= MQTT =================
MQTT_BROKER = "broker.hivemq.com"
MQTT_TOPIC = "package/chat"

mqtt_client = mqtt.Client()
mqtt_client.connect(MQTT_BROKER, 1883, 60)
mqtt_client.loop_start()

def send(msg):
    mqtt_client.publish(MQTT_TOPIC, msg.encode())
    print("📡 MQTT →", msg)

# ================= TTS INDONESIA =================
engine = pyttsx3.init()
engine.setProperty("rate", 150)

def speak(text, delay=0.7):
    print("🤖 Kurir:", text)
    engine.say(text)
    engine.runAndWait()
    time.sleep(delay)

# ================= STT =================
r = sr.Recognizer()
mic = sr.Microphone()

def listen():
    with mic as source:
        print("🎧 Mendengarkan...")
        r.adjust_for_ambient_noise(source, duration=0.5)
        audio = r.listen(source)
    try:
        return r.recognize_google(audio, language="id-ID").lower()
    except:
        return ""

# ================= FLOW =================
print("🚚 Kurir Paket Aktif")

state = "ASK_NAME"

send("trigger_detected")
speak("Permisi, ada paket.")
speak("Paket atas nama siapa?")

while True:
    user_input = listen()

    if not user_input:
        speak("Maaf, bisa diulangi namanya?")
        continue

    print("👤 User:", user_input)

    if state == "ASK_NAME":
        name = user_input.strip()
        send(f"name:{name}")
        speak(f"Baik, paket atas nama {name}.")
        speak("Silakan diambil ya. Terima kasih.")
        break
