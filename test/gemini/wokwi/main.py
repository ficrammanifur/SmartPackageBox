# Smart Package Box - ESP32 Wokwi (SERVO FIX + MQTT STATUS)
from machine import Pin, I2C, PWM, unique_id
import ssd1306
import time
import math
import network
import umqtt.simple as mqtt
import utime
import ubinascii

# ================= KONFIG =================
SSID = 'Wokwi-GUEST'
PASS = ''
MQTT_BROKERS = ['broker.hivemq.com', 'mqtt.wokwi.com']
MQTT_PORT = 1883
MQTT_SUB_TOPIC = b'package/chat'
MQTT_PUB_TOPIC = b'package/status'
CLIENT_ID = ubinascii.hexlify(unique_id())

WIDTH = 128
HEIGHT = 64

# ================= SERVO FIX WOKWI =================
servo_pin = 19
servo = PWM(Pin(servo_pin), freq=50)

def servo_open():
    servo.duty(26)  # 0° = terbuka
    print("✅ SERVO TERBUKA (duty 26)")

def servo_close():
    servo.duty(123)  # 180° = tertutup
    print("🔒 SERVO TERTUTUP (duty 123)")

# ================= WIFI & OLED =================
def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        print('Connecting WiFi...')
        wlan.connect(SSID, PASS)
        while not wlan.isconnected():
            time.sleep(1)
    print('WiFi connected:', wlan.ifconfig())

connect_wifi()

i2c = I2C(0, scl=Pin(22), sda=Pin(21))
oled = ssd1306.SSD1306_I2C(WIDTH, HEIGHT, i2c)
fb = oled.framebuf

# ================= GRAFIS =================
def draw_filled_circle(fb, cx, cy, r, col):
    for dy in range(-r, r + 1):
        y = cy + dy
        if 0 <= y < HEIGHT:
            dx = int(math.sqrt(r * r - dy * dy))
            x1 = max(0, cx - dx)
            x2 = min(WIDTH - 1, cx + dx)
            if x1 <= x2:
                fb.hline(x1, y, x2 - x1 + 1, col)

def draw_smile(fb, cx, cy, r):
    for angle in range(180, 361, 5):
        theta = math.radians(angle)
        px = cx + r * math.cos(theta)
        py = cy - r * math.sin(theta)
        if 0 <= int(px) < WIDTH and 0 <= int(py) < HEIGHT:
            fb.pixel(int(px), int(py), 1)

def draw_mouth(fb, open_mouth=False):
    mouth_x = 64
    mouth_y = 45
    if open_mouth:
        draw_filled_circle(fb, mouth_x, mouth_y, 5, 1)
    else:
        draw_smile(fb, mouth_x, mouth_y, 8)

def draw_face(offset=0, blink=False, mouth_open=False):
    oled.fill(0)
    eye_y = 22
    eye_r = 12
    if blink:
        fb.hline(28, eye_y, 24, 1)
        fb.hline(76, eye_y, 24, 1)
    else:
        draw_filled_circle(fb, 40 + offset, eye_y, eye_r, 1)
        draw_filled_circle(fb, 88 + offset, eye_y, eye_r, 1)
    draw_mouth(fb, mouth_open)
    oled.show()

def draw_text(lines):
    oled.fill(0)
    draw_mouth(fb, open_mouth=True)
    for i, line in enumerate(lines[:3]):
        x = (WIDTH - len(line) * 6) // 2
        fb.text(line, x, 28 + i * 12, 1)
    oled.show()

# ================= MQTT =================
package_message = None
mqtt_client = None
servo_open_until = 0
mqtt_connected = False
box_is_open = False  # Track status kotak

