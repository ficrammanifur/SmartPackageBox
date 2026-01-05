#include <WiFi.h>
#include <PubSubClient.h>  // MQTT
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>  // OLED
#include <ESP32Servo.h>  // Servo
#include <ArduinoJson.h>  // Untuk parse JSON
#include <driver/i2s.h>   // I2S untuk MAX98357A
#include <math.h>         // sin() untuk tone

// ================= KONFIG =================
const char* ssid = "Buat Apa";
const char* password = "syafril1112";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_pass = "";

// Topics
const char* text_topic = "package/text";
const char* response_topic = "package/response";
const char* status_topic = "package/status";

// Hardware Pins
#define SERVO_PIN 19
#define OLED_SDA 21
#define OLED_SCL 22
#define I2S_BCLK 27
#define I2S_LRC  26
#define I2S_DOUT 25

// Objek
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo myServo;

// Global untuk throttle
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;
const unsigned long RECONNECT_INTERVAL = 5000;  // 5s antar retry di loop

// I2S Globals
bool i2s_ready = false;

// ================= I2S SETUP =================
void initI2S() {
  Serial.println("Init I2S for MAX98357A...");
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,      // Naikkan buffer
    .dma_buf_len = 1024,     // Buffer lebih besar
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S install failed: %d\n", err);
    return;
  }

  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin set failed: %d\n", err);
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }

  err = i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (err != ESP_OK) {
    Serial.printf("I2S clock set failed: %d\n", err);
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }

  i2s_zero_dma_buffer(I2S_NUM_0);  // Clear buffer
  i2s_ready = true;
  Serial.println("I2S ready!");
  oledUpdate("I2S OK");  // Flash di OLED
  delay(500);
  oledUpdate("Siap!");   // Kembali
}

// Play simple sine tone (beep)
void playTone(float freq, int duration_ms) {
  if (!i2s_ready) {
    Serial.println("I2S not ready, skip tone");
    return;
  }

  const int sample_rate = 16000;
  const int samples_per_cycle = sample_rate / freq;
  int total_samples = (duration_ms * sample_rate) / 1000;
  static float phase = 0;
  
  int16_t buffer[1024];  // Buffer besar
  size_t bytes_written;

  for (int i = 0; i < total_samples; i += 512) {  // Chunked write
    int chunk_size = min(512, total_samples - i);
    for (int j = 0; j < chunk_size; j++) {
      float val = sin(phase) * 2000;  // Amplitude rendah, adjust kalau perlu
      buffer[j * 2] = buffer[j * 2 + 1] = (int16_t)val;  // Stereo
      phase += 2 * PI / samples_per_cycle;
      if (phase >= 2 * PI) phase -= 2 * PI;
    }
    i2s_write(I2S_NUM_0, (const char*)buffer, chunk_size * 4, &bytes_written, portMAX_DELAY);  // 16bit stereo = 4 bytes/sample
  }

  // Flush silence
  memset(buffer, 0, sizeof(buffer));
  i2s_write(I2S_NUM_0, (const char*)buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
  Serial.printf("Played tone %.0fHz for %dms\n", freq, duration_ms);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);  // Stabilin Serial
  
  // OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Starting...");
  display.display();
  
  // Servo
  myServo.attach(SERVO_PIN);
  myServo.write(0);  // Tutup
  Serial.println("Servo attached");
  
  // WiFi (timeout 10s biar gak infinite loop)
  WiFi.begin(ssid, password);
  int wifi_timeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_timeout < 100) {  // Max 10s
    delay(100);
    Serial.print(".");
    wifi_timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
    Serial.println("WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");  // Log signal
    oledUpdate("WiFi OK");
  } else {
    Serial.println("\nWiFi failed!");
    oledUpdate("WiFi Failed");
    return;  // Stop kalau WiFi gagal
  }
  
  // MQTT
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  reconnectMQTT();
  
  // I2S (setelah MQTT, biar prioritas network)
  if (mqtt_client.connected()) {
    initI2S();
  }
  
  Serial.println("🚀 ESP32 Stable!");
  oledUpdate("Siap! Test MQTT");
  mqttPublishStatus("boot_ready");  // 🔊 Trigger suara laptop
  
  // Boot beep dari ESP32
  playTone(440, 200);  // Short beep
  
  // Test Servo (hanya kalau MQTT OK)
  if (mqtt_client.connected()) {
    servoOpen();
    delay(1000);
    servoClose();
    Serial.println("Servo test OK");
  } else {
    Serial.println("Skip servo test: MQTT not stable");
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost! Reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }
  
  if (!mqtt_client.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      reconnectMQTT();
    }
  } else {
    // Reset attempts kalau connected
    reconnectAttempts = 0;
  }
  mqtt_client.loop();
  
  delay(1000);  // Throttle loop ke 1s (kurangi spam reconnect)
}

// ================= MQTT =================
void reconnectMQTT() {
  int attempts = 0;
  String clientId = "ESP32Client-" + String(millis());  // Unique ID dengan timestamp
  while (!mqtt_client.connected() && attempts < 3) {  // Max 3 attempts per call
    Serial.print("Attempting MQTT connection #" + String(attempts + 1) + " ...");
    Serial.print("Client ID: " + clientId);
    Serial.print(" | WiFi RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    if (mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" - Success!");
      mqtt_client.subscribe(response_topic);
      oledUpdate("MQTT OK");
      
      // Re-init I2S kalau MQTT reconnect (jarang, tapi safe)
      if (i2s_ready) {
        i2s_driver_uninstall(I2S_NUM_0);
      }
      initI2S();
      
      reconnectAttempts = 0;  // Reset global attempts
    } else {
      Serial.print(" - Fail, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" retry in 5s");
      delay(5000);
      attempts++;
      reconnectAttempts++;
    }
  }
  
  // Fallback kalau gagal total
  if (!mqtt_client.connected() && reconnectAttempts > 5) {  // >5 total attempts
    Serial.println("MQTT unstable! Check network/broker.");
    oledUpdate("MQTT Error");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Response: " + message);
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.println("JSON error");
    return;
  }
  
  String action = doc["action"].as<String>();
  if (action == "open") {
    String name = doc["name"].as<String>();
    oledUpdate("Open: " + name);
    playTone(523, 300);  // C5 note untuk confirm (lebih happy tone)
    servoOpen();
    mqttPublishStatus("opened");
    Serial.println("Simulate TTS: Selamat " + name);  // Nanti ganti real TTS
  } else if (action == "deny") {
    oledUpdate("Denied");
    playTone(220, 200);  // Low tone untuk deny
    Serial.println("Simulate TTS: Maaf");
  } else if (action == "sleep") {
    oledUpdate("Sleep Mode");
    playTone(0, 100);  // Silence
    Serial.println("Simulate TTS: Mode tidur");
  }
}

void sendTextToLaptop(String text) {
  if (mqtt_client.connected()) {
    mqtt_client.publish(text_topic, text.c_str());
    Serial.println("📤 Sent: " + text);
  } else {
    Serial.println("Cannot send: MQTT disconnected");
  }
}

// ================= HARDWARE =================
void oledUpdate(String msg) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(msg);
  display.display();
}

void servoOpen() {
  myServo.write(90);
  delay(500);
}

void servoClose() {
  myServo.write(0);
  delay(500);
  mqttPublishStatus("closed");
}

void mqttPublishStatus(String status) {
  if (mqtt_client.connected()) {
    mqtt_client.publish(status_topic, status.c_str());
    Serial.println("📤 Status: " + status);
  } else {
    Serial.println("Cannot publish status: MQTT down");
  }
}
