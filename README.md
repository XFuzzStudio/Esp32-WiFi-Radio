# ESP32 WiFi Radio

Version 1.1 firmware set for the LCDWiki 2.8 inch ESP32-S3 ES3C28P board and a
Waveshare ESP32-C6 touch remote.

The official radio firmware is the LVGL build in `ESP32WiFiRadio/`. Extra
loader apps use the same LCDWiki ESP32-S3 target and run from `ESP32BinLoader`.

## Projects

- `ESP32WiFiRadio/` - official LVGL internet radio for the LCDWiki ESP32-S3.
- `ESP-WiFi-Scanner/` - LVGL Wi-Fi scanner, host finder, and port scanner.
- `ESP-GiF-Player/` - LVGL GIF player with touch and web upload.
- `ESP32WiFiRadioPilot/` - ESP32-C6 touch pilot using ESP-NOW.
- `ESP32BinLoader/` - factory/rescue loader that installs `.bin` apps from SD.
- `shared/` - ESP-NOW protocol shared by the radio and pilot.
- `sd_card/` - sanitized starter SD card files for all SD-backed apps.

## Hardware

- Radio board: LCDWiki 2.8 inch ESP32-S3 Display, touch version `ES3C28P`
- Radio upload port used during development: `COM6`
- Radio debug UART used during development: `COM7`, `115200`
- Pilot board: Waveshare ESP32-C6-Touch-LCD-1.47
- Pilot upload/debug port used during development: `COM5`, `115200`

## Radio Features

- LVGL 9 touch UI with loading animation, dark mode, scrollable station list, and controls.
- ES8311 codec and I2S audio streaming through `ESP32-audioI2S-master`.
- SD-first station/config storage with setup AP when Wi-Fi is missing.
- Web UI for Wi-Fi, stations, config, diagnostics, ESP-NOW pairing, and OTA.
- Up to 50 stations in `/apps_data/ESP32WiFiRadio/stations.csv`.
- NTP clock, battery status, Wi-Fi bars, pilot battery/link state.
- Screensaver with large clock to reduce display updates on battery.
- Addressable RGB LED network/playback/error effects.

## Extra Apps

- `ESP-WiFi-Scanner` scans Wi-Fi networks, connects through an on-screen
  keyboard, scans live hosts and popular/custom ports, and can write scan logs
  only when logging is enabled from touch UI or web GUI.
- `ESP-GiF-Player` lists and plays GIF files from SD with touch controls and web
  upload/selection.

## Quick Start

1. Install Arduino IDE 2.x, ESP32 Arduino core, and required libraries.
2. Copy the contents of `sd_card/` to a microSD card.
3. Fill `wifi_ssid=` and `wifi_password=` on the SD card, or leave them blank
   and use the setup AP at `http://192.168.4.1`.
4. Compile/upload the radio:

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
& $cli compile --fqbn $fqbnS3 ESP32WiFiRadio
& $cli upload -p COM6 --fqbn $fqbnS3 ESP32WiFiRadio
```

5. Compile/upload the pilot:

```powershell
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
& $cli compile --fqbn $fqbnC6 ESP32WiFiRadioPilot
& $cli upload -p COM5 --fqbn $fqbnC6 ESP32WiFiRadioPilot
```

## Documentation

- [Build and upload](docs/build-and-upload.md)
- [SD card files](docs/sd-card.md)
- [Versioning](docs/versioning.md)
- [Release workflow](docs/release-workflow.md)
- [Security notes](docs/security.md)
- [ESP32 WiFi Radio Pilot](docs/radio-pilot.md)
- [ESP32 Bin Loader](docs/esp32-bin-loader.md)
- [UART commands](docs/uart-commands.md)
- [Board targets](docs/board-targets.md)
- [Release checklist](docs/release-checklist.md)
- [License notes](LICENSES.md)

## License

The repository code and documentation are GPL-3.0-or-later. This matches the
GPLv3 audio dependency used by the radio firmware build path.
