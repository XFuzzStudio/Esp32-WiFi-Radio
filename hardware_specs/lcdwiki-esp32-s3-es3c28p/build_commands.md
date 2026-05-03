# Build And Upload Commands

These commands assume Arduino CLI, ESP32 Arduino core, and the required
libraries are installed.

## Board Core

```powershell
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

## Libraries

```powershell
arduino-cli lib install "GFX Library for Arduino"
arduino-cli lib install "Adafruit NeoPixel"
```

`Wire`, `SPI`, and `ESP_I2S` come from the ESP32 Arduino core.

## Compile

From the parent folder of the sketch:

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" YourSketchFolder
```

## Upload

TamaFi test unit used `COM6` for the LCDWiki ESP32-S3 board:

```powershell
arduino-cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" YourSketchFolder
```

## Serial Monitor

```powershell
arduino-cli monitor -p COM6 -c baudrate=115200
```

## First Smoke Tests

1. Backlight on: GPIO45 high.
2. Display clear: Arduino_GFX `fillScreen(0x0000)`, then draw colored rectangles.
3. Color direction: call `invertDisplay(true)` if colors are wrong.
4. Touch: read FT6336 at `0x38` on SDA GPIO16 / SCL GPIO15.
5. NeoPixel: set LED 0 on GPIO42, `NEO_GRB + NEO_KHZ800`.
6. Audio: GPIO1 low, then I2S on GPIO4/5/6/7/8.

## Example Smoke Tests

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" hardware_specs\lcdwiki-esp32-s3-es3c28p\examples\display_touch_smoke_test
arduino-cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" hardware_specs\lcdwiki-esp32-s3-es3c28p\examples\display_touch_smoke_test

arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" hardware_specs\lcdwiki-esp32-s3-es3c28p\examples\neopixel_smoke_test
arduino-cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" hardware_specs\lcdwiki-esp32-s3-es3c28p\examples\neopixel_smoke_test

arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" hardware_specs\lcdwiki-esp32-s3-es3c28p\examples\audio_tone_smoke_test
arduino-cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" hardware_specs\lcdwiki-esp32-s3-es3c28p\examples\audio_tone_smoke_test
```
