#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESP_I2S.h>
#include <Wire.h>

#include "../hardware_specs/lcdwiki-esp32-s3-es3c28p/lcdwiki_es3c28p_config.h"

#define DEBUG_PORT Serial0

static constexpr int8_t DEBUG_UART_RX = 44;
static constexpr int8_t DEBUG_UART_TX = 43;
static constexpr uint32_t PROBE_RATE = 16000;
static constexpr uint16_t PROBE_FREQ = 1000;
static constexpr int16_t PROBE_AMPLITUDE = 16000;

Adafruit_NeoPixel pixel(
  LCDWIKI_ES3C28P_RGB_COUNT,
  LCDWIKI_ES3C28P_RGB_PIN,
  NEO_GRB + NEO_KHZ800
);
I2SClass i2s;

bool toneEnabled = true;
bool i2sReady = false;
uint32_t probeRate = PROBE_RATE;
uint8_t probeBits = 16;
bool espBclkInv = false;
bool espWsInv = false;
bool espMclkInv = false;
uint32_t totalBytes = 0;
uint32_t totalFrames = 0;
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

void dumpCodec() {
  DEBUG_PORT.println("ES8311 full register dump:");
  for (uint8_t reg = 0; reg < 0x4A; reg++) {
    uint8_t value = 0;
    if (readReg(reg, value)) {
      delay(1);
    }
  }
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

uint8_t bitsCode(uint8_t bits) {
  switch (bits) {
    case 16:
      return 3 << 2;
    case 18:
      return 2 << 2;
    case 20:
      return 1 << 2;
    case 24:
      return 0 << 2;
    case 32:
      return 4 << 2;
    default:
      return 3 << 2;
  }
}

bool setCodecBits(uint8_t bits) {
  uint8_t reg09 = 0;
  uint8_t reg0A = 0;
  if (!readReg(0x09, reg09) || !readReg(0x0A, reg0A)) {
    return false;
  }
  const uint8_t code = bitsCode(bits);
  reg09 = (reg09 & static_cast<uint8_t>(~0x1C)) | code;
  reg0A = (reg0A & static_cast<uint8_t>(~0x1C)) | code;
  const bool ok = writeReg(0x09, reg09) & writeReg(0x0A, reg0A);
  logf("ES8311 codec bits=%u %s", bits, ok ? "ok" : "FAILED");
  return ok;
}

bool setCodecSclkInverted(bool inverted) {
  uint8_t reg06 = 0;
  if (!readReg(0x06, reg06)) {
    return false;
  }
  if (inverted) {
    reg06 |= 0x20;
  } else {
    reg06 &= static_cast<uint8_t>(~0x20);
  }
  const bool ok = writeReg(0x06, reg06);
  logf("ES8311 SCLK invert=%d %s", inverted ? 1 : 0, ok ? "ok" : "FAILED");
  return ok;
}

bool initCodec16k() {
  Wire.begin(LCDWIKI_ES3C28P_TOUCH_SDA, LCDWIKI_ES3C28P_TOUCH_SCL);
  Wire.setClock(LCDWIKI_ES3C28P_TOUCH_I2C_FREQUENCY);

  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  if (Wire.endTransmission() != 0) {
    logf("ES8311 not detected at 0x%02X", LCDWIKI_ES3C28P_AUDIO_ADDR);
    return false;
  }
  logf("ES8311 detected at 0x%02X", LCDWIKI_ES3C28P_AUDIO_ADDR);

  bool ok = true;
  ok &= writeReg(0x00, 0x1F);
  delay(20);
  ok &= writeReg(0x00, 0x00);
  ok &= writeReg(0x00, 0x80);
  ok &= writeReg(0x01, 0x3F);

  // 16 kHz, MCLK = 4.096 MHz, 256fs, 16-bit I2S.
  ok &= writeReg(0x02, 0x00);
  ok &= writeReg(0x03, 0x10);
  ok &= writeReg(0x04, 0x10);
  ok &= writeReg(0x05, 0x00);
  ok &= writeReg(0x06, 0x03);
  ok &= writeReg(0x07, 0x00);
  ok &= writeReg(0x08, 0xFF);

  uint8_t reg00 = 0x80;
  if (readReg(0x00, reg00)) {
    ok &= writeReg(0x00, reg00 & 0xBF);
  }

  ok &= writeReg(0x09, 0x0C);
  ok &= writeReg(0x0A, 0x0C);
  ok &= writeReg(0x0D, 0x01);
  ok &= writeReg(0x0E, 0x02);
  ok &= writeReg(0x12, 0x00);
  ok &= writeReg(0x13, 0x10);
  ok &= writeReg(0x1C, 0x6A);
  ok &= writeReg(0x37, 0x08);
  ok &= writeReg(0x32, 0xF0);
  setMuted(false);

  logf("ES8311 init %s", ok ? "ok" : "FAILED");
  return ok;
}

void resetCounters() {
  totalBytes = 0;
  totalFrames = 0;
  phase = 0;
}

bool initI2S() {
  i2s.end();
  delay(20);
  i2s.setInverted(espBclkInv, espWsInv, espMclkInv);
  i2s.setPins(
    LCDWIKI_ES3C28P_AUDIO_BCLK,
    LCDWIKI_ES3C28P_AUDIO_LRCK,
    LCDWIKI_ES3C28P_AUDIO_DOUT,
    -1,
    LCDWIKI_ES3C28P_AUDIO_MCLK
  );
  const i2s_data_bit_width_t width = probeBits == 32
    ? I2S_DATA_BIT_WIDTH_32BIT
    : I2S_DATA_BIT_WIDTH_16BIT;
  const bool ok = i2s.begin(I2S_MODE_STD, probeRate, width, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
  logf("ESP_I2S begin %s, rate=%lu bits=%u bclk=%u lrck=%u dout=%u mclk=%u inv(b/ws/m)=%d/%d/%d",
       ok ? "ok" : "FAILED",
       probeRate,
       probeBits,
       LCDWIKI_ES3C28P_AUDIO_BCLK,
       LCDWIKI_ES3C28P_AUDIO_LRCK,
       LCDWIKI_ES3C28P_AUDIO_DOUT,
       LCDWIKI_ES3C28P_AUDIO_MCLK,
       espBclkInv ? 1 : 0,
       espWsInv ? 1 : 0,
       espMclkInv ? 1 : 0);
  if (ok) {
    resetCounters();
  }
  return ok;
}

void setMode(uint8_t bits, uint32_t rate) {
  probeBits = bits;
  probeRate = rate;
  setCodecBits(probeBits);
  i2sReady = initI2S();
  setMuted(false);
}

void writeToneChunk() {
  const uint32_t halfPeriod = max<uint32_t>(1, probeRate / (PROBE_FREQ * 2));

  if (probeBits == 32) {
    static int32_t buffer[128 * 2];
    for (uint16_t i = 0; i < 128; i++) {
      const int32_t sample = toneEnabled
        ? (((phase / halfPeriod) & 1 ? PROBE_AMPLITUDE : -PROBE_AMPLITUDE) << 16)
        : 0;
      phase++;
      if (phase >= halfPeriod * 2) {
        phase = 0;
      }
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }

    const size_t bytes = i2s.write(reinterpret_cast<const uint8_t *>(buffer), sizeof(buffer));
    totalBytes += bytes;
    totalFrames += bytes / (sizeof(int32_t) * 2);
  } else {
    static int16_t buffer[128 * 2];
    for (uint16_t i = 0; i < 128; i++) {
      const int16_t sample = toneEnabled
        ? ((phase / halfPeriod) & 1 ? PROBE_AMPLITUDE : -PROBE_AMPLITUDE)
        : 0;
      phase++;
      if (phase >= halfPeriod * 2) {
        phase = 0;
      }
      buffer[i * 2] = sample;
      buffer[i * 2 + 1] = sample;
    }

    const size_t bytes = i2s.write(reinterpret_cast<const uint8_t *>(buffer), sizeof(buffer));
    totalBytes += bytes;
    totalFrames += bytes / (sizeof(int16_t) * 2);
  }
}

void printStatus() {
  uint8_t regs[] = {0x00, 0x01, 0x06, 0x09, 0x0A, 0x12, 0x13, 0x31, 0x32, 0x37};
  DEBUG_PORT.printf("STATUS tone=%d i2sReady=%d rate=%lu bits=%u bytes=%lu frames=%lu ampGPIO=%s i2sErr=%d inv(b/ws/m)=%d/%d/%d\n",
                    toneEnabled,
                    i2sReady,
                    probeRate,
                    probeBits,
                    totalBytes,
                    totalFrames,
                    digitalRead(LCDWIKI_ES3C28P_AUDIO_EN) == LOW ? "LOW" : "HIGH",
                    i2s.lastError(),
                    espBclkInv ? 1 : 0,
                    espWsInv ? 1 : 0,
                    espMclkInv ? 1 : 0);
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
  } else if (cmd == "mode16") {
    setMode(16, probeRate);
  } else if (cmd == "mode32") {
    setMode(32, probeRate);
  } else if (cmd == "rate16") {
    setMode(probeBits, 16000);
  } else if (cmd == "rate44") {
    setMode(probeBits, 44100);
  } else if (cmd == "rate48") {
    setMode(probeBits, 48000);
  } else if (cmd == "invbclk") {
    espBclkInv = !espBclkInv;
    i2sReady = initI2S();
  } else if (cmd == "invws") {
    espWsInv = !espWsInv;
    i2sReady = initI2S();
  } else if (cmd == "invmclk") {
    espMclkInv = !espMclkInv;
    i2sReady = initI2S();
  } else if (cmd == "sclk0") {
    setCodecSclkInverted(false);
  } else if (cmd == "sclk1") {
    setCodecSclkInverted(true);
  } else if (cmd == "normal") {
    espBclkInv = false;
    espWsInv = false;
    espMclkInv = false;
    setCodecSclkInverted(false);
    setMode(16, PROBE_RATE);
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "dump") {
    dumpCodec();
  } else if (cmd == "reinit") {
    initCodec16k();
  } else if (cmd == "help") {
    logf("Commands: toneon toneoff mute unmute amp0 amp1 mode16 mode32 rate16 rate44 rate48 invbclk invws invmclk sclk0 sclk1 normal status dump reinit help");
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
  pixelColor(0, 20, 0);

  logf("");
  logf("LCDWiki ES3C28P direct ES8311/ESP_I2S tone probe");
  logf("This sketch bypasses ESP32-audioI2S and streams a direct square wave.");

  pinMode(LCDWIKI_ES3C28P_AUDIO_EN, OUTPUT);
  digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LOW);
  logf("AUDIO_EN GPIO%u LOW", LCDWIKI_ES3C28P_AUDIO_EN);

  const bool codecOk = initCodec16k();
  i2sReady = initI2S();

  if (codecOk && i2sReady) {
    pixelColor(0, 90, 0);
    logf("TONE STARTED: %u Hz square, %lu Hz sample rate", PROBE_FREQ, PROBE_RATE);
  } else {
    pixelColor(90, 0, 0);
  }
  printStatus();
}

void loop() {
  handleSerial();

  if (i2sReady) {
    writeToneChunk();
  }

  const uint32_t now = millis();
  if (now - lastReportMs > 1000) {
    const uint32_t frames = totalFrames;
    static uint32_t lastFrames = 0;
    DEBUG_PORT.printf("TONE probe: tone=%d fps=%lu totalFrames=%lu totalBytes=%lu amp=%s err=%d\n",
                      toneEnabled,
                      frames - lastFrames,
                      totalFrames,
                      totalBytes,
                      digitalRead(LCDWIKI_ES3C28P_AUDIO_EN) == LOW ? "LOW" : "HIGH",
                      i2s.lastError());
    lastFrames = frames;
    lastReportMs = now;
    pixelColor(toneEnabled ? 0 : 25, toneEnabled ? 90 : 25, 0);
  }
}
