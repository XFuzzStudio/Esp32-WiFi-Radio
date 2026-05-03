#pragma once

/*
  Experimental TFT_eSPI setup for LCDWiki ES3C28P.

  TamaFi's tested working display path is Arduino_GFX + Arduino_ILI9341.
  During bring-up, the TFT_eSPI path produced a black screen while backlight
  was active. Keep this file only for experiments or for projects that already
  depend on TFT_eSPI.
*/

#define USER_SETUP_LOADED

#define ILI9341_DRIVER

#define TFT_WIDTH 240
#define TFT_HEIGHT 320

#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS 10
#define TFT_DC 46
#define TFT_RST -1

#define TFT_BL 45
#define TFT_BACKLIGHT_ON 1

#define SPI_FREQUENCY 27000000
#define SPI_READ_FREQUENCY 16000000
#define SPI_TOUCH_FREQUENCY 2500000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// If colors are inverted, use tft.invertDisplay(true) after tft.init().
