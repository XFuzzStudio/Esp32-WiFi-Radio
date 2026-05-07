# ESP32 WiFi Radio

Official LVGL 9 internet radio firmware for the LCDWiki ESP32-S3 ES3C28P board.

## What It Does

- Plays internet radio streams over Wi-Fi through the ES8311 codec.
- Loads `/apps_data/ESP32WiFiRadio/radio.cfg` and
  `/apps_data/ESP32WiFiRadio/stations.csv` from microSD.
- Requires the radio SD data folder and uses AP setup when Wi-Fi is blank.
- Shows station, stream title, clock, SD/Wi-Fi/battery/pilot state, and controls.
- Supports up to 50 stations.
- Pairs with `ESP32WiFiRadioPilot/` over ESP-NOW.
- Provides web UI for config, station editing, diagnostics, pairing, and OTA.
- Uses a screensaver with large NTP clock and battery/link status.

## SD Files

`/apps_data/ESP32WiFiRadio/stations.csv`:

```text
Station name|stream_url|optional_cover_bmp_url
```

`/apps_data/ESP32WiFiRadio/radio.cfg` stores runtime settings. The sample in
`../sd_card/apps_data/ESP32WiFiRadio/radio.cfg` keeps Wi-Fi blank for safe
publication.

Important safe ranges are enforced by firmware:

- volume: `0..21`
- station rows: up to `50`
- Wi-Fi retry: `5..120` seconds
- Wi-Fi connect timeout: `5..60` seconds
- status refresh: `1000..30000` ms
- touch debounce: `80..800` ms
- LED brightness: `0..100`

Themes: `ocean`, `forest`, `sunset`, `mono`, `aurora`, `ember`, `berry`, `ice`,
`mint`, `plum`, `steel`, `amber`, `neon`, `wine`.

LED effects: `off`, `solid`, `breathe`, `blink`, `vu`.

## Build And Upload

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
& $cli compile --fqbn $fqbnS3 ESP32WiFiRadio
& $cli upload -p COM6 --fqbn $fqbnS3 ESP32WiFiRadio
& $cli monitor -p COM7 -c baudrate=115200
```

Required libraries:

- `lvgl` 9.x
- `GFX Library for Arduino`
- `Adafruit NeoPixel`
- `ESP32-audioI2S-master`

## OTA

Open the radio web UI and go to `/ota`. Upload `ESP32WiFiRadio.ino.bin`; the
radio display shows the OTA progress bar.

## ESP32 Bin Loader App

For `ESP32BinLoader`, copy the compiled app binary to the loader SD card:

```powershell
Copy-Item <build-path>\ESP32WiFiRadio.ino.bin <loader-sd-card>\apps\radio_lvgl.bin
```

The loader manifest uses:

```text
ESP32 WiFi Radio|radio_lvgl.bin|Official LVGL radio firmware
```

When the radio is launched from `ESP32BinLoader`, it sets the next boot target
back to the loader during startup. That means a normal reset returns to
`ESP32 Bin Loader` instead of keeping the radio as the default boot app.
