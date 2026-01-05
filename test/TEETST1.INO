#include <WiFi.h>
#include <PubSubClient.h>  // MQTT
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>  // OLED
#include <ESP32Servo.h>  // Servo
#include <ArduinoJson.h>  // Parse JSON (use static for small size)
#include <driver/i2s.h>   // I2S Mic + Speaker
#include <math.h>         // sin()
#include <FS.h>           // SPIFFS
#include <SPIFFS.h>       // SPIFFS
#include <HTTPClient.h>   // Fallback, but use WiFiClient for streaming

// ================= KONFIG =================
const char* ssid = "Buat Apa";
const char* password = "syafril1112";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_pass = "";
const char* pc_ip = "192.168.1.100";  // Ganti ke IP laptop-mu
const int pc_port = 5000;

// Topics
const char* text_topic = "package/text";  // Legacy
const char* response_topic = "package/response";
const char* status_topic = "package/status";
const char* command_topic = "package/command";  // Dari PC

// Hardware Pins
#define SERVO_PIN 19
#define OLED_SDA 21
#define OLED_SCL 22
#define I2S_BCLK 27
#define I2S_LRC  26
#define I2S_DOUT 25  // Speaker
#define I2S_MIC_WS 35  // Mic LRCLK
#define I2S_MIC_SCK 33  // Mic BCLK
#define I2S_MIC_SD 32   // Mic DATA
#define BUTTON_PIN 0    // Trigger

WiFiClient espClient;
PubSubClient mqtt_client(espClient);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo myServo;
WiFiClient tcp_client;  // For streaming POST

// Globals
unsigned long lastReconnectAttempt = 0;
int reconnectAttempts = 0;
const unsigned long RECONNECT_INTERVAL = 5000;
bool i2s_tx_ready = false;
bool i2s_rx_ready = false;
bool listening = false;
unsigned long listen_start = 0;
const int AUDIO_DURATION_MS = 3000;  // 3s to reduce memory
const int SAMPLE_RATE = 16000;
const int CHUNK_SIZE = 1024;  // Small chunks for streaming

// ================= I2S SETUP =================
void initI2S_TX() {  // Speaker
  Serial.println("Init I2S TX...");
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,  // Reduced
    .dma_buf_len = 512,  // Reduced to save DRAM
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
  if (err != ESP_OK) return;
  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return; }
  err = i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (err != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return; }
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_tx_ready = true;
  Serial.println("I2S TX ready!");
}

void initI2S_RX() {  // Mic
  Serial.println("Init I2S RX...");
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,  // Reduced
    .dma_buf_len = 512,  // Reduced
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };
  esp_err_t err = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  if (err != ESP_OK) return;
  err = i2s_set_pin(I2S_NUM_1, &pin_config);
  if (err != ESP_OK) { i2s_driver_uninstall(I2S_NUM_1); return; }
  err = i2s_set_clk(I2S_NUM_1, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (err != ESP_OK) { i2s_driver_uninstall(I2S_NUM_1); return; }
  i2s_zero_dma_buffer(I2S_NUM_1);
  i2s_rx_ready = true;
  Serial.println("I2S RX ready!");
}

// Stream capture & POST via raw TCP (chunked, no big buffer)
void captureAndStream() {
  if (!i2s_rx_ready || WiFi.status() != WL_CONNECTED) {
    Serial.println("Mic/WiFi not ready");
    return;
  }

  oledUpdate("Mendengarkan");
  listening = true;
  listen_start = millis();

  // Connect TCP
  if (!tcp_client.connect(pc_ip, pc_port)) {
    Serial.println("TCP connect failed");
    listening = false;
    return;
  }

  // Send HTTP POST header
  String headers = "POST /audio_stream HTTP/1.1\r\n";
  headers += "Host: " + String(pc_ip) + ":" + String(pc_port) + "\r\n";
  headers += "Content-Type: audio/pcm\r\n";
  headers += "Content-Length: " + String(AUDIO_DURATION_MS * SAMPLE_RATE * 2 / 1000) + "\r\n";  // Approx 16bit mono
  headers += "Connection: close\r\n\r\n";
  tcp_client.print(headers);

  uint8_t chunk[CHUNK_SIZE];
  size_t bytes_read;
  int total_sent = 0;
  unsigned long start_time = millis();

  while (listening && (millis() - start_time < AUDIO_DURATION_MS)) {
    i2s_read(I2S_NUM_1, chunk, CHUNK_SIZE, &bytes_read, 10 / portTICK_PERIOD_MS);
    if (bytes_read > 0) {
      tcp_client.write(chunk, bytes_read);
      total_sent += bytes_read;
    }
    delay(10);  // Small delay for real-time
  }

  tcp_client.stop();
  listening = false;
  Serial.printf("Streamed %d bytes in %d ms\n", total_sent, millis() - start_time);
  oledUpdate("Siap!");
}