def mqtt_callback(topic, msg):
    global package_message, servo_open_until, box_is_open
    command = msg.decode('utf-8').strip().lower()
    print(f'📥 Received: {command}')

    if "ask_name" in command:
        package_message = ["Paket atas nama", "siapa ya?"]

    elif command.startswith("name:"):
        if box_is_open:  # Cegah buka ulang jika sudah terbuka
            print("⚠️ Kotak sudah terbuka, skip command")
            return
            
        name = command[5:].strip()
        # Manual capitalize untuk MicroPython
        if name:
            name = name[0].upper() + name[1:].lower() if len(name) > 1 else name.upper()
        package_message = ["Selamat!", f"Paket untuk {name}", "Kotak terbuka!"]
        
        # BUKA SERVO
        servo_open()
        box_is_open = True
        servo_open_until = utime.ticks_add(utime.ticks_ms(), 15000)  # 15 detik
        
        # Kirim konfirmasi opened
        if mqtt_connected:
            mqtt_client.publish(MQTT_PUB_TOPIC, b'opened')
            print("📤 Status sent: opened")

    elif command == "close_box":
        if not box_is_open:  # Cegah tutup ulang
            print("⚠️ Kotak sudah tertutup, skip command")
            return
            
        servo_close()
        box_is_open = False
        servo_open_until = 0
        package_message = ["Terima kasih!", "Kotak ditutup"]
        
        # Kirim konfirmasi closed
        if mqtt_connected:
            mqtt_client.publish(MQTT_PUB_TOPIC, b'closed')
            print("📤 Status sent: closed")

    elif command == "invalid_name":
        package_message = ["Maaf,", "Nama tidak terdaftar"]

def connect_to_broker(broker):
    global mqtt_client, mqtt_connected
    try:
        mqtt_client = mqtt.MQTTClient(CLIENT_ID, broker, MQTT_PORT)
        mqtt_client.set_callback(mqtt_callback)
        mqtt_client.connect()
        mqtt_client.subscribe(MQTT_SUB_TOPIC)
        mqtt_connected = True
        print(f'✅ MQTT Connected to {broker}!')
        return True
    except Exception as e:
        print(f'❌ MQTT Failed ({broker}):', e)
        return False

def setup_mqtt():
    for broker in MQTT_BROKERS:
        if connect_to_broker(broker):
            return

setup_mqtt()

# Init tertutup
servo_close()
print("🔒 Lock initialized: CLOSED")

# ================= MAIN LOOP =================
t = 0
package_mode = False
last_mqtt_check = 0
last_reconnect = 0
pos_list = [-4, -2, 0, 2, 4, 2, 0, -2]

try:
    while True:
        current_time = utime.ticks_ms()

        # Reconnect MQTT jika terputus
        if not mqtt_connected and utime.ticks_diff(current_time, last_reconnect) > 15000:
            setup_mqtt()
            last_reconnect = current_time

        # Check pesan MQTT
        if mqtt_connected and utime.ticks_diff(current_time, last_mqtt_check) > 100:
            try:
                mqtt_client.check_msg()
            except Exception as e:
                print("❌ MQTT error:", e)
                mqtt_connected = False
            last_mqtt_check = current_time

        # Auto tutup setelah timeout (15 detik)
        if box_is_open and servo_open_until and utime.ticks_diff(current_time, servo_open_until) > 0:
            servo_close()
            box_is_open = False
            print("⏱️ Kotak ditutup otomatis (timeout)")
            
            if mqtt_connected:
                mqtt_client.publish(MQTT_PUB_TOPIC, b'closed')
                print("📤 Status sent: closed (timeout)")
            
            servo_open_until = 0
            package_message = ["Timeout!", "Kotak ditutup"]

        # Tampilkan pesan jika ada
        if package_message:
            package_mode = True
            for _ in range(12):
                draw_face(0, mouth_open=(_ % 2 == 0))
                time.sleep(0.1)
            draw_text(package_message)
            time.sleep(5)
            package_message = None
            package_mode = False

        # Animasi idle
        if not package_mode:
            mouth_open = (math.sin(t * 3) > 0)
            for pos in pos_list:
                draw_face(pos, mouth_open=mouth_open)
                time.sleep(0.15)
                t += 0.15
            if int(t) % 10 < 0.4:
                draw_face(0, blink=True)
                time.sleep(0.3)

except KeyboardInterrupt:
    servo_close()
    oled.fill(0)
    oled.show()
    print("🛑 Stopped.")
