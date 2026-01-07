/*
 * SMART PACKAGE ESP32 v3 (SLIMMED: MQTT + AUDIO PLAY FOR TTS)
 * Optimasi Size: Hapus unused, config Audio buat WAV only. Size ~1.2-1.4MB.
 * Library: PubSubClient, ESP32-audioI2S (WAV only), ArduinoJson (minimal), HTTPClient, WiFi, U8g2 (opsional).
 * Install: Library Manager, pilih ESP32-audioI2S v1.0+.
 * Partition: Huge APP (3MB) recommended.
 */

#include <WiFi.h>
#include <esp_task_wdt.h>
#include <Wire.h>
#include <U8g2lib.h>  // Comment kalau gak butuh OLED
#include <PubSubClient.h>
#include <Audio.h>     // ESP32-audioI2S
#include <ArduinoJson.h>  // Minimal: 6.x atau 7.x
#include <HTTPClient.h>

// Defines buat slim Audio: Disable decoder non-WAV
#define USE_WAV_DECODER 1
// #define USE_MP3_DECODER 0  // Uncomment & set 0 buat disable MP3 dll (edit library kalau perlu)

// WiFi
const char* ssid = "Buat Apa";
const char* password = "syafril1112";

// MQTT
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_status_topic = "package/status";
const char* mqtt_command_topic = "package/command";
const char* mqtt_client_id = "ESP32_Package_01";

// Server URL
const String server_ip = "10.88.109.179";
const int server_port = 5000;
const String audio_base_url = "http://" + server_ip + ":" + String(server_port) + "/audio/";

// OLED (opsional)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// I2S Pins
#define I2S_DOUT 25
#define I2S_BCLK 27
#define I2S_LRC  26

// Objects
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
Audio audio;

// States
bool mqtt_connected = false;
bool boot_ready_sent = false;
unsigned long boot_time = 0;

// Log sederhana (kurangi printf buat size)
void logActivity(const char* msg) {
  unsigned long t = millis() - boot_time;
  Serial.printf("[%lu] %s\n", t, msg);
}

// MQTT Callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("[MQTT] %s: %s\n", topic, msg.c_str());

  if (String(topic) == mqtt_command_topic) {
    DynamicJsonDocument doc(512);  // Smaller buffer
    DeserializationError err = deserializeJson(doc, msg);
    if (err) return;
    if (doc["cmd"] == "play_audio") {
      String file = doc["file"].as<String>();
      String url = audio_base_url + file;
      Serial.printf("[AUDIO] Play: %s\n", url.c_str());
      audio.connecttohost(url.c_str());
      // OLED update (opsional)
      // u8g2.clearBuffer(); u8g2.drawStr(0,15, ("Play: "+file).c_str()); u8g2.sendBuffer();
    }
  }
}

// MQTT Reconnect
void mqttReconnect() {
  if (mqtt_client.connected()) return;
  Serial.print("[MQTT] Connect...");
  if (mqtt_client.connect(mqtt_client_id)) {
    Serial.println("OK");
    mqtt_client.subscribe(mqtt_command_topic);
    mqtt_connected = true;
    if (!boot_ready_sent) {
      mqtt_client.publish(mqtt_status_topic, "boot_ready");
      boot_ready_sent = true;
      logActivity("BOOT READY SENT");
    }
  } else {
    Serial.println("Failed");
  }
}

void setup() {
  Serial.begin(115200);
  boot_time = millis();
  logActivity("BOOT START");

  // WDT Slim
  esp_task_wdt_config_t cfg = {.timeout_ms=30000, .idle_core_mask=0, .trigger_panic=true};
  esp_task_wdt_init(&cfg);
  esp_task_wdt_add(NULL);
  logActivity("WDT OK");

  // OLED (comment kalau gak butuh)
  Wire.begin(21, 22);
  u8g2.begin();
  u8g2.drawStr(0,15,"BOOTING...");
  u8g2.sendBuffer();
  logActivity("OLED OK");

  // WiFi
  logActivity("WIFI");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print("."); esp_task_wdt_reset();
  }
  logActivity(("WIFI OK: "+WiFi.localIP().toString()).c_str());

  // Audio Init (Slim: Volume only)
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(15);
  logActivity("AUDIO READY");

  // MQTT
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  logActivity("MQTT INIT");

  u8g2.drawStr(0,15,"READY!");
  u8g2.sendBuffer();
}

void loop() {
  esp_task_wdt_reset();  // Feed

  if (!mqtt_client.connected()) mqttReconnect();
  mqtt_client.loop();
  audio.loop();  // Non-blocking

  // OLED IP update (every 10s, opsional)
  static unsigned long last_update = 0;
  if (millis() - last_update > 10000) {
    u8g2.clearBuffer();
    u8g2.drawStr(0,15, WiFi.localIP().toString().c_str());
    if (mqtt_connected) u8g2.drawStr(0,30,"MQTT:OK");
    u8g2.sendBuffer();
    last_update = millis();
  }

  delay(100);
}
