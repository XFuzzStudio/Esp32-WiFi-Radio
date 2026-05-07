# ESP32 WiFi Radio Pilot

Touch remote for `ESP32WiFiRadio/` on the Waveshare ESP32-C6-Touch-LCD-1.47.

## What It Shows

- Current station and stream title from the radio.
- Radio battery, pilot battery, radio Wi-Fi bars, and ESP-NOW link state.
- Current radio volume and ESP-NOW channel.
- Large-clock screensaver; after a longer idle period the backlight turns off.
- OTA mode over the pilot's own AP with progress on the pilot display.

## Controls

- `Prev` / `Next` changes station.
- `Vol-` / `Vol+` changes volume.
- `Play` starts playback.
- `Stop` stops playback.
- `OTA` starts the pilot OTA AP.

## Pairing

1. On the radio, start `Pair Pilot` from the touch menu, web UI, or UART command
   `pairremote`.
2. Keep the pilot powered on. When it hears the ESP-NOW pairing advert, it stores
   the radio MAC and channel in NVS.
3. To clear radio-side pairing use web UI, UART `unpairremote`, or edit
   `/apps_data/ESP32WiFiRadio/radio.cfg` and clear `remote_paired_mac=`.

## Build And Upload

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
& $cli compile --fqbn $fqbnC6 ESP32WiFiRadioPilot
& $cli upload -p COM5 --fqbn $fqbnC6 ESP32WiFiRadioPilot
```

The pilot uses the C6 USB serial port for debug at `115200`.

## OTA

Tap the small `OTA` button in the status card. The pilot starts AP
`ESP32RadioPilot-OTA` with password `pilot1234`; open `http://192.168.4.1` and
upload `ESP32WiFiRadioPilot.ino.bin`.
