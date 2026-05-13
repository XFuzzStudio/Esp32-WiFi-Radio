# Board Targets

## LCDWiki ESP32-S3 ES3C28P

Used by `ESP32WiFiRadio/` and `ESP32BinLoader/`.

- MCU: ESP32-S3 N16R8
- Flash: 16 MB
- PSRAM: 8 MB OPI
- Display: 2.8 inch 240x320 ILI9341
- Touch: FT6336 capacitive touch
- Audio: ES8311 codec
- SD: SD_MMC, 4-bit preferred with 1-bit fallback
- RGB LED: GPIO42
- Upload port used during development: `COM6`
- Debug UART used during development: `COM7`, `115200`

The compile-time pin definitions live in `shared/lcdwiki_es3c28p/`. The longer
vendor research notes were moved out of the repo archive to keep the public
source tree focused on buildable firmware.

Build target:

```text
esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=default
```

Radio partition scheme:

```text
PartitionScheme=app3M_fat9M_16MB
```

Bin loader partition scheme:

```text
PartitionScheme=custom
```

## Waveshare ESP32-C6-Touch-LCD-1.47

Used by `ESP32WiFiRadioPilot/`.

- MCU: ESP32-C6FH8
- Flash: 8 MB
- Display: 1.47 inch 172x320 touch LCD
- Display controller path: Arduino_GFX `Arduino_ST7789` with custom init table
- Touch: AXS5106L on I2C
- Battery ADC: GPIO0 through the onboard voltage divider
- Upload/debug port used during development: `COM5`, `115200`

Pins used by the pilot:

| Function | GPIO |
| --- | ---: |
| LCD SCK | 1 |
| LCD MOSI | 2 |
| LCD CS | 14 |
| LCD DC | 15 |
| LCD RST | 22 |
| LCD BL | 23 |
| Touch SDA | 18 |
| Touch SCL | 19 |
| Touch RST | 20 |
| Touch INT | 21 |
| Battery ADC | 0 |

Build target:

```text
esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB
```
