# LCDWiki ESP32-S3 Internet Radio

Arduino sketch for the LCDWiki ESP32-S3 ES3C28P board.

This is the stable baseline project. The LVGL fork lives in `../esp32radioLVGL`.

## Features

- ILI9341V TFT UI with touch station menu.
- FT6336 touch input.
- ES8311 codec init plus I2S streaming through `ESP32-audioI2S-master`.
- SD card station list at `/stations.csv` and runtime config at `/radio.cfg`.
- SD-first boot protocol with progress bar, optional `/logo.bmp`, and visible errors for missing SD/config/stations.
- First setup portal over AP at `http://192.168.4.1`.
- AP settings for theme, volume, autoplay, LED behavior, timing, startup station,
  Wi-Fi, battery, clock, and cover downloads. Saves go to `/radio.cfg` on SD.
- One NeoPixel status LED:
  - blue/white blink every 1 s: boot
  - amber blink: Wi-Fi connecting
  - blue pulse: AP/config portal active
  - theme-color breathing: streaming
  - green pulse: connected/idle
  - red blink: SD/config/station error

## SD Files

The SD card is the source of truth. On boot the sketch expects:

- `/radio.cfg`
- `/stations.csv`
- optional `/logo.bmp` for the boot screen
- optional `/covers/` for downloaded station BMP covers

If the SD card is missing, the display shows an SD error, the header `SD` label
turns red, and the NeoPixel blinks red. If `/radio.cfg` or `/stations.csv` is
missing, the setup AP can still be started, but saving from the AP writes the
new file to SD.

Edit `/stations.csv` on the SD card:

```text
Groove Salad|http://ice5.somafm.com/groovesalad-128-mp3|http://example.com/groove.bmp
Drone Zone|http://ice5.somafm.com/dronezone-128-mp3
Deep Space One|http://ice5.somafm.com/deepspaceone-128-mp3
```

The third field is optional cover art. Current cover decoding is intentionally
small: use plain `http://` BMP files under roughly 140 kB. If no usable cover is
available, the UI draws a station badge instead.

Edit `/radio.cfg` for startup settings:

```text
# WiFi values are plain text on SD. Leave wifi_ssid empty to start setup AP.
theme=ocean
volume=13
autoplay=1
cover_download=1
startup_station=last
wifi_ssid=
wifi_password=
start_ap_on_boot=0
wifi_retry_seconds=15
wifi_connect_timeout_seconds=18
status_refresh_ms=5000
ntp_enabled=1
ntp_server=pool.ntp.org
timezone=CET-1CEST,M3.5.0,M10.5.0/3
clock_24h=1
touch_debounce_ms=170
battery_enabled=1
battery_min_mv=3300
battery_max_mv=4200
battery_scale_permille=2000
led_enabled=1
led_brightness=18
led_boot_effect=breathe
led_wifi_effect=blink
led_ap_effect=breathe
led_streaming_effect=vu
led_idle_effect=breathe
led_error_effect=blink
led_pulse_ms=2400
led_blink_ms=260
```

The repository also includes a ready-to-edit sample in `sd_card/radio.cfg` and
`sd_card/stations.csv`.

Themes: `ocean`, `forest`, `sunset`, `mono`, `aurora`, `ember`, `berry`,
`ice`, `mint`, `plum`, `steel`, `amber`, `neon`, `wine`.

LED effects: `off`, `solid`, `breathe`, `blink`, `vu`. Safe ranges are enforced
by the sketch: volume `0..21`, LED brightness `0..100`, Wi-Fi retry `5..120`
seconds, Wi-Fi connect timeout `5..60` seconds, status refresh `1000..30000`
ms, touch debounce `80..800` ms. Battery is read from GPIO9; the default
`battery_scale_permille=2000` means the measured ADC voltage is multiplied by
2.000 before calculating percentage.

The clock uses SNTP through `ntp_server` and a POSIX `timezone` string. The
default timezone is Poland/Central Europe with daylight saving time.

## Build, Upload, Debug

Arduino CLI from the bundled Arduino IDE:

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default" InternetRadio
& $cli upload -p COM6 --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default" InternetRadio
& $cli monitor -p COM7 -c baudrate=115200
```

Useful debug commands on `COM7`:

```text
help
status
config
sd
saveconfig
reload
play
stop
next
prev
station 0
vol 21
theme
theme forest
list
ap
apoff
reconnect
clearwifi
unmute
toneon
toneoff
i2slog on
i2slog off
codecdebug on
codecdebug off
codecsummary
codec16
codec32
amp0
amp1
codecdump
reboot
```

See `../docs/uart-commands.md` for the shared UART command reference and
`../docs/sd-card.md` for SD card setup notes.

Required libraries:

- `GFX Library for Arduino`
- `Adafruit NeoPixel`
- `ESP32-audioI2S-master`
