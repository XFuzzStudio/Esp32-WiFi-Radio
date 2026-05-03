#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <driver/i2s_std.h>

#include "../hardware_specs/lcdwiki-esp32-s3-es3c28p/lcdwiki_es3c28p_config.h"

#define DEBUG_PORT Serial0

static constexpr int8_t DEBUG_UART_RX = 44;
static constexpr int8_t DEBUG_UART_TX = 43;
static constexpr uint32_t SAMPLE_RATE = 24000;
static constexpr uint16_t TONE_HZ = 1000;
static constexpr int16_t TONE_AMPLITUDE = 18000;

Adafruit_NeoPixel pixel(
  LCDWIKI_ES3C28P_RGB_COUNT,
  LCDWIKI_ES3C28P_RGB_PIN,
  NEO_GRB + NEO_KHZ800
);

i2s_chan_handle_t txHandle = nullptr;
bool toneEnabled = true;
uint8_t audioDoutPin = LCDWIKI_ES3C28P_AUDIO_DOUT;
uint8_t audioDinPin = LCDWIKI_ES3C28P_AUDIO_DIN;
uint32_t totalFrames = 0;
uint32_t totalBytes = 0;
uint32_t lastFrames = 0;
uint32_t lastReportMs = 0;
uint32_t phase = 0;
String serialLine;

void logf(const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  DEBUG_PORT.println(buffer);
}

void pixelColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  Wire.write(reg);
  Wire.write(value);
  const bool ok = Wire.endTransmission() == 0;
  DEBUG_PORT.printf("ES8311 W 0x%02X = 0x%02X %s\n", reg, value, ok ? "ok" : "FAIL");
  return ok;
}

bool readReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    DEBUG_PORT.printf("ES8311 R 0x%02X pointer FAIL\n", reg);
    return false;
  }
  if (Wire.requestFrom(static_cast<uint8_t>(LCDWIKI_ES3C28P_AUDIO_ADDR), static_cast<uint8_t>(1)) != 1) {
    DEBUG_PORT.printf("ES8311 R 0x%02X data FAIL\n", reg);
    return false;
  }
  value = Wire.read();
  DEBUG_PORT.printf("ES8311 R 0x%02X = 0x%02X\n", reg, value);
  return true;
}

void setMuted(bool muted) {
  uint8_t reg31 = 0;
  if (!readReg(0x31, reg31)) {
    return;
  }
  if (muted) {
    reg31 |= 0x60;
  } else {
    reg31 &= static_cast<uint8_t>(~0x60);
  }
  writeReg(0x31, reg31);
}

bool initCodec() {
  Wire.begin(LCDWIKI_ES3C28P_TOUCH_SDA, LCDWIKI_ES3C28P_TOUCH_SCL);
  Wire.setClock(LCDWIKI_ES3C28P_TOUCH_I2C_FREQUENCY);

  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  if (Wire.endTransmission() != 0) {
    logf("ES8311 not detected at 0x%02X", LCDWIKI_ES3C28P_AUDIO_ADDR);
    return false;
  }
  logf("ES8311 detected at 0x%02X", LCDWIKI_ES3C28P_AUDIO_ADDR);

  bool ok = true;
  uint8_t reg = 0;

  // Follow Espressif esp_codec_dev ES8311 open -> set_fs -> start sequence.
  if (readReg(0x0D, reg) && reg != 0xFA) {
    ok &= writeReg(0x0D, 0xFA);
  }
  ok &= writeReg(0x44, 0x08);
  ok &= writeReg(0x44, 0x08);
  ok &= writeReg(0x01, 0x30);
  ok &= writeReg(0x02, 0x00);
  ok &= writeReg(0x03, 0x10);
  ok &= writeReg(0x16, 0x24);
  ok &= writeReg(0x04, 0x10);
  ok &= writeReg(0x05, 0x00);
  ok &= writeReg(0x0B, 0x00);
  ok &= writeReg(0x0C, 0x00);
  ok &= writeReg(0x10, 0x1F);
  ok &= writeReg(0x11, 0x7F);
  ok &= writeReg(0x00, 0x80);

  if (readReg(0x00, reg)) {
    ok &= writeReg(0x00, reg & 0xBF);
  }
  ok &= writeReg(0x01, 0x3F);
  if (readReg(0x06, reg)) {
    ok &= writeReg(0x06, reg & static_cast<uint8_t>(~0x20));
  }
  ok &= writeReg(0x13, 0x10);
  ok &= writeReg(0x1B, 0x0A);
  ok &= writeReg(0x1C, 0x6A);
  ok &= writeReg(0x44, 0x58);

  // 24 kHz, MCLK = 6.144 MHz, 256fs, 16-bit Philips I2S.
  ok &= writeReg(0x09, 0x0C);
  ok &= writeReg(0x0A, 0x0C);
  ok &= writeReg(0x02, 0x00);
  ok &= writeReg(0x05, 0x00);
  ok &= writeReg(0x03, 0x10);
  ok &= writeReg(0x04, 0x10);
  ok &= writeReg(0x06, 0x03);
  ok &= writeReg(0x07, 0x00);
  ok &= writeReg(0x08, 0xFF);

  ok &= writeReg(0x00, 0x80);
  ok &= writeReg(0x01, 0x3F);
  if (readReg(0x09, reg)) {
    ok &= writeReg(0x09, reg & static_cast<uint8_t>(~0x40));
  }
  if (readReg(0x0A, reg)) {
    ok &= writeReg(0x0A, reg & static_cast<uint8_t>(~0x40));
  }
  ok &= writeReg(0x17, 0xBF);
  ok &= writeReg(0x0E, 0x02);
  ok &= writeReg(0x12, 0x00);
  ok &= writeReg(0x14, 0x1A);
  if (readReg(0x14, reg)) {
    ok &= writeReg(0x14, reg & static_cast<uint8_t>(~0x40));
  }
  ok &= writeReg(0x0D, 0x01);
  ok &= writeReg(0x15, 0x40);
  ok &= writeReg(0x37, 0x08);
  ok &= writeReg(0x45, 0x00);
  ok &= writeReg(0x32, 0xF0);
  setMuted(false);

  logf("ES8311 esp_codec_dev-style init %s", ok ? "ok" : "FAILED");
  return ok;
}

