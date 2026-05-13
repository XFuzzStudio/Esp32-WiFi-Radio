# ESP-WiFi-Scanner

Version 1.1 LVGL network utility app for the LCDWiki ESP32-S3 ES3C28P.

## Features

- Wi-Fi scan from touch UI and web GUI.
- On-screen keyboard for SSID and password entry, with background tap to close.
- Connects to selected Wi-Fi and shows IP/RSSI.
- Tapping a scanned Wi-Fi row fills the SSID field and loads a saved password
  when one exists.
- Header space is dedicated to current Wi-Fi/AP state, selected SSID, and IP.
- Scans the local subnet for live hosts using fast TCP probes.
- Scans popular ports or a custom comma-separated port list.
- Results are shown in a larger touch-scrollable output panel; Wi-Fi row
  selection is active only immediately after Wi-Fi scans.
- A top-right spinner is shown while Wi-Fi, host, or port scans are running.
- Writes scan logs to `/apps_data/ESP-WiFi-Scanner/logs/scanner.log` only when
  logging is enabled from the touch UI or web GUI.
- Saves Wi-Fi credentials locally on SD in
  `/apps_data/ESP-WiFi-Scanner/wifi_creds.csv`; this file is intentionally not
  part of the public sample data.

## SD Layout

This app requires the SD card and this folder:

```text
/apps_data/ESP-WiFi-Scanner/
  logs/
```

## Build

```powershell
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
arduino-cli compile --fqbn $fqbnS3 ESP-WiFi-Scanner
```
