/*
 * SMART PACKAGE ESP32 v3.1 (ULTRA SLIM)
 * Target: <1.0MB firmware size
 * Optimizations:
 * - Minimal OLED usage (remove if not needed)
 * - Reduced buffer sizes
 * - Disabled debug output in production
 * - Static memory allocation where possible
 */

#include <WiFi.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <PubSubClient.h>
#include <Audio.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

// ===== SIZE OPTIMIZATION FLAGS =====
#define ENABLE_OLED 1        // Set to 0 to disable OLED (saves ~30KB)
#define ENABLE_DEBUG 0       // Set to 0 to disable Serial debug (saves ~20KB)
#define JSON_BUFFER_SIZE 256 // Reduced from 512

// ===== WiFi Configuration =====
const char* ssid = "Buat Apa";
const char* password = "syafril1112";

// ===== MQTT Configuration =====
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_status_topic = "package/status";
const char* mqtt_command_topic = "package/command";
const char* mqtt_client_id = "ESP32_Package_01";

// ===== Server Configuration =====
const char* server_ip = "10.88.109.179";
const int server_port = 5000;

// ===== I2S Pin Configuration =====
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC  26

// ===== Global Objects =====
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
Audio audio;

#if ENABLE_OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

// ===== State Variables =====
bool boot_ready_sent = false;
unsigned long last_display_update = 0;

// ===== Debug Logging Macro =====
#if ENABLE_DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// ===== Helper Functions =====
void updateDisplay(const char* line1, const char* line2 = nullptr) {
#if ENABLE_OLED
  u8g2.clearBuffer();
  u8g2.drawStr(0, 15, line1);
  if (line2) u8g2.drawStr(0, 30, line2);
  u8g2.sendBuffer();
#endif
}

String buildAudioUrl(const String& filename) {
  String url;
  url.reserve(80); // Pre-allocate to avoid fragmentation
  url = "http://";
  url += server_ip;
  url += ":";
  url += server_port;
  url += "/audio/";
  url += filename;
  return url;
}

// ===== MQTT Callback =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Use static buffer to avoid heap allocation
  static char msgBuffer[JSON_BUFFER_SIZE];
  
  if (length >= JSON_BUFFER_SIZE) {
    DEBUG_PRINTLN("[MQTT] Message too large");
    return;
  }
  
  memcpy(msgBuffer, payload, length);
  msgBuffer[length] = '\0';
  
  DEBUG_PRINTF("[MQTT] %s: %s\n", topic, msgBuffer);

  if (strcmp(topic, mqtt_command_topic) == 0) {
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;
    DeserializationError err = deserializeJson(doc, msgBuffer);
    
    if (err) {
      DEBUG_PRINTLN("[JSON] Parse failed");
      return;
    }
    
    const char* cmd = doc["cmd"];
    if (cmd && strcmp(cmd, "play_audio") == 0) {
      const char* file = doc["file"];
      if (file) {
        String url = buildAudioUrl(file);
        DEBUG_PRINTF("[AUDIO] Playing: %s\n", url.c_str());
        audio.connecttohost(url.c_str());
        updateDisplay("Playing...", file);
      }
    }
  }
}

// ===== MQTT Connection =====
void mqttReconnect() {
  if (mqtt_client.connected()) return;
  
  DEBUG_PRINT("[MQTT] Connecting...");
  
  if (mqtt_client.connect(mqtt_client_id)) {
    DEBUG_PRINTLN("OK");
    mqtt_client.subscribe(mqtt_command_topic);
    
    if (!boot_ready_sent) {
      mqtt_client.publish(mqtt_status_topic, "boot_ready");
      boot_ready_sent = true;
      DEBUG_PRINTLN("[MQTT] Boot ready sent");
    }
  } else {
    DEBUG_PRINTLN("Failed");
  }
}

// ===== Setup =====
void setup() {
#if ENABLE_DEBUG
  Serial.begin(115200);
  delay(100);
  DEBUG_PRINTLN("\n[BOOT] Starting...");
#endif

  // Watchdog Timer
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 30000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  DEBUG_PRINTLN("[WDT] Configured");

#if ENABLE_OLED
  // OLED Initialization
  Wire.begin(21, 22);
  u8g2.begin();
  updateDisplay("Booting...");
  DEBUG_PRINTLN("[OLED] Ready");
#endif

  // WiFi Connection
  DEBUG_PRINTLN("[WiFi] Connecting...");
  updateDisplay("WiFi...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 40) {
    delay(500);
    DEBUG_PRINT(".");
    esp_task_wdt_reset();
    wifi_attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("\n[WiFi] Connected");
    DEBUG_PRINTLN(WiFi.localIP());
    updateDisplay("WiFi OK", WiFi.localIP().toString().c_str());
  } else {
    DEBUG_PRINTLN("\n[WiFi] Failed!");
    updateDisplay("WiFi FAIL");
  }

  // Audio System
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(15); // 0-21
  DEBUG_PRINTLN("[Audio] Ready");

  // MQTT Setup
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  mqtt_client.setKeepAlive(60);
  mqtt_client.setSocketTimeout(15);
  DEBUG_PRINTLN("[MQTT] Configured");

  updateDisplay("Ready!");
  delay(1000);
}

// ===== Main Loop =====
void loop() {
  esp_task_wdt_reset();

  // MQTT Handling
  if (!mqtt_client.connected()) {
    mqttReconnect();
  }
  mqtt_client.loop();

  // Audio Processing (non-blocking)
  audio.loop();

  // Periodic Display Update (every 10 seconds)
  if (millis() - last_display_update > 10000) {
#if ENABLE_OLED
    u8g2.clearBuffer();
    u8g2.drawStr(0, 15, WiFi.localIP().toString().c_str());
    if (mqtt_client.connected()) {
      u8g2.drawStr(0, 30, "MQTT:OK");
    } else {
      u8g2.drawStr(0, 30, "MQTT:--");
    }
    u8g2.sendBuffer();
#endif
    last_display_update = millis();
  }

  delay(100);
}
