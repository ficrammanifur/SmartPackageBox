#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <math.h>

// ================= CONFIG =================
const char* ssid = "Buat Apa";
const char* password = "syafril1112";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* pc_ip = "192.168.1.100";  // GANTI DENGAN IP PC KAMU (cek ipconfig)
const int pc_port = 5000;

const char* response_topic = "package/response";
const char* command_topic  = "package/command";
const char* status_topic   = "package/status";

// ================= PIN =================
#define SERVO_PIN 19
#define OLED_SDA 21
#define OLED_SCL 22
#define I2S_BCLK 27
#define I2S_LRC 26
#define I2S_DOUT 25
#define I2S_MIC_WS 12    // <-- UBAH: Pin aman
#define I2S_MIC_SCK 13   // <-- UBAH: Pin aman
#define I2S_MIC_SD 15    // <-- UBAH: Pin aman
#define BUTTON_PIN 4     // <-- UBAH: Dari 0 ke 4 (hindari BOOT button noise)


// ================= GLOBAL =================
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
WiFiClient tcp_client;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo myServo;

bool i2s_tx_ready = false;
bool i2s_rx_ready = false;
bool listening = false;
bool mqtt_ready_published = false;

unsigned long lastMQTTAttempt = 0;
const unsigned long MQTT_INTERVAL = 5000;
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_DELAY = 200;

// ================= OLED =================
void oledUpdate(const String& msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

// ================= I2S TX =================
void initI2S_TX() {
  Serial.println("=== Init I2S TX ===");
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  esp_err_t res = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  Serial.printf("I2S TX Install: %s (code %d)\n", esp_err_to_name(res), res);
  if (res != ESP_OK) return;

  res = i2s_set_pin(I2S_NUM_0, &pin);
  Serial.printf("I2S TX Set Pin: %s (code %d)\n", esp_err_to_name(res), res);
  if (res != ESP_OK) return;

  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_tx_ready = true;
  Serial.println("I2S TX Ready");
  Serial.println("=== End Init I2S TX ===");
}

// ================= I2S RX =================
void initI2S_RX() {
  Serial.println("=== Init I2S RX ===");
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  // Step 1: Driver Install
  esp_err_t res = i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  Serial.printf("I2S RX Install: %s (code %d)\n", esp_err_to_name(res), res);
  if (res != ESP_OK) {
    Serial.println("I2S RX Install Failed - Abort");
    return;
  }

  // Step 2: Set Pins
  res = i2s_set_pin(I2S_NUM_1, &pin);
  Serial.printf("I2S RX Set Pin: %s (code %d)\n", esp_err_to_name(res), res);
  if (res != ESP_OK) {
    Serial.println("I2S RX Set Pin Failed - Abort");
    return;
  }

  // Step 3: Zero Buffer
  i2s_zero_dma_buffer(I2S_NUM_1);
  Serial.println("I2S RX Zero Buffer OK");

  i2s_rx_ready = true;
  Serial.println("I2S RX Ready");
  Serial.println("=== End Init I2S RX ===");
}

// ================= BEEP =================
void playBootBeep() {
  if (!i2s_tx_ready) {
    Serial.println("Skip Beep - TX not ready");
    return;
  }

  Serial.println("Playing Boot Beep...");
  int16_t buf[256 * 2];
  size_t written;
  float phase = 0;

  for (int i = 0; i < 50; i++) {
    for (int j = 0; j < 256; j++) {
      int16_t s = sin(phase) * 2000;
      buf[j * 2] = buf[j * 2 + 1] = s;
      phase += 2 * PI * 440 / 16000;
    }
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, 100 / portTICK_PERIOD_MS);
    yield();
  }
  Serial.println("Boot Beep Played");
}

// ================= PLAY WAV FROM SPIFFS =================
void playWav(String path) {
  if (!i2s_tx_ready) {
    Serial.println("Skip Play - TX not ready");
    return;
  }

  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("File not found: " + path);
    return;
  }

  file.seek(44);  // Skip header

  int16_t mono_buffer[256];
  int16_t stereo_buffer[512];
  size_t bytes_read;
  size_t written;

  while ((bytes_read = file.read((uint8_t*)mono_buffer, sizeof(mono_buffer))) > 0) {
    for (int i = 0; i < bytes_read / 2; i++) {
      stereo_buffer[i * 2] = mono_buffer[i];
      stereo_buffer[i * 2 + 1] = mono_buffer[i];
    }
    i2s_write(I2S_NUM_0, stereo_buffer, bytes_read * 2, &written, portMAX_DELAY);
    if (written != bytes_read * 2) break;
  }
  file.close();
  Serial.println("Audio played: " + path);
}

// ================= DOWNLOAD & PLAY AUDIO FROM PC =================
void playAudioFromServer(String filename) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skip Download - WiFi down");
    return;
  }

  Serial.println("Downloading: " + filename);
  HTTPClient http;
  String url = "http://" + String(pc_ip) + ":" + String(pc_port) + "/audio/" + filename;
  http.begin(url);
  http.setTimeout(5000);

  int httpCode = http.GET();
  Serial.printf("HTTP GET: %d\n", httpCode);
  if (httpCode == HTTP_CODE_OK) {
    File audioFile = SPIFFS.open("/audio.wav", "w");
    if (audioFile) {
      http.writeToStream(&audioFile);
      audioFile.close();
      oledUpdate("Memutar...");
      playWav("/audio.wav");
      oledUpdate("Ready");
      Serial.println("Downloaded & Played: " + filename);
    } else {
      Serial.println("SPIFFS write failed");
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }
  http.end();
}

