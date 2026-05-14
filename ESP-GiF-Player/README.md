# ESP-GiF-Player

Version 1.1 LVGL media frame app for the LCDWiki ESP32-S3 ES3C28P.

## Features

- Lists `.gif` files from `/apps_data/ESP-GiF-Player/gifs` and `.jpg`,
  `.jpeg`, `.bmp` photos from `/apps_data/ESP-GiF-Player/photos`.
- Plays media in landscape orientation as a simple photo frame.
- Touch controls for previous, play/pause, next, and refresh.
- Web GUI for upload, selecting media, play/pause, and delete.
- Uses the `AnimatedGIF` Arduino library for decoding.
- Uses the `JPEGDEC` Arduino library for JPEG photos.
- Supports uncompressed 24-bit BMP files without an extra decoder.
- Uses the onboard addressable RGB LED for AP/client/upload status.

## SD Layout

This app requires the SD card and this folder:

```text
/apps_data/ESP-GiF-Player/
  gifs/
  photos/
  uploads/
```

Upload GIF/JPG/BMP files from the web UI or copy GIF files into `gifs/` and
photos into `photos/` directly on the SD card.

## Build

```powershell
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
arduino-cli compile --fqbn $fqbnS3 ESP-GiF-Player
```
