# ESP-GiF-Player

Version 1.1 LVGL GIF player app for the LCDWiki ESP32-S3 ES3C28P.

## Features

- Lists `.gif` files from `/apps_data/ESP-GiF-Player/gifs`.
- Touch controls for previous, play/pause, next, and menu.
- Web GUI for upload, selecting GIFs, play/pause, and delete.
- Uses the `AnimatedGIF` Arduino library for decoding.

## SD Layout

This app requires the SD card and this folder:

```text
/apps_data/ESP-GiF-Player/
  gifs/
  uploads/
```

Upload GIF files into `gifs/` from the web UI or copy them directly to the SD
card.

## Build

```powershell
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
arduino-cli compile --fqbn $fqbnS3 ESP-GiF-Player
```