// Play remote WAV (unchanged, but optimized buffer)
void playRemoteWAV(String filename) {
  if (!i2s_tx_ready) return;
  HTTPClient http;
  String url = "http://" + String(pc_ip) + ":" + String(pc_port) + "/audio/" + filename;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.printf("HTTP GET failed: %d\n", httpCode);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  oledUpdate("Menjawab");

  // Skip header
  uint8_t header_skip[44];
  stream->readBytes(header_skip, 44);

  uint8_t buf[512];
  size_t bytes_written;
  int16_t sample_buf[256 * 2];  // Small stereo buffer

  while (http.connected() && stream->available()) {
    size_t bytes_read = stream->readBytes(buf, 512);
    if (bytes_read == 0) break;

    // Mono to stereo (16bit)
    for (size_t i = 0; i < bytes_read / 2; i++) {
      int16_t sample = (buf[i * 2] | (buf[i * 2 + 1] << 8));
      sample_buf[i * 2] = sample;
      sample_buf[i * 2 + 1] = sample;
    }
    size_t stereo_bytes = (bytes_read / 2) * 4;
    i2s_write(I2S_NUM_0, (uint8_t*)sample_buf, stereo_bytes, &bytes_written, portMAX_DELAY);
  }

  http.end();
  oledUpdate("Siap!");
  Serial.println("Remote WAV played");
}

// Play local WAV (boot, small buffer)
void playLocalWAV(const char* filename) {
  if (!i2s_tx_ready || !SPIFFS.exists(filename)) return;

  File file = SPIFFS.open(filename, "r");
  file.seek(44);

  oledUpdate("Speaking...");
  uint8_t buf[512];
  size_t bytes_written;
  int16_t sample_buf[256 * 2];

  while (file.available()) {
    size_t bytes_read = file.read(buf, 512);
    if (bytes_read == 0) break;

    for (size_t i = 0; i < bytes_read / 2; i++) {
      int16_t sample = (buf[i * 2] | (buf[i * 2 + 1] << 8));
      sample_buf[i * 2] = sample;
      sample_buf[i * 2 + 1] = sample;
    }
    size_t stereo_bytes = (bytes_read / 2) * 4;
    i2s_write(I2S_NUM_0, (uint8_t*)sample_buf, stereo_bytes, &bytes_written, portMAX_DELAY);
  }

  file.close();

  // Flush
  memset(sample_buf, 0, sizeof(sample_buf));
  i2s_write(I2S_NUM_0, (uint8_t*)sample_buf, sizeof(sample_buf), &bytes_written, portMAX_DELAY);

  oledUpdate("Siap!");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS failed");
    return;
  }
  Serial.println("SPIFFS ready");

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Starting...");
  display.display();

  myServo.attach(SERVO_PIN);
  myServo.write(0);
  Serial.println("Servo ready");

  WiFi.begin(ssid, password);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 100) {
    delay(100);
    Serial.print(".");
    timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    oledUpdate("WiFi OK");
  } else {
    oledUpdate("WiFi Failed");
    return;
  }

  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  reconnectMQTT();

  initI2S_TX();
  initI2S_RX();

  Serial.println("🚀 ESP32 Ready!");
  oledUpdate("Siap!");
  mqttPublishStatus("boot_ready");

  playLocalWAV("/boot.wav");

  if (mqtt_client.connected()) {
    myServo.write(90);
    delay(1000);
    myServo.write(0);
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(5000);
    return;
  }

  if (!mqtt_client.connected()) {
    if (millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      reconnectMQTT();
    }
  } else {
    reconnectAttempts = 0;
  }
  mqtt_client.loop();

  // Button: Press to capture & stream
  static bool last_button = HIGH;
  bool button = digitalRead(BUTTON_PIN);
  if (button == LOW && last_button == HIGH && !listening) {
    Serial.println("Button pressed - Streaming audio");
    captureAndStream();
  }
  last_button = button;

  delay(50);
}

// ================= MQTT =================
void reconnectMQTT() {
  String clientId = "ESP32Client-" + String(millis());
  int attempts = 0;
  while (!mqtt_client.connected() && attempts < 3) {
    if (mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      mqtt_client.subscribe(response_topic);
      mqtt_client.subscribe(command_topic);
      Serial.println("MQTT Success!");
      oledUpdate("MQTT OK");
      reconnectAttempts = 0;
    } else {
      Serial.printf("MQTT fail rc=%d\n", mqtt_client.state());
      delay(5000);
      attempts++;
      reconnectAttempts++;
    }
  }
  if (reconnectAttempts > 5) oledUpdate("MQTT Error");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("MQTT Cmd: " + msg);

  StaticJsonDocument<512> doc;  // Smaller static
  if (deserializeJson(doc, msg)) return;

  String cmd = doc["cmd"].as<String>();
  if (cmd == "set_status") {
    oledUpdate(doc["state"].as<String>());
  } else if (cmd == "open_box") {
    String name = doc["name"].as<String>();
    oledUpdate("Open: " + name);
    myServo.write(90);
    delay(500);
    mqttPublishStatus("opened");
  } else if (cmd == "play_audio") {
    playRemoteWAV(doc["file"].as<String>());
  } else if (cmd == "sleep") {
    oledUpdate("Sleep");
  }
}

void oledUpdate(String msg) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(msg);
  display.display();
}

void mqttPublishStatus(String status) {
  if (mqtt_client.connected()) {
    mqtt_client.publish(status_topic, status.c_str());
    Serial.println("📤 Status: " + status);
  }
}