// ================= RECORD & SEND AUDIO TO PC =================
void recordAndSendAudio() {
  Serial.println("=== Start Recording ===");
  if (!i2s_rx_ready) {
    Serial.println("I2S RX not ready - Skip Recording");
    return;
  }

  const int duration = 5;
  const int sample_rate = 16000;
  const int num_samples = sample_rate * duration;
  const int buffer_size = 512;

  uint8_t* audio_buffer = (uint8_t*)malloc(num_samples * 2);
  if (!audio_buffer) {
    Serial.println("Malloc failed - Skip");
    return;
  }

  size_t total_read = 0;
  oledUpdate("Mendengarkan...");

  Serial.println("Recording " + String(duration) + "s...");
  for (int i = 0; i < num_samples / buffer_size; i++) {
    size_t block_read;
    i2s_read(I2S_NUM_1, audio_buffer + total_read, buffer_size * 2, &block_read, 100 / portTICK_PERIOD_MS);
    total_read += block_read;
    if (block_read < buffer_size * 2) {
      Serial.printf("Read block %d: only %d bytes\n", i, block_read);
      break;
    }
    yield();
  }
  Serial.printf("Total recorded: %d bytes\n", total_read);

  // POST to PC
  Serial.println("Sending to PC...");
  HTTPClient http;
  http.begin("http://" + String(pc_ip) + ":" + String(pc_port) + "/audio_stream");
  http.addHeader("Content-Type", "application/octet-stream");

  int httpCode = http.POST(audio_buffer, total_read);
  Serial.printf("HTTP POST: %d\n", httpCode);
  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("PC Response: " + response);
  } else {
    Serial.printf("HTTP POST Failed: %d\n", httpCode);
  }
  http.end();

  free(audio_buffer);
  oledUpdate("Diproses...");
  Serial.println("=== End Recording ===");
}

// ================= MQTT =================
void mqttPublishStatus(const char* status) {
  if (mqtt_client.connected()) {
    mqtt_client.publish(status_topic, status);
    Serial.println("Published Status: " + String(status));
  } else {
    Serial.println("MQTT not connected, skip publish: " + String(status));
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (uint32_t i = 0; i < length; i++) msg += (char)payload[i];

  Serial.println("MQTT Received: " + msg);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.println("JSON parse error: " + String(error.c_str()));
    return;
  }

  String cmd = doc["cmd"] | "";
  Serial.println("MQTT Cmd: " + cmd);
  if (cmd == "open_box") {
    oledUpdate("Box Open");
    myServo.write(90);
    delay(800);
    myServo.write(0);
    mqttPublishStatus("opened");
  } else if (cmd == "set_status") {
    String state = doc["state"] | "";
    oledUpdate(state);
    Serial.println("OLED Updated: " + state);
  } else if (cmd == "play_audio") {
    String file = doc["file"] | "";
    playAudioFromServer(file);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);  // Stabilkan Serial
  Serial.println("\n=== ESP32 Boot ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button Pin " + String(BUTTON_PIN) + " Setup");

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAIL");
    while (1);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  oledUpdate("Booting...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS OK");

  // WiFi
  WiFi.disconnect(true, true);
  delay(500);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ WiFi FAILED");
    oledUpdate("WiFi FAIL");
    return;
  }

  Serial.print("\n✅ WiFi Connected: ");
  Serial.println(WiFi.localIP());
  oledUpdate("WiFi OK");
  delay(500);

  // I2S
  initI2S_TX();
  delay(200);
  if (i2s_tx_ready) playBootBeep();

  initI2S_RX();  // <-- Ini yang debug sekarang
  delay(200);

  // Servo
  myServo.attach(SERVO_PIN);
  myServo.write(0);
  Serial.println("Servo Attached");

  // MQTT
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);

  oledUpdate("Connecting MQTT...");
  Serial.println("Setup Complete - Entering Loop");
  Serial.println("=== End Boot ===");
}

// ================= LOOP =================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected - Reconnecting...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  if (!mqtt_client.connected()) {
    if (millis() - lastMQTTAttempt > MQTT_INTERVAL) {
      lastMQTTAttempt = millis();
      String clientId = "ESP32Client-" + String(random(0xffff), HEX);
      if (mqtt_client.connect(clientId.c_str())) {
        mqtt_client.subscribe(command_topic);
        Serial.println("✅ MQTT Connected & Subscribed");
        oledUpdate("MQTT OK");
        if (!mqtt_ready_published) {
          delay(500);  // Stabilkan
          mqttPublishStatus("boot_ready");
          mqtt_ready_published = true;
        }
      } else {
        Serial.print("MQTT Failed state: ");
        Serial.println(mqtt_client.state());
      }
    }
  } else {
    mqtt_client.loop();
  }

  // Button
  static bool lastBtn = HIGH;
  bool btn = digitalRead(BUTTON_PIN);
  if (btn == LOW && lastBtn == HIGH && (millis() - lastButtonPress > DEBOUNCE_DELAY)) {
    Serial.println("Button Pressed - Start Recording (Debounced)");
    lastButtonPress = millis();
    recordAndSendAudio();
  }
  lastBtn = btn;

  delay(10);
  yield();
}