bool initI2S() {
  if (txHandle) {
    i2s_channel_disable(txHandle);
    i2s_del_channel(txHandle);
    txHandle = nullptr;
    delay(20);
  }

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = 6;
  chanCfg.dma_frame_num = 240;
  chanCfg.auto_clear_after_cb = true;
  chanCfg.auto_clear_before_cb = false;
  chanCfg.intr_priority = 0;

  esp_err_t err = i2s_new_channel(&chanCfg, &txHandle, nullptr);
  if (err != ESP_OK) {
    logf("i2s_new_channel failed: %d", err);
    return false;
  }

  i2s_std_config_t stdCfg = {};
  stdCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  stdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
  stdCfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
  stdCfg.slot_cfg.bit_shift = true;
#if SOC_I2S_HW_VERSION_2
  stdCfg.slot_cfg.left_align = true;
  stdCfg.slot_cfg.big_endian = false;
  stdCfg.slot_cfg.bit_order_lsb = false;
#endif
  stdCfg.gpio_cfg.mclk = static_cast<gpio_num_t>(LCDWIKI_ES3C28P_AUDIO_MCLK);
  stdCfg.gpio_cfg.bclk = static_cast<gpio_num_t>(LCDWIKI_ES3C28P_AUDIO_BCLK);
  stdCfg.gpio_cfg.ws = static_cast<gpio_num_t>(LCDWIKI_ES3C28P_AUDIO_LRCK);
  stdCfg.gpio_cfg.dout = static_cast<gpio_num_t>(audioDoutPin);
  stdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;

  err = i2s_channel_init_std_mode(txHandle, &stdCfg);
  if (err != ESP_OK) {
    logf("i2s_channel_init_std_mode failed: %d", err);
    return false;
  }
  err = i2s_channel_enable(txHandle);
  if (err != ESP_OK) {
    logf("i2s_channel_enable failed: %d", err);
    return false;
  }

  totalFrames = 0;
  totalBytes = 0;
  lastFrames = 0;
  phase = 0;

  logf("IDF I2S ok: port=0 rate=%lu bits=16 bclk=%u lrck=%u dout=%u din=%u mclk=%u",
       SAMPLE_RATE,
       LCDWIKI_ES3C28P_AUDIO_BCLK,
       LCDWIKI_ES3C28P_AUDIO_LRCK,
       audioDoutPin,
       audioDinPin,
       LCDWIKI_ES3C28P_AUDIO_MCLK);
  return true;
}

