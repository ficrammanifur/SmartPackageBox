#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <driver/i2s_std.h>  // New I2S API for ESP-IDF v5+
#include <SPIFFS.h>
#include <HTTPClient.h>

// ================= WIFI & MQTT =================
const char* ssid = "Buat Apa";
const char* password = "syafril1112";

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* command_topic = "package/command";
const char* status_topic  = "package/status";

// ================= PC SERVER =================
const char* pc_ip = "10.88.109.179";
const int pc_port = 5000;

// ================= PIN (FINAL) =================
#define SERVO_PIN     19
#define BUTTON_PIN    4

#define OLED_SDA      21
#define OLED_SCL      22

// I2S SPEAKER (TX)
#define I2S_BCLK      27
#define I2S_LRC       26
#define I2S_DOUT      25

// I2S MIC (RX)
#define I2S_MIC_WS    14
#define I2S_MIC_SCK   33
#define I2S_MIC_SD    32

// ================= GLOBAL =================
WiFiClient espClient;
PubSubClient mqtt(espClient);

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo boxServo;

i2s_chan_handle_t tx_handle = NULL;
i2s_chan_handle_t rx_handle = NULL;

bool i2s_tx_ready = false;
bool i2s_rx_ready = false;

// ================= OLED =================
void oled(const String& msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}

// ================= I2S SPEAKER (TX) =================
void initI2S_TX() {
  // Allocate TX channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
  if (ret != ESP_OK) {
    Serial.printf("I2S TX Alloc failed: %d\n", ret);
    return;
  }

  // Configure for 16kHz, 16-bit stereo TX in Philips (std) mode
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_BCLK,
          .ws = (gpio_num_t)I2S_LRC,
          .dout = (gpio_num_t)I2S_DOUT,
          .din = I2S_GPIO_UNUSED,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };

  // Initialize TX channel
  ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
  if (ret != ESP_OK) {
    Serial.printf("I2S TX Init failed: %d\n", ret);
    return;
  }

  // Enable
  ret = i2s_channel_enable(tx_handle);
  if (ret != ESP_OK) {
    Serial.printf("I2S TX Enable failed: %d\n", ret);
    return;
  }

  i2s_tx_ready = true;
  Serial.println("I2S TX Ready");
}

// ================= I2S MIC (RX) =================
void initI2S_RX() {
  // Allocate RX channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
  if (ret != ESP_OK) {
    Serial.printf("I2S RX Alloc failed: %d\n", ret);
    return;
  }

  // Configure for 16kHz, 16-bit mono RX in Philips (std) mode
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {
          .mclk = I2S_GPIO_UNUSED,
          .bclk = (gpio_num_t)I2S_MIC_SCK,
          .ws = (gpio_num_t)I2S_MIC_WS,
          .dout = I2S_GPIO_UNUSED,
          .din = (gpio_num_t)I2S_MIC_SD,
          .invert_flags = {
              .mclk_inv = false,
              .bclk_inv = false,
              .ws_inv = false,
          },
      },
  };

  // Initialize RX channel
  ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
  if (ret != ESP_OK) {
    Serial.printf("I2S RX Init failed: %d\n", ret);
    return;
  }

  // Enable
  ret = i2s_channel_enable(rx_handle);
  if (ret != ESP_OK) {
    Serial.printf("I2S RX Enable failed: %d\n", ret);
    return;
  }

  i2s_rx_ready = true;
  Serial.println("I2S RX Ready");
}

// ================= PLAY WAV =================
void playWav(const char* path) {
  if (!i2s_tx_ready || tx_handle == NULL) {
    Serial.println("I2S TX not ready");
    return;
  }

  File file = SPIFFS.open(path);
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    return;
  }

  file.seek(44);  // Skip WAV header (RIFF + fmt + data)

  int16_t mono[256];
  int16_t stereo[512];  // Duplicate mono to stereo
  size_t readBytes, written;

  while ((readBytes = file.read((uint8_t*)mono, sizeof(mono))) > 0) {
    for (int i = 0; i < readBytes / 2; i++) {
      stereo[i * 2] = mono[i];
      stereo[i * 2 + 1] = mono[i];
    }
    i2s_channel_write(tx_handle, stereo, readBytes * 2, &written, portMAX_DELAY);
  }

  file.close();
  Serial.println("Audio playback done");
}

// ================= DOWNLOAD AUDIO =================
void downloadAndPlay(String filename) {
  HTTPClient http;
  String url = "http://" + String(pc_ip) + ":" + String(pc_port) + "/audio/" + filename;

  http.begin(url);
  http.setTimeout(10000);  // 10s timeout
  int code = http.GET();

  if (code == 200) {
    File f = SPIFFS.open("/audio.wav", "w");
    if (f) {
      http.writeToStream(&f);
      f.close();
      Serial.println("Audio downloaded");
    } else {
      Serial.println("Failed to save audio");
      http.end();
      return;
    }

    oled("Memutar...");
    playWav("/audio.wav");
    oled("Ready");
  } else {
    Serial.printf("HTTP GET failed: %d\n", code);
  }

  http.end();
}

// ================= MQTT CALLBACK =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (uint32_t i = 0; i < length; i++) msg += (char)payload[i];

  Serial.printf("MQTT: %s -> %s\n", topic, msg.c_str());

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.printf("JSON parse failed: %s\n", error.c_str());
    return;
  }

  String cmd = doc["cmd"] | "";

  if (cmd == "play_audio") {
    String file = doc["file"] | "";
    if (file.length() > 0) {
      downloadAndPlay(file);
    }
  } else if (cmd == "open_box") {
    oled("Box Open");
    boxServo.write(90);
    delay(1000);
    boxServo.write(0);
    oled("Ready");
    mqtt.publish(status_topic, "box_opened");
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);  // Stable boot
  Serial.println("Starting Smart Package ESP32");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
  } else {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
  }
  oled("Booting...");

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    oled("SPIFFS Error");
    return;
  }
  Serial.println("SPIFFS OK");

  // WiFi
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK");
    oled("WiFi OK");
  } else {
    Serial.println("\nWiFi Failed");
    oled("WiFi Failed");
    return;
  }

  // I2S (order matters: TX first)
  initI2S_TX();
  delay(100);  // Settle clocks
  initI2S_RX();

  // Servo
  boxServo.attach(SERVO_PIN);
  boxServo.write(0);  // Closed
  Serial.println("Servo Ready");

  // MQTT
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  oled("Ready");
  Serial.println("Setup complete");
}

// ================= LOOP =================
void loop() {
  if (!mqtt.connected()) {
    String cid = "ESP32-" + String(random(0xffff), HEX);
    if (mqtt.connect(cid.c_str())) {
      mqtt.subscribe(command_topic);
      mqtt.publish(status_topic, "boot_ready");
      Serial.println("MQTT Connected");
    } else {
      Serial.printf("MQTT connect failed: %d\n", mqtt.state());
      delay(5000);
    }
  } else {
    mqtt.loop();
  }

  // Button check (e.g., manual open)
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      oled("Manual Open");
      boxServo.write(90);
      delay(1000);
      boxServo.write(0);
      oled("Ready");
    }
  }

  delay(10);
}
