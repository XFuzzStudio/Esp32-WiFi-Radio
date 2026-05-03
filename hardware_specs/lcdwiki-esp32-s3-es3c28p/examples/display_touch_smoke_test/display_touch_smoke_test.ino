#include "../../arduino_gfx_display.h"
#include "../../touch_ft6336.h"

LcdWikiEs3c28pDisplay display;
LcdWikiFt6336Touch touch;

static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr uint16_t COLOR_BLUE = 0x001F;
static constexpr uint16_t COLOR_GREEN = 0x07E0;
static constexpr uint16_t COLOR_RED = 0xF800;
static constexpr uint16_t COLOR_WHITE = 0xFFFF;

void setup() {
  Serial.begin(115200);

  if (!display.begin()) {
    Serial.println("Display init failed");
    return;
  }

  touch.begin();

  Arduino_GFX *gfx = display.gfx();
  gfx->fillScreen(COLOR_BLACK);
  gfx->fillRect(0, 0, 240, 32, COLOR_BLUE);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(8, 8);
  gfx->print("LCDWiki S3");
  gfx->drawRect(0, 32, 240, 288, COLOR_GREEN);
}

void loop() {
  uint16_t x = 0;
  uint16_t y = 0;
  if (touch.read(x, y)) {
    Arduino_GFX *gfx = display.gfx();
    gfx->fillCircle(x, y, 4, COLOR_RED);
    Serial.printf("touch %u %u\n", x, y);
    delay(35);
  }
}
