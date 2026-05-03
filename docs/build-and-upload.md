# Build And Upload

These commands use the Arduino CLI bundled with Arduino IDE on Windows.

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbn = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
```

## Stable Project

```powershell
& $cli compile --fqbn $fqbn InternetRadio
& $cli upload -p COM6 --fqbn $fqbn InternetRadio
& $cli monitor -p COM7 -c baudrate=115200
```

## LVGL Fork

```powershell
& $cli compile --fqbn $fqbn esp32radioLVGL
& $cli upload -p COM6 --fqbn $fqbn esp32radioLVGL
& $cli monitor -p COM7 -c baudrate=115200
```

## Required Libraries

Stable `InternetRadio`:

- `GFX Library for Arduino`
- `Adafruit NeoPixel`
- `ESP32-audioI2S-master`

LVGL fork:

- `lvgl` 9.x
- `GFX Library for Arduino`
- `Adafruit NeoPixel`
- `ESP32-audioI2S-master`

## Board Options

Use the ESP32-S3 board target with:

- Flash size: `16M`
- PSRAM: `opi`
- Partition scheme: `app3M_fat9M_16MB`
- USB mode: `hwcdc`
- CDC on boot: default
