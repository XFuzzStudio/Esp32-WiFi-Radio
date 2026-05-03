# LCDWiki ESP32-S3 ES3C28P Board Spec

Copyable hardware profile for the LCDWiki 2.8 inch ESP32-S3 Display touch version
(`ES3C28P`). This folder is meant to be dropped into another Arduino/ESP32
project when the board needs display, touch, NeoPixel, audio, SD, and battery
pin definitions.

## Tested Baseline

- Board: LCDWiki 2.8inch ESP32-S3 Display, touch SKU `ES3C28P`
- Module: ESP32-S3 `N16R8`
- Flash: 16 MB external SPI flash
- PSRAM: 8 MB internal OPI PSRAM
- Display: 2.8 inch IPS TFT, `240x320`, SPI, `ILI9341V`
- Touch: capacitive, I2C, `D-FT6336G`, address `0x38`
- Audio: ES8311 codec/amplifier path, I2S plus enable pin
- Status LED: one addressable RGB LED on a single data pin
- TamaFi tested port: display, touch, one NeoPixel, and simple ES8311 tone output

Source references:

- LCDWiki board page: <https://www.lcdwiki.com/2.8inch_ESP32-S3_Display>
- TamaFi working profile: `TamaFi/hardware_profile.h`

## Recommended Arduino Target

Use the ESP32 Arduino core with ESP32-S3 settings:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" TamaFi
arduino-cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" TamaFi
```

For another project, keep the same board options unless the partition layout has
to change.

## Required Arduino Libraries

- ESP32 Arduino core, preferably 3.x
- `GFX Library for Arduino` by moononournation
- `Adafruit NeoPixel`
- Built-in ESP32 Arduino libraries: `Wire`, `SPI`, `ESP_I2S`

The tested display path is Arduino_GFX with `Arduino_ILI9341`. TFT_eSPI is kept
only as an experimental fallback in this folder because it produced a black
screen during TamaFi bring-up on this exact board.

## Files In This Folder

- `pinout.md` - complete pin table grouped by peripheral
- `lcdwiki_es3c28p_config.h` - project-ready pin and feature macros
- `arduino_gfx_display.h` - minimal Arduino_GFX display helper
- `touch_ft6336.h` - minimal FT6336 I2C touch helper
- `audio_es8311_minimal.h` - minimal ES8311/I2S helper from TamaFi bring-up
- `audio_es8311_notes.md` - ES8311/I2S bring-up notes and register hints
- `tft_espi_user_setup_experimental.h` - TFT_eSPI config for experiments only
- `build_commands.md` - compile/upload commands and board manager notes
- `examples/display_touch_smoke_test` - Arduino_GFX plus FT6336 touch test
- `examples/neopixel_smoke_test` - known-good one-LED NeoPixel smoke test
- `examples/audio_tone_smoke_test` - simple ES8311 tone smoke test

## Bring-Up Order

1. Select ESP32-S3 N16R8 with OPI PSRAM and 16 MB flash.
2. Turn on backlight GPIO45 early.
3. Initialize Arduino_GFX as `Arduino_ILI9341`.
4. Use SPI frequency `27000000` first. Raise it only after the image is stable.
5. Set display rotation to `0`.
6. Enable color inversion with `invertDisplay(true)`.
7. Initialize touch I2C on SDA GPIO16 and SCL GPIO15, then reset FT6336 on GPIO18.
8. Test the NeoPixel on GPIO42 without clearing it every main loop iteration.
9. Enable audio by driving GPIO1 low, then start I2S on pins 4/5/6/7/8.

## Known TamaFi Findings

- Backlight working does not prove the display controller is initialized.
- Arduino_GFX + `Arduino_ILI9341` is the tested path for this board.
- Colors are inverted unless `invertDisplay(true)` is used.
- LCD reset is shared with ESP32-S3 reset, so the usable TFT reset pin is `-1`.
- The active touch panel covers the full `240x320`, but square TamaFi UI used
  only the top `240x240` area.
- The board has one addressable LED. Do not treat it as a strip.
- Audio enable is active low on GPIO1.
