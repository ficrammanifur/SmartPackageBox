import speech_recognition as sr
import pyttsx3
import google.generativeai as genai
import paho.mqtt.client as mqtt
import os

# ===== CONFIG =====
BROKER = "broker.hivemq.com"
PUB_TOPIC = "package/chat"
GEMINI_API_KEY = os.getenv("GEMINI_API_KEY")

# ===== MQTT =====
mqtt_client = mqtt.Client()
mqtt_client.connect(BROKER, 1883, 60)
mqtt_client.loop_start()

# ===== TTS =====
engine = pyttsx3.init()
engine.setProperty("rate", 170)

def speak(text):
    engine.say(text)
    engine.runAndWait()

# ===== GEMINI =====
genai.configure(api_key=GEMINI_API_KEY)
model = genai.GenerativeModel(
    model_name="gemini-1.5-flash",
    system_instruction="""
    Kamu adalah Smart Package Box.
    Jawab singkat, sopan, dan fokus pada paket.
    """
)

# ===== SPEECH =====
recognizer = sr.Recognizer()
mic = sr.Microphone()

print("🎤 Smart Package Box aktif...")
speak("Kotak pintar siap menerima perintah")

while True:
    with mic as source:
        recognizer.adjust_for_ambient_noise(source)
        print("🎧 Mendengarkan...")
        audio = recognizer.listen(source)

    try:
        text = recognizer.recognize_google(audio, language="id-ID")
        print("🗣️ Kamu:", text)

        response = model.generate_content(text)
        reply = response.text.strip()

        print("🤖 Box:", reply)
        speak(reply)

        # Kirim ke ESP32
        mqtt_client.publish(PUB_TOPIC, f"name:{text.lower()}")

    except sr.UnknownValueError:
        speak("Maaf, saya tidak mendengar dengan jelas.")
    except Exception as e:
        print("Error:", e)
