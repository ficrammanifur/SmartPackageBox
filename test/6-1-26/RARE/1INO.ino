#include <WiFi.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include <math.h>

// ================= WIFI =================
const char* ssid = "Buat Apa";
const char* password = "syafril1112";

// ================= SERVER =================
const char* ws_host = "192.168.1.100"; // IP PC
const int ws_port = 8765;
const char* ws_path = "/audio";

// ================= I2S MIC (INMP441) =================
#define MIC_WS   14
#define MIC_SCK  33
#define MIC_SD   32

// ================= I2S SPEAKER (MAX98357A) ============
#define SPK_BCLK 27
#define SPK_LRC  26
#define SPK_DOUT 25

#define SAMPLE_RATE 16000
#define BUF_LEN 1024

WebSocketsClient webSocket;

// ================= STATE =================
enum AudioState { LISTENING, PLAYING };
AudioState audioState = LISTENING;

// ================= I2S INIT =================
void initMic() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pin);
}

void initSpeaker() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false
  };

  i2s_pin_config_t pin = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin);
}

// ================= WEBSOCKET =================
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_BIN) {
    audioState = PLAYING;
    size_t written;
    i2s_write(I2S_NUM_0, payload, length, &written, portMAX_DELAY);
    audioState = LISTENING;
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  initMic();
  initSpeaker();

  webSocket.begin(ws_host, ws_port, ws_path);
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  webSocket.loop();

  if (audioState == LISTENING && webSocket.isConnected()) {
    static int16_t micBuf[BUF_LEN];
    size_t bytesRead;

    i2s_read(I2S_NUM_1, micBuf, sizeof(micBuf), &bytesRead, portMAX_DELAY);
    webSocket.sendBIN((uint8_t*)micBuf, bytesRead);
  }
}
