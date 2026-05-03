#include <Adafruit_NeoPixel.h>

#include "../../lcdwiki_es3c28p_config.h"

Adafruit_NeoPixel pixel(
  LCDWIKI_ES3C28P_RGB_COUNT,
  LCDWIKI_ES3C28P_RGB_PIN,
  NEO_GRB + NEO_KHZ800
);

void setup() {
  pixel.begin();
  pixel.setBrightness(32);
  pixel.setPixelColor(0, pixel.Color(0, 32, 8));
  pixel.show();
}

void loop() {
  static uint8_t hue = 0;
  pixel.setPixelColor(0, pixel.ColorHSV(static_cast<uint16_t>(hue) * 256));
  pixel.show();
  hue++;
  delay(25);
}
