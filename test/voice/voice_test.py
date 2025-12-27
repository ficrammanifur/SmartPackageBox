import speech_recognition as sr
import paho.mqtt.client as mqtt
import time

BROKER = "broker.hivemq.com"
PORT = 1883
PUB_TOPIC = "package/chat"

# MQTT
client = mqtt.Client()
client.connect(BROKER, PORT, 60)
client.loop_start()

print("🎙️ Voice Test Ready")
print("Ucapkan nama (Aisah / Rabiathul / Nadiyah)")
print("Ctrl+C untuk keluar\n")

recognizer = sr.Recognizer()
mic = sr.Microphone()

try:
    while True:
        with mic as source:
            recognizer.adjust_for_ambient_noise(source, duration=0.5)
            print("🎧 Listening...")
            audio = recognizer.listen(source)

        try:
            text = recognizer.recognize_google(audio, language="id-ID")
            print("🗣️ Heard:", text)

            name = text.strip().lower()
            client.publish(PUB_TOPIC, f"name:{name}")
            print(f"📤 Sent name:{name}\n")

        except sr.UnknownValueError:
            print("❌ Tidak jelas, ulangi...\n")
        except sr.RequestError as e:
            print("⚠️ Speech API error:", e)

        time.sleep(0.5)

except KeyboardInterrupt:
    print("\n⛔ Stop")

client.loop_stop()
client.disconnect()
