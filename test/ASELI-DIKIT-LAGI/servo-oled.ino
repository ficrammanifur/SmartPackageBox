#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <driver/i2s.h>

/* ================= OLED ================= */
#define I2C_SDA 22
#define I2C_SCL 21
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(128, 64, &Wire, -1);

/* ================= SERVO ================= */
#define SERVO_PIN 19
Servo myServo;

/* ================= MIC (I2S RX) ================= */
#define MIC_BCLK  27
#define MIC_WS    32
#define MIC_SD    33

/* ================= SPEAKER (I2S TX) ================= */
#define SPK_BCLK  14
#define SPK_LRC   15
#define SPK_DOUT  25

#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPK_PORT I2S_NUM_1

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(500);

  /* ---- I2C ---- */
  Wire.begin(I2C_SDA, I2C_SCL);

  /* ---- OLED ---- */
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED gagal");
    while (1);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ESP32 READY");
  display.display();

  /* ---- SERVO ---- */
  myServo.setPeriodHertz(50);                 // 50Hz
  myServo.attach(SERVO_PIN, 500, 2500);       // pulse aman
  myServo.write(0);

  /* ---- MIC ---- */
  initMic();

  /* ---- SPEAKER ---- */
  initSpeaker();
}

/* ================= LOOP ================= */
void loop() {
  for (int angle = 0; angle <= 180; angle += 2) {
    myServo.write(angle);
    updateOLED(angle);
    delay(25);
  }

  for (int angle = 180; angle >= 0; angle -= 2) {
    myServo.write(angle);
    updateOLED(angle);
    delay(25);
  }
}

/* ================= OLED ================= */
void updateOLED(int angle) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SERVO TEST");

  display.setTextSize(2);
  display.setCursor(0, 25);
  display.print(angle);
  display.print((char)247);
  display.print(" ");

  display.display();
}

/* ================= MIC INIT ================= */
void initMic() {
  i2s_config_t mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t mic_pins = {
    .bck_io_num = MIC_BCLK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_MIC_PORT, &mic_config, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &mic_pins);
}

/* ================= SPEAKER INIT ================= */
void initSpeaker() {
  i2s_config_t spk_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false
  };

  i2s_pin_config_t spk_pins = {
    .bck_io_num = SPK_BCLK,
    .ws_io_num = SPK_LRC,
    .data_out_num = SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_SPK_PORT, &spk_config, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &spk_pins);
}
