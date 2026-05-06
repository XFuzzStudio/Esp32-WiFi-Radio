# ESP32 WiFi Radio Pilot

`ESP32WiFiRadioPilot/` is a touch remote for `ESP32WiFiRadio/`. It targets the
Waveshare ESP32-C6-Touch-LCD-1.47 board.

## Link Protocol

- Transport: ESP-NOW.
- Pairing: the radio broadcasts a pairing advert for 120 seconds.
- Storage: the radio writes the paired pilot MAC to `/radio.cfg` as
  `remote_paired_mac=`, and the pilot stores the radio MAC/channel in NVS.
- Runtime status: the radio sends station, title/status, clock, radio battery,
  Wi-Fi bars/RSSI, volume, SD/AP/play flags, and channel.
- Runtime commands: the pilot sends prev, next, volume down, volume up, play,
  and stop commands.
- OTA: the pilot has its own AP/web update mode with on-screen progress.

## Pairing Steps

1. Upload `ESP32WiFiRadio/` to the LCDWiki ESP32-S3 on `COM6`.
2. Open debug UART on `COM7` at `115200`, or use the touch menu/web UI.
3. Start pairing with one of:
   - touch menu: `Menu -> Pair Pilot`
   - web UI: `ESP32-C6 Pilot -> Pair for 120 s`
   - UART: `pairremote`
4. Upload `ESP32WiFiRadioPilot/` to the ESP32-C6 on `COM5` and keep it powered
   on.
5. The pilot scans ESP-NOW channels until it hears the radio pairing advert.

## Ports

- LCDWiki ESP32-S3 upload: `COM6`
- LCDWiki debug UART: `COM7`, `115200`
- ESP32-C6 pilot upload/debug: `COM5`, `115200`

## Build

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
& $cli compile --fqbn $fqbnS3 ESP32WiFiRadio
& $cli compile --fqbn $fqbnC6 ESP32WiFiRadioPilot
```

## Screensaver

The pilot enters a large-clock screen after a short idle period and turns off
the backlight later. Incoming ESP-NOW packets update cached state but do not wake
the screen; touch wakes it.

## OTA

- Radio: open the radio web UI and go to `/ota`.
- Pilot: tap `OTA`, join AP `ESP32RadioPilot-OTA` with password `pilot1234`,
  open `http://192.168.4.1`, upload the pilot `.bin`, and watch the progress
  bar.
