#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>

#include "lcdwiki_es3c28p_config.h"

class LcdWikiEs8311Audio {
public:
  bool begin(TwoWire &wire = Wire, uint32_t sampleRate = LCDWIKI_ES3C28P_AUDIO_SAMPLE_RATE) {
    if (ready_) {
      return true;
    }

    wire_ = &wire;
    sampleRate_ = sampleRate;

    wire_->begin(LCDWIKI_ES3C28P_TOUCH_SDA, LCDWIKI_ES3C28P_TOUCH_SCL);
    wire_->setClock(LCDWIKI_ES3C28P_TOUCH_I2C_FREQUENCY);

    pinMode(LCDWIKI_ES3C28P_AUDIO_EN, OUTPUT);
    digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LCDWIKI_ES3C28P_AUDIO_EN_ACTIVE);
    delay(10);

    i2s_.setPins(
      LCDWIKI_ES3C28P_AUDIO_BCLK,
      LCDWIKI_ES3C28P_AUDIO_LRCK,
      LCDWIKI_ES3C28P_AUDIO_DOUT,
      -1,
      LCDWIKI_ES3C28P_AUDIO_MCLK
    );

    if (!i2s_.begin(I2S_MODE_STD, sampleRate_, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
      return false;
    }

    writeReg(0x00, 0x1F);
    delay(20);
    writeReg(0x00, 0x00);
    writeReg(0x00, 0x80);

    writeReg(0x01, 0x3F);
    writeReg(0x02, 0x00);
    writeReg(0x03, 0x10);
    writeReg(0x04, 0x10);
    writeReg(0x05, 0x00);
    writeReg(0x06, 0x03);
    writeReg(0x07, 0x00);
    writeReg(0x08, 0xFF);

    uint8_t reg00 = 0x80;
    readReg(0x00, reg00);
    writeReg(0x00, reg00 & 0xBF);
    writeReg(0x09, 0x0C);
    writeReg(0x0A, 0x0C);

    writeReg(0x0D, 0x01);
    writeReg(0x0E, 0x02);
    writeReg(0x12, 0x00);
    writeReg(0x13, 0x10);
    writeReg(0x1C, 0x6A);
    writeReg(0x37, 0x08);
    writeReg(0x32, 0xB8);

    ready_ = true;
    setMuted(false);
    return true;
  }

  void setMuted(bool muted) {
    if (!ready_) {
      return;
    }

    uint8_t reg31 = 0;
    readReg(0x31, reg31);
    if (muted) {
      reg31 |= 0x60;
    } else {
      reg31 &= static_cast<uint8_t>(~0x60);
    }
    writeReg(0x31, reg31);
  }

  void playToneBlocking(uint32_t freq, uint16_t durationMs) {
    if (!begin()) {
      return;
    }

    const uint16_t amplitude = (freq == 0) ? 0 : 2400;
    const uint32_t phaseInc = (freq == 0)
      ? 0
      : static_cast<uint32_t>((static_cast<uint64_t>(freq) << 32) / sampleRate_);
    uint32_t samplesLeft = (sampleRate_ * static_cast<uint32_t>(durationMs)) / 1000;
    int16_t buffer[64 * 2];

    setMuted(freq == 0);

    while (samplesLeft > 0) {
      const uint32_t n = samplesLeft > 64 ? 64 : samplesLeft;
      for (uint32_t i = 0; i < n; i++) {
        const int16_t sample = amplitude == 0
          ? 0
          : ((phase_ & 0x80000000UL) ? amplitude : -amplitude);
        phase_ += phaseInc;
        buffer[i * 2] = sample;
        buffer[i * 2 + 1] = sample;
      }
      i2s_.write(reinterpret_cast<const uint8_t *>(buffer), n * 2 * sizeof(int16_t));
      samplesLeft -= n;
    }

    if (freq != 0) {
      memset(buffer, 0, sizeof(buffer));
      i2s_.write(reinterpret_cast<const uint8_t *>(buffer), sizeof(buffer));
    }
  }

private:
  I2SClass i2s_;
  TwoWire *wire_ = &Wire;
  bool ready_ = false;
  uint32_t sampleRate_ = LCDWIKI_ES3C28P_AUDIO_SAMPLE_RATE;
  uint32_t phase_ = 0;

  bool writeReg(uint8_t reg, uint8_t value) {
    wire_->beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
  }

  bool readReg(uint8_t reg, uint8_t &value) {
    wire_->beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
    wire_->write(reg);
    if (wire_->endTransmission() != 0) {
      return false;
    }

    if (wire_->requestFrom(static_cast<int>(LCDWIKI_ES3C28P_AUDIO_ADDR), 1) != 1) {
      return false;
    }

    value = wire_->read();
    return true;
  }
};
