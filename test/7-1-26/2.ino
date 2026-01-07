/*
 * SMART PACKAGE ESP32 v3 (LEGACY I2S - GEMINI AUDIO + MQTT INTEGRATION)
 * Perbaikan: Fix WDT "task not found" dengan esp_task_wdt_add(NULL) untuk main task (Arduino core 3.x compatible).
 *           Tambah MQTT: Subscribe command dari server, download & play WAV via I2S (pakai ESP32-audioI2S library untuk decode).
 *           Library dibutuhkan: 
 *             - PubSubClient (MQTT)
 *             - ESP32-audioI2S (audio decode & play, install via Library Manager)
 *             - ArduinoJson (parse JSON command)
 *             - WiFi, SPIFFS, U8g2, driver/i2s.h
 *           Server IP: Hardcode 10.88.109.39:5000 (dari log WiFi). Ganti jika beda.
 *           Command dari MQTT: {"cmd": "play_audio", "file": "active.wav"} → Download http://IP:5000/audio/file → Play.
 *           Boot: Kirim "boot_ready" ke MQTT setelah setup.
 *           Pin: OLED (SDA=21,SCL=22), I2S (DOUT=25,BCLK=27,LRC=26).
 */

#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_task_wdt.h>  // WDT
#include <Wire.h>
#include <U8g2lib.h>
#include "driver/i2s.h"
#include <PubSubClient.h>    // MQTT
#include <Audio.h>           // ESP32-audioI2S
#include <ArduinoJson.h>     // JSON parse
#include <HTTPClient.h>      // Download file

// WiFi Config
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// MQTT Config (dari app.py)
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_status_topic = "package/status";
const char* mqtt_command_topic = "package/command";
const char* mqtt_client_id = "ESP32_Package_01";

// Server Audio URL (IP dari log WiFi, port 5000 dari app.py)
const String server_ip = "10.88.109.39";
const int server_port = 5000;
const String audio_base_url = "http://" + server_ip + ":" + String(server_port) + "/audio/";

// OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// I2S Pins (untuk Audio lib)
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC  26

// Audio Object
Audio audio;

// WiFi & MQTT Clients
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// Timers & States
unsigned long boot_time = 0;
unsigned long last_wdt_feed = 0;
bool mqtt_connected = false;
bool boot_ready_sent = false;

// Fungsi log
void logActivity(const char* msg) {
  unsigned long activity_time = millis() - boot_time;
  Serial.printf("[%lu ms] ACTIVITY: %s\n", activity_time, msg);
}

// Callback MQTT Message
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("[MQTT] Message from %s: %s\n", topic, message.c_str());

  if (String(topic) == mqtt_command_topic) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, message);
    String cmd = doc["cmd"];
    if (cmd == "play_audio") {
      String filename = doc["file"];
      String url = audio_base_url + filename;
      Serial.printf("[AUDIO] Playing: %s\n", url.c_str());
      audio.connecttohost(url.c_str());  // Play dari URL (lib handle download & decode)
      u8g2.clearBuffer();
      u8g2.drawStr(0, 15, ("Playing: " + filename).c_str());
      u8g2.sendBuffer();
    }
  }
}

// MQTT Reconnect
void mqttReconnect() {
  if (mqtt_client.connected()) return;
  Serial.print("[MQTT] Attempting connection...");
  if (mqtt_client.connect(mqtt_client_id)) {
    Serial.println("connected");
    mqtt_client.subscribe(mqtt_command_topic);
    mqtt_connected = true;
    // Kirim boot ready setelah connect
    if (!boot_ready_sent) {
      mqtt_client.publish(mqtt_status_topic, "boot_ready");
      boot_ready_sent = true;
      logActivity("MQTT BOOT READY SENT");
    }
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqtt_client.state());
    Serial.println(" retry in 5s");
    delay(5000);
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  boot_time = millis();
  logActivity("BOOT START");

  // Fix WDT: Gunakan NULL untuk main task (Arduino core 3.x)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_err_t wdt_err = esp_task_wdt_init(&wdt_config);
  if (wdt_err == ESP_OK) {
    esp_task_wdt_add(NULL);  // NULL untuk current/main task
    logActivity("WDT INIT OK");
  } else {
    Serial.printf("[WDT] Init Failed: %d\n", wdt_err);
  }

  // OLED
  Wire.begin(21, 22);
  if (u8g2.begin()) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "BOOTING...");
    u8g2.sendBuffer();
    logActivity("OLED OK");
  }

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount Failed");
  }
  logActivity("SPIFFS OK");

  // WiFi
  logActivity("WIFI CONNECT");
  WiFi.begin(ssid, password);
  int wifi_timeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_timeout < 20000) {
    delay(500);
    Serial.print(".");
    wifi_timeout += 500;
    if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();  // Feed jika registered
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n...[%lu ms] ACTIVITY: WIFI OK: %s\n", millis() - boot_time, WiFi.localIP().toString().c_str());
  }

  // I2S Legacy Init (base untuk Audio lib)
  logActivity("I2S TX INIT: Legacy mode for MAX98357A");
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(15);  // 0-21
  logActivity("I2S TX READY");

  // MQTT Setup
  logActivity("MQTT INIT");
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);

  // Update OLED
  u8g2.clearBuffer();
  u8g2.drawStr(0, 15, "READY! MQTT...");
  u8g2.sendBuffer();

  Serial.println("Setup Complete - Waiting for MQTT commands...");
}

// Loop
void loop() {
  // Feed WDT
  if (esp_task_wdt_status(NULL) == ESP_OK) {
    esp_task_wdt_reset();
  } else {
    Serial.println("[WDT] Task not subscribed - Skipping reset");
  }

  // MQTT Handle
  if (!mqtt_client.connected()) {
    mqttReconnect();
  }
  mqtt_client.loop();

  // Audio Handle (non-blocking)
  audio.loop();

  // OLED Status Update
  static unsigned long last_oled_update = 0;
  if (millis() - last_oled_update > 5000) {  // Update every 5s
    u8g2.clearBuffer();
    u8g2.drawStr(0, 15, WiFi.localIP().toString().c_str());
    if (mqtt_connected) u8g2.drawStr(0, 30, "MQTT: OK");
    u8g2.sendBuffer();
    last_oled_update = millis();
  }

  delay(100);  // Short delay
}
