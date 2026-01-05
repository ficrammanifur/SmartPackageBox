#include <WiFi.h>
#include <PubSubClient.h>  // MQTT
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>  // OLED
#include <ESP32Servo.h>  // Servo
#include <driver/i2s.h>  // I2S native
#include "Audio.h"  // ESP32-audioI2S untuk TTS/STT sederhana

// ================= KONFIG =================
const char* ssid = "WIFI_SSID_KAMU";
const char* password = "WIFI_PASS_KAMU";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";  // Kosong kalau public broker
const char* mqtt_pass = "";

// Topics
const char* text_topic = "package/text";      // Kirim text STT ke laptop
const char* response_topic = "package/response";  // Terima JSON dari Gemini
const char* status_topic = "package/status";  // Publish status (opened/closed)

// Hardware Pins (sesuai deskripsimu)
#define I2S_MIC_WS 35
#define I2S_MIC_SCK 33
#define I2S_MIC_SD 32
#define I2S_SPK_DIN 25
#define I2S_SPK_BCLK 27
#define I2S_SPK_LRC 26
#define SERVO_PIN 19
#define OLED_SDA 21
#define OLED_SCL 22

// Objek
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo myServo;
Audio audio;  // Untuk TTS (play MP3/WAV dari SD atau generate TTS – butuh SD card untuk file audio)

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected!");
  
  // MQTT
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  reconnectMQTT();
  
  // OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Smart Box Ready");
  display.display();
  
  // Servo
  myServo.attach(SERVO_PIN);
  myServo.write(0);  // Tutup box (0 derajat)
  
  // I2S Mic Setup (STT sederhana – deteksi suara, kirim ke laptop via HTTP atau MQTT)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &(i2s_pin_config_t){
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  });
  
  // I2S Speaker Setup (TTS)
  audio.setPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN);
  audio.setVolume(15);  // 0-21
  
  Serial.println("🚀 ESP32 Ready!");
  oledUpdate("Siap Terima Paket");
  
  // Mulai flow: Tanya nama
  ttsSpeak("Permisi, ada paket. Atas nama siapa?");
}

void loop() {
  if (!mqtt_client.connected()) {
    reconnectMQTT();
  }
  mqtt_client.loop();
  
  // Deteksi suara & STT (sederhana: ambil sample audio, konversi ke text via threshold atau kirim ke cloud STT)
  // Untuk STT full, integrasikan ESP-SR library atau kirim audio ke Google STT via HTTP (tapi butuh WiFi stable).
  // Contoh sederhana: Asumsi trigger button atau voice detect, lalu listen & kirim text manual dulu.
  delay(100);
}

// ================= MQTT =================
void reconnectMQTT() {
  while (!mqtt_client.connected()) {
    if (mqtt_client.connect("ESP32Client", mqtt_user, mqtt_pass)) {
      mqtt_client.subscribe(response_topic);
      Serial.println("MQTT connected!");
    } else {
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("Response dari Gemini: " + message);
  
  // Parse JSON (sederhana, pakai ArduinoJson library kalau perlu)
  // Contoh: if (message.indexOf("\"action\":\"open\"") > 0) { ... }
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, message);
  
  String action = doc["action"];
  String name = doc["name"];
  String speak_msg = doc["message"] | "";
  
  if (action == "open") {
    oledUpdate("Buka untuk: " + name);
    servoOpen();
    mqttPublishStatus("opened");
    ttsSpeak("Selamat! Paket untuk " + name + ". Kotak terbuka.");
    
    // Tanya selesai
    delay(2000);
    ttsSpeak("Sudah selesai ambil paket?");
    // Listen reply (implement STT di sini)
    String reply = listenSTT();  // Fungsi custom STT
    if (reply.indexOf("sudah") >= 0 || reply.indexOf("ya") >= 0) {
      servoClose();
      ttsSpeak("Terima kasih!");
    } else {
      servoClose();
      ttsSpeak("Waktu habis, kotak ditutup.");
    }
    
  } else if (action == "deny") {
    oledUpdate("Akses Ditolak");
    ttsSpeak(speak_msg);
    
  } else if (action == "ask_name") {
    oledUpdate("Tanya Nama");
    ttsSpeak(speak_msg);
  }
  
  // Kirim text STT ke laptop via MQTT
  void sendTextToLaptop(String text) {
    mqtt_client.publish(text_topic, text.c_str());
  }
}

// ================= HARDWARE FUNCTIONS =================
void oledUpdate(String msg) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(msg);
  display.display();
}

void servoOpen() {
  myServo.write(90);  // Buka (90 derajat)
  delay(500);
}

void servoClose() {
  myServo.write(0);   // Tutup
  delay(500);
  mqttPublishStatus("closed");
}

void mqttPublishStatus(String status) {
  mqtt_client.publish(status_topic, status.c_str());
}

void ttsSpeak(String text) {
  // Untuk TTS full, generate MP3 via Google TTS API & play via audio.connecttohost("http://url.mp3")
  // Sementara: Pakai pre-recorded WAV di SD card, atau text-to-speech sederhana.
  // Contoh: audio.connecttoFS(SD, "/hello.wav"); audio.loop();
  Serial.println("TTS: " + text);  // Placeholder – ganti dengan audio.play()
  // Implement: Gunakan ESP32-audioI2S untuk stream TTS dari URL (butuh WiFi).
}

// STT Sederhana (placeholder – expand dengan ESP-SR atau Google Speech API)
String listenSTT() {
  // Ambil sample dari I2S mic, threshold untuk detect word.
  // Contoh: size_t bytesRead; int16_t sample[128]; i2s_read(I2S_NUM_0, sample, sizeof(sample), &bytesRead, portMAX_DELAY);
  // Analisis sample untuk keyword (sudah/ya).
  // Return "sudah" atau "".
  delay(2000);  // Simulate listen
  return "sudah";  // Dummy
}
