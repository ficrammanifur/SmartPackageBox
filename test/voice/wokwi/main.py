# Smart Package Box - ESP32: OLED Animasi + Servo Lock via MQTT
# Fitur: Animasi wajah, tampil teks interaksi, buka servo jika nama valid (Aisah/Rabiathul/Nadiyah)
from machine import Pin, I2C, PWM, unique_id
import ssd1306
import time
import math
import network
import umqtt.simple as mqtt
import utime
import ubinascii

# Konfigurasi WiFi & MQTT (sama seperti sebelumnya)
SSID = 'Wokwi-GUEST'
PASS = ''
MQTT_BROKERS = ['broker.hivemq.com', 'mqtt.wokwi.com']
MQTT_PORT = 1883
MQTT_SUB_TOPIC = b'package/chat'  # Topic baru untuk package box
MQTT_PUB_TOPIC = b'package/confirm'
CLIENT_ID = ubinascii.hexlify(unique_id())

WIDTH = 128
HEIGHT = 64

# Setup Servo (pin 19, 50Hz)
servo_pin = 19
servo = PWM(Pin(servo_pin), freq=50)
SERVO_MIN_US = 1000
SERVO_MAX_US = 2000
PERIOD_US = 20000

def servo_angle(angle):
    if 0 <= angle <= 180:
        pulse_us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * (angle / 180.0)
        duty = int((pulse_us * 65535) / PERIOD_US)
        servo.duty_u16(duty)
        print(f"Servo to {angle}° (duty: {duty})")

# Setup WiFi
def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        wlan.connect(SSID, PASS)
        print('Connecting WiFi...')
        while not wlan.isconnected():
            time.sleep(1)
        print('WiFi connected:', wlan.ifconfig())

connect_wifi()

# Setup I2C & OLED
i2c = I2C(0, scl=Pin(22), sda=Pin(21))
oled = ssd1306.SSD1306_I2C(WIDTH, HEIGHT, i2c)
fb = oled.framebuf

# Global vars
package_message = None
mqtt_client = None
servo_open_until = 0
mqtt_connected = False
whitelist_names = ['aisyah', 'rabiathul', 'nadia']  # Lowercase untuk cek mudah

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
        cx_left = 40 + offset
        draw_filled_circle(fb, cx_left, eye_y, eye_r, 1)
        cx_right = 88 + offset
        draw_filled_circle(fb, cx_right, eye_y, eye_r, 1)
    draw_mouth(fb, mouth_open)
    oled.show()

def draw_package_text(text):
    oled.fill(0)
    draw_mouth(fb, open_mouth=True)  # Mulut "bicara"
    lines = []
    words = text.split()
    line = ''
    for word in words:
        test_line = line + word + ' '
        if len(test_line) < 21:
            line = test_line
        else:
            lines.append(line.strip())
            line = word + ' '
    if line:
        lines.append(line.strip()[:20] + '...' if len(line.strip()) > 20 else line.strip())
    y_start = 30
    for i, line in enumerate(lines[:3]):
        x = (WIDTH - len(line) * 6) // 2
        fb.text(line, x, y_start + i * 8, 1)
    oled.show()

# MQTT Callback: Proses command dari laptop
def mqtt_callback(topic, msg):
    global package_message, servo_open_until
    command = msg.decode('utf-8').lower()
    print('Received:', command)
    
    if 'trigger_detected' in command:  # Wake word "Permisi, ada paket"
        package_message = "Paket atas nama siapa?"
        print("Trigger detected! Asking for name.")
    elif 'name:' in command:  # Format: "name:Aisah"
        name = command.split('name:')[1].strip()
        if name in whitelist_names:
            display_name = name[0].upper() + name[1:]
            package_message = f"Paket untuk {display_name}. Membuka kotak..."
            servo_angle(0)
            servo_open_until = utime.ticks_add(utime.ticks_ms(), 2000)
            print(f"Valid name: {name}. Opening lock.")
        else:
            package_message = "Nama tidak terdaftar. Tidak bisa dibuka."
            print(f"Invalid name: {name}. Denied.")
    else:
        package_message = command  # Fallback pesan lain

