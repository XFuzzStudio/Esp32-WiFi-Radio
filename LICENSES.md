# License Notes

This repository's own source code and documentation are released under
GPL-3.0-or-later. The full GPLv3 text is in `LICENSE`.

I chose GPL-3.0-or-later because the radio playback path depends on
`ESP32-audioI2S-master`, which is distributed under GPLv3. Keeping this project
GPL avoids giving users the false impression that finished firmware builds are
permissively licensed.

Third-party dependencies keep their own licenses:

- `ESP32-audioI2S-master` - GPLv3
- `Adafruit NeoPixel` - LGPLv3 or later
- `Adafruit GFX Library` / `Adafruit BusIO` - BSD-style Adafruit licenses
- `GFX Library for Arduino` - see the upstream `Arduino_GFX` project
- `lvgl` - MIT license
- ESP32 Arduino core and bundled ESP-IDF components - see Espressif's upstream
  license notices

The `sd_card/` files are samples only and must not be replaced in git with a
private SD card copy containing Wi-Fi credentials.
