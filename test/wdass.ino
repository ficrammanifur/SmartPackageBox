#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <driver/i2s.h>

/* ================= OLED ================= */
#define I2C_SDA 22
#define I2C_SCL 21
Adafruit_SSD1306 display(128, 64, &Wire, -1);

/* ================= MIC ================= */
#define MIC_BCLK  27
#define MIC_WS    32
#define MIC_SD    33

/* ================= SPEAKER ================= */
#define SPK_BCLK  14
#define SPK_LRC   15
#define SPK_DOUT  25

/* ================= SERVO ================= */
#define SERVO_PIN 19
Servo myServo;

/* ================= I2S ================= */
#define I2S_MIC_PORT I2S_NUM_0
#define I2S_SPK_PORT I2S_NUM_1

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(300);

  /* ---- I2C ---- */
  Wire.begin(I2C_SDA, I2C_SCL);

  /* ---- OLED ---- */
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  /* ---- SERVO ---- */
  myServo.attach(SERVO_PIN);
  myServo.write(0);

  /* ---- MIC ---- */
  initMic();

  /* ---- SPEAKER ---- */
  initSpeaker();

  displayStatus(0);
}

/* ================= LOOP ================= */
void loop() {
  // 0 → 180
  for (int angle = 0; angle <= 180; angle += 5) {
    myServo.write(angle);
    displayStatus(angle);
    delay(40);
  }

  // 180 → 0
  for (int angle = 180; angle >= 0; angle -= 5) {
    myServo.write(angle);
    displayStatus(angle);
    delay(40);
  }
}

/* ================= OLED ================= */
void displayStatus(int angle) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("SERVO TEST");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(angle);
  display.print((char)247); // simbol derajat
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
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
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
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
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