void writeTone() {
  static int16_t buffer[128 * 2];
  const uint32_t halfPeriod = max<uint32_t>(1, SAMPLE_RATE / (TONE_HZ * 2));

  for (uint16_t i = 0; i < 128; i++) {
    const int16_t sample = toneEnabled
      ? ((phase / halfPeriod) & 1 ? TONE_AMPLITUDE : -TONE_AMPLITUDE)
      : 0;
    phase++;
    if (phase >= halfPeriod * 2) {
      phase = 0;
    }
    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
  }

  size_t bytesWritten = 0;
  const esp_err_t err = i2s_channel_write(txHandle, buffer, sizeof(buffer), &bytesWritten, portMAX_DELAY);
  if (err == ESP_OK) {
    totalBytes += bytesWritten;
    totalFrames += bytesWritten / (sizeof(int16_t) * 2);
  } else {
    logf("i2s_channel_write err=%d", err);
  }
}

void printStatus() {
  DEBUG_PORT.printf("STATUS tone=%d frames=%lu bytes=%lu amp=%s\n",
                    toneEnabled,
                    totalFrames,
                    totalBytes,
                    digitalRead(LCDWIKI_ES3C28P_AUDIO_EN) == LOW ? "LOW" : "HIGH");
  uint8_t regs[] = {0x00, 0x01, 0x06, 0x09, 0x0A, 0x0D, 0x0E, 0x12, 0x13, 0x14, 0x15, 0x17, 0x31, 0x32, 0x37, 0x44, 0x45};
  for (uint8_t i = 0; i < sizeof(regs); i++) {
    uint8_t value = 0;
    if (readReg(regs[i], value)) {
      DEBUG_PORT.printf("  reg 0x%02X = 0x%02X\n", regs[i], value);
    }
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd == "toneon") {
    toneEnabled = true;
    setMuted(false);
    pixelColor(0, 90, 0);
    logf("TONE ON");
  } else if (cmd == "toneoff") {
    toneEnabled = false;
    logf("TONE OFF");
  } else if (cmd == "mute") {
    setMuted(true);
    logf("Muted");
  } else if (cmd == "unmute") {
    setMuted(false);
    logf("Unmuted");
  } else if (cmd == "amp0") {
    digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LOW);
    logf("AUDIO_EN LOW");
  } else if (cmd == "amp1") {
    digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, HIGH);
    logf("AUDIO_EN HIGH");
  } else if (cmd == "dout6") {
    audioDoutPin = LCDWIKI_ES3C28P_AUDIO_DOUT;
    audioDinPin = LCDWIKI_ES3C28P_AUDIO_DIN;
    initI2S();
    setMuted(false);
  } else if (cmd == "dout8") {
    audioDoutPin = LCDWIKI_ES3C28P_AUDIO_DIN;
    audioDinPin = LCDWIKI_ES3C28P_AUDIO_DOUT;
    initI2S();
    setMuted(false);
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "help") {
    logf("Commands: toneon toneoff mute unmute amp0 amp1 dout6 dout8 status help");
  }
}

void handleSerial() {
  while (DEBUG_PORT.available()) {
    const char c = DEBUG_PORT.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 80) {
      serialLine += c;
    }
  }
}

void setup() {
  DEBUG_PORT.begin(115200, SERIAL_8N1, DEBUG_UART_RX, DEBUG_UART_TX);
  delay(100);

  pixel.begin();
  pixel.setBrightness(50);
  pixelColor(0, 25, 0);

  logf("");
  logf("LCDWiki ES3C28P IDF I2S_NUM_0 ES8311 tone probe");

  pinMode(LCDWIKI_ES3C28P_AUDIO_EN, OUTPUT);
  digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LOW);
  logf("AUDIO_EN GPIO%u LOW", LCDWIKI_ES3C28P_AUDIO_EN);

  const bool codecOk = initCodec();
  const bool i2sOk = initI2S();

  if (codecOk && i2sOk) {
    pixelColor(0, 90, 0);
    logf("IDF TONE STARTED: %u Hz square, %lu Hz sample rate", TONE_HZ, SAMPLE_RATE);
  } else {
    pixelColor(90, 0, 0);
  }
  printStatus();
}

void loop() {
  handleSerial();
  if (txHandle) {
    writeTone();
  }

  const uint32_t now = millis();
  if (now - lastReportMs > 1000) {
    DEBUG_PORT.printf("IDF tone: tone=%d fps=%lu totalFrames=%lu totalBytes=%lu amp=%s\n",
                      toneEnabled,
                      totalFrames - lastFrames,
                      totalFrames,
                      totalBytes,
                      digitalRead(LCDWIKI_ES3C28P_AUDIO_EN) == LOW ? "LOW" : "HIGH");
    lastFrames = totalFrames;
    lastReportMs = now;
    pixelColor(toneEnabled ? 0 : 25, toneEnabled ? 90 : 25, 0);
  }
}