def restart_and_reconnect():
    global mqtt_connected
    print('MQTT reconnecting in 5s...')
    time.sleep(5)
    mqtt_connected = False

def connect_to_broker(broker):
    global mqtt_client, mqtt_connected
    for attempt in range(1, 4):
        try:
            print(f"Connecting to {broker} (attempt {attempt})...")
            mqtt_client = mqtt.MQTTClient(CLIENT_ID, broker, MQTT_PORT)
            mqtt_client.set_callback(mqtt_callback)
            mqtt_client.connect()
            mqtt_client.subscribe(MQTT_SUB_TOPIC)
            mqtt_connected = True
            print(f'MQTT connected & subscribed to {MQTT_SUB_TOPIC}')
            return True
        except OSError as e:
            print(f'Failed: {e}')
            if '113' in str(e):
                restart_and_reconnect()
            if attempt < 3:
                time.sleep(2 ** (attempt - 1) + 2)
    return False

def setup_mqtt():
    global mqtt_connected
    for broker in MQTT_BROKERS:
        if connect_to_broker(broker):
            return
    print("All brokers failed. Retrying in loop.")
    mqtt_connected = False

setup_mqtt()

# Init servo closed
servo_angle(90)
print("Lock initialized closed.")

# Main loop vars
t = 0
package_mode = False
package_duration = 0
last_mqtt_check = 0
last_reconnect = 0
pos_list = [-4, -2, 0, 2, 4, 2, 0, -2]

try:
    while True:
        current_time = utime.ticks_ms()

        # Reconnect MQTT every 15s if down
        if not mqtt_connected and utime.ticks_diff(current_time, last_reconnect) > 15000:
            setup_mqtt()
            last_reconnect = current_time

        # Non-blocking MQTT check
        if mqtt_connected and utime.ticks_diff(current_time, last_mqtt_check) > 100:
            try:
                mqtt_client.check_msg()
            except OSError as e:
                if '113' in str(e):
                    restart_and_reconnect()
            except Exception as e:
                print(f"MQTT error: {e}")
                mqtt_connected = False
            last_mqtt_check = current_time

        # Auto-close servo
        if servo_open_until and utime.ticks_diff(current_time, servo_open_until) > 0:
            servo_angle(90)
            servo_open_until = 0
            print("Lock closed.")

        if package_message:
            package_mode = True
            package_duration = max(3, len(package_message) // 20 + 2)
            # Animasi bicara cepat
            for _ in range(10):
                mouth_open = (_ % 2 == 0)
                draw_face(0, blink=False, mouth_open=mouth_open)
                time.sleep(0.1)
            # Tampil teks
            draw_package_text(package_message)
            time.sleep(package_duration)
            # Publish confirm
            if mqtt_connected:
                try:
                    mqtt_client.publish(MQTT_PUB_TOPIC, b'Message displayed & lock handled')
                except Exception as e:
                    print(f'Publish failed: {e}')
            package_message = None
            package_mode = False

        if not package_mode:
            # Normal animasi
            mouth_open = (math.sin(t * 3) > 0)
            for pos in pos_list:
                draw_face(pos, blink=False, mouth_open=mouth_open)
                time.sleep(0.15)
                t += 0.15
            # Kedip every ~15s
            if int(current_time / 15000) % 15 == 0:
                draw_face(0, blink=True, mouth_open=False)
                time.sleep(0.2)
                draw_face(0, blink=False, mouth_open=False)
                time.sleep(0.2)

except KeyboardInterrupt:
    oled.fill(0)
    oled.show()
    servo_angle(0)
    if mqtt_client:
        mqtt_client.disconnect()
    print("Stopped.")
