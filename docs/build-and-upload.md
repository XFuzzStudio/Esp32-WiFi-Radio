# Build And Upload

These commands use the Arduino CLI bundled with Arduino IDE on Windows.

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
$fqbnLoader = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=default"
```

## ESP32 WiFi Radio

```powershell
& $cli compile --fqbn $fqbnS3 ESP32WiFiRadio
& $cli upload -p COM6 --fqbn $fqbnS3 ESP32WiFiRadio
& $cli monitor -p COM7 -c baudrate=115200
```

## ESP32 WiFi Radio Pilot

```powershell
& $cli compile --fqbn $fqbnC6 ESP32WiFiRadioPilot
& $cli upload -p COM5 --fqbn $fqbnC6 ESP32WiFiRadioPilot
& $cli monitor -p COM5 -c baudrate=115200
```

## ESP32 Bin Loader

```powershell
& $cli compile --fqbn $fqbnLoader ESP32BinLoader
& $cli upload -p COM6 --fqbn $fqbnLoader ESP32BinLoader
& $cli monitor -p COM7 -c baudrate=115200
```

## OTA

- Radio: open `/ota` in the radio web UI and upload `ESP32WiFiRadio.ino.bin`.
- Pilot: tap `OTA`, join AP `ESP32RadioPilot-OTA` / `pilot1234`, open
  `http://192.168.4.1`, and upload `ESP32WiFiRadioPilot.ino.bin`.
- Bin Loader: copy app `.bin` files to `/apps` on the loader SD card, then run
  them from the touch UI, web UI, or UART.

## Required Libraries

`ESP32WiFiRadio`:

- `lvgl` 9.x
- `GFX Library for Arduino`
- `Adafruit NeoPixel`
- `ESP32-audioI2S-master`

`ESP32WiFiRadioPilot`:

- `GFX Library for Arduino`

`ESP32BinLoader`:

- `GFX Library for Arduino`

## Board Options

Radio and loader use the ESP32-S3 board target with:

- Flash size: `16M`
- PSRAM: `opi`
- USB mode: `hwcdc`
- CDC on boot: default

The radio uses `app3M_fat9M_16MB`; the loader uses `custom` so Arduino reads
`ESP32BinLoader/partitions.csv`.
