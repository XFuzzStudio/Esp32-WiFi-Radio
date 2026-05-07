# ESP-WiFi-Scanner

Version 1.0 LVGL network utility app for the LCDWiki ESP32-S3 ES3C28P.

## Features

- Wi-Fi scan from touch UI and web GUI.
- On-screen keyboard for SSID and password entry.
- Connects to selected Wi-Fi and shows IP/RSSI.
- Scans the local subnet for live hosts using fast TCP probes.
- Scans popular ports or a custom comma-separated port list.
- Writes scan logs to `/apps_data/ESP-WiFi-Scanner/logs/scanner.log` only when
  logging is enabled from the touch UI or web GUI.

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
