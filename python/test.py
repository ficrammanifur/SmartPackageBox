import paho.mqtt.client as mqtt
import time

BROKER = "broker.hivemq.com"
PORT = 1883

PUB_TOPIC = "package/chat"
SUB_TOPIC = "package/confirm"

def on_connect(client, userdata, flags, rc):
    print("✅ MQTT connected")
    client.subscribe(SUB_TOPIC)

def on_message(client, userdata, msg):
    print("📩 ESP32:", msg.payload.decode())

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, 60)
client.loop_start()

print("\n=== Smart Package Box Test ===")
print("Ketik:")
print("  trigger  → ESP32 tanya nama")
print("  nama xxx → kirim nama (contoh: nama Aisah)")
print("  exit     → keluar\n")

while True:
    user_input = input(">> ").strip()

    if not user_input:
        continue

    if user_input.lower() == "exit":
        print("⛔ Exit")
        break

    elif user_input.lower() == "trigger":
        client.publish(PUB_TOPIC, "trigger_detected")
        print("📤 Trigger sent")

    else:
        # APAPUN YANG DIKETIK DIANGGAP NAMA
        name = user_input.strip()
        client.publish(PUB_TOPIC, f"name:{name}")
        print(f"📤 Name sent: {name}")

    time.sleep(0.2)

client.loop_stop()
client.disconnect()
