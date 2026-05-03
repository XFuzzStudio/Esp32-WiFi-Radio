#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "lcdwiki_es3c28p_config.h"

class LcdWikiFt6336Touch {
public:
  bool begin(TwoWire &wire = Wire) {
    wire_ = &wire;

    pinMode(LCDWIKI_ES3C28P_TOUCH_INT, INPUT_PULLUP);
    pinMode(LCDWIKI_ES3C28P_TOUCH_RST, OUTPUT);

    digitalWrite(LCDWIKI_ES3C28P_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(LCDWIKI_ES3C28P_TOUCH_RST, HIGH);
    delay(120);

    wire_->begin(LCDWIKI_ES3C28P_TOUCH_SDA, LCDWIKI_ES3C28P_TOUCH_SCL);
    wire_->setClock(LCDWIKI_ES3C28P_TOUCH_I2C_FREQUENCY);

    return readRegister(0xA8) || readRegister(0xA3);
  }

  bool touched() {
    if (!writeRegisterPointer(0x02)) {
      return false;
    }

    if (wire_->requestFrom(LCDWIKI_ES3C28P_TOUCH_ADDR, 1) != 1) {
      return false;
    }

    return (wire_->read() & 0x0F) > 0;
  }

  bool read(uint16_t &x, uint16_t &y) {
    if (!writeRegisterPointer(0x02)) {
      return false;
    }

    if (wire_->requestFrom(LCDWIKI_ES3C28P_TOUCH_ADDR, 5) != 5) {
      return false;
    }

    const uint8_t points = wire_->read() & 0x0F;
    if (points == 0) {
      return false;
    }

    const uint8_t xh = wire_->read();
    const uint8_t xl = wire_->read();
    const uint8_t yh = wire_->read();
    const uint8_t yl = wire_->read();

    x = static_cast<uint16_t>(((xh & 0x0F) << 8) | xl);
    y = static_cast<uint16_t>(((yh & 0x0F) << 8) | yl);

    return x < LCDWIKI_ES3C28P_SCREEN_WIDTH && y < LCDWIKI_ES3C28P_SCREEN_HEIGHT;
  }

private:
  TwoWire *wire_ = &Wire;

  bool writeRegisterPointer(uint8_t reg) {
    wire_->beginTransmission(LCDWIKI_ES3C28P_TOUCH_ADDR);
    wire_->write(reg);
    return wire_->endTransmission(true) == 0;
  }

  bool readRegister(uint8_t reg) {
    if (!writeRegisterPointer(reg)) {
      return false;
    }
    if (wire_->requestFrom(LCDWIKI_ES3C28P_TOUCH_ADDR, 1) != 1) {
      return false;
    }
    (void)wire_->read();
    return true;
  }
};
