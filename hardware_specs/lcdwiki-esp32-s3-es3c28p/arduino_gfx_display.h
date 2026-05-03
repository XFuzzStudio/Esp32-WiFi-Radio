#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "lcdwiki_es3c28p_config.h"

class LcdWikiEs3c28pDisplay {
public:
  bool begin() {
    pinMode(LCDWIKI_ES3C28P_LCD_BL, OUTPUT);
    digitalWrite(LCDWIKI_ES3C28P_LCD_BL, LCDWIKI_ES3C28P_LCD_BL_ON);

    bus_ = new Arduino_HWSPI(
      LCDWIKI_ES3C28P_LCD_DC,
      LCDWIKI_ES3C28P_LCD_CS,
      LCDWIKI_ES3C28P_LCD_SCK,
      LCDWIKI_ES3C28P_LCD_MOSI,
      LCDWIKI_ES3C28P_LCD_MISO
    );

    gfx_ = new Arduino_ILI9341(
      bus_,
      LCDWIKI_ES3C28P_LCD_RST,
      LCDWIKI_ES3C28P_DISPLAY_ROTATION,
      false,
      LCDWIKI_ES3C28P_SCREEN_WIDTH,
      LCDWIKI_ES3C28P_SCREEN_HEIGHT
    );

    if (!gfx_->begin(LCDWIKI_ES3C28P_SPI_FREQUENCY)) {
      return false;
    }

    gfx_->setRotation(LCDWIKI_ES3C28P_DISPLAY_ROTATION);
    gfx_->invertDisplay(LCDWIKI_ES3C28P_DISPLAY_INVERT_COLORS != 0);
    gfx_->fillScreen(0x0000);
    return true;
  }

  Arduino_GFX *gfx() {
    return gfx_;
  }

  void setBacklight(bool enabled) {
    digitalWrite(
      LCDWIKI_ES3C28P_LCD_BL,
      enabled ? LCDWIKI_ES3C28P_LCD_BL_ON : LCDWIKI_ES3C28P_LCD_BL_OFF
    );
  }

private:
  Arduino_DataBus *bus_ = nullptr;
  Arduino_GFX *gfx_ = nullptr;
};
