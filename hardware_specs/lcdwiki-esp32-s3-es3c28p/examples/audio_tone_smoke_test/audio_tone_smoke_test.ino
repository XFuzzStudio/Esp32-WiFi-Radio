#include "../../audio_es8311_minimal.h"

LcdWikiEs8311Audio audio;

void setup() {
  audio.begin();
}

void loop() {
  audio.playToneBlocking(880, 120);
  delay(350);
}
