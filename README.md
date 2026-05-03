# LCDWiki ESP32-S3 Internet Radio

Arduino internet radio projects for the LCDWiki 2.8 inch ESP32-S3 ES3C28P board
with ILI9341 TFT, FT6336 touch, ES8311 audio codec, microSD, battery ADC, and one
addressable RGB LED.

## Projects

- `InternetRadio/` - stable Arduino_GFX UI. This is the safer baseline and uses
  SD-first boot/config handling.
- `esp32radioLVGL/` - LVGL 9 fork with a modern card-based UI and animated
  loading screen.
- `hardware_specs/lcdwiki-esp32-s3-es3c28p/` - reusable board pinout, helpers,
  and bring-up notes.
- `sd_card/` - sanitized starter files for the SD card.
- `AudioProbe/` and `AudioProbeIdf/` - diagnostic audio sketches kept for ES8311
  bring-up history.

## Hardware

- Board: LCDWiki 2.8 inch ESP32-S3 Display, touch version `ES3C28P`
- ESP32-S3 target: 16 MB flash, OPI PSRAM, 3 MB app + 9 MB FAT partition layout
- Programming port used during development: `COM6`
- Debug UART used during development: `COM7` at `115200`

See [hardware_specs/lcdwiki-esp32-s3-es3c28p/pinout.md](hardware_specs/lcdwiki-esp32-s3-es3c28p/pinout.md)
for the complete pin map.

## Quick Start

1. Install Arduino IDE 2.x and ESP32 Arduino core.
2. Install the libraries listed in each project README.
3. Copy `sd_card/radio.cfg` and `sd_card/stations.csv` to the root of a microSD
   card.
4. Edit `wifi_ssid=` and `wifi_password=` on the SD card, or leave them empty and
   use the setup AP.
5. Compile and upload either project:

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default" InternetRadio
& $cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default" InternetRadio
```

For the LVGL fork, replace `InternetRadio` with `esp32radioLVGL`.

## Documentation

- [Build and upload](docs/build-and-upload.md)
- [SD card files](docs/sd-card.md)
- [UART commands](docs/uart-commands.md)
- [GitHub release checklist](docs/release-checklist.md)
- [License notes](LICENSES.md)

## Publication Notes

The sample SD config is sanitized. Do not commit a real SD card dump containing
Wi-Fi credentials.

The repository code and documentation are GPL-3.0-or-later. This matches the
GPLv3 audio dependency used by the firmware build path.
