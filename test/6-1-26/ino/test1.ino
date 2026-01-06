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
#include <math.h>

// ================= CONFIG =================
const char* ssid = "Buat Apa";
const char* password = "syafril1112";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* pc_ip = "192.168.1.100";
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
#define I2S_MIC_WS 14
#define I2S_MIC_SCK 33
#define I2S_MIC_SD 32
#define BUTTON_PIN 0

// ================= GLOBAL =================
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
WiFiClient tcp_client;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo myServo;

bool i2s_tx_ready = false;
bool i2s_rx_ready = false;
bool listening = false;

unsigned long lastMQTTAttempt = 0;
const unsigned long MQTT_INTERVAL = 5000;

// ================= OLED =================
void oledUpdate(const String& msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

// ================= I2S TX =================
void initI2S_TX() {
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

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_tx_ready = true;
}

// ================= I2S RX =================
void initI2S_RX() {
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

  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin);
  i2s_zero_dma_buffer(I2S_NUM_1);
  i2s_rx_ready = true;
}

// ================= BEEP =================
void playBootBeep() {
  if (!i2s_tx_ready) return;

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
}

// ================= MQTT =================
void mqttPublishStatus(const char* status) {
  if (mqtt_client.connected()) {
    mqtt_client.publish(status_topic, status);
    Serial.println(status);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (uint32_t i = 0; i < length; i++) msg += (char)payload[i];

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msg)) return;

  String cmd = doc["cmd"] | "";
  if (cmd == "open_box") {
    oledUpdate("Box Open");
    myServo.write(90);
    delay(800);
    myServo.write(0);
    mqttPublishStatus("opened");
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAIL");
    while (1);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  oledUpdate("Booting...");

  SPIFFS.begin(true);

  // Reset WiFi stack
  WiFi.disconnect(true, true);
  delay(500);
  WiFi.mode(WIFI_STA);
  delay(200);

  // WiFi event logging
  WiFi.onEvent([](WiFiEvent_t event) {
    Serial.printf("[WiFi EVENT] %d\n", event);
  });

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

  oledUpdate("WiFi OK");
  delay(500);

  initI2S_TX();
  delay(200);
  playBootBeep();

  initI2S_RX();

  myServo.attach(SERVO_PIN);
  myServo.write(0);

  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);

  oledUpdate("Ready");
  mqttPublishStatus("boot_ready");
}

// ================= LOOP =================
void loop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt_client.connected()) {
    if (millis() - lastMQTTAttempt > MQTT_INTERVAL) {
      lastMQTTAttempt = millis();
      mqtt_client.connect("ESP32Client");
      if (mqtt_client.connected()) {
        mqtt_client.subscribe(command_topic);
        oledUpdate("MQTT OK");
      }
    }
  }

  mqtt_client.loop();

  static bool lastBtn = HIGH;
  bool btn = digitalRead(BUTTON_PIN);

  if (btn == LOW && lastBtn == HIGH) {
    Serial.println("Button Pressed");
  }
  lastBtn = btn;

  delay(10);
  yield();
}
