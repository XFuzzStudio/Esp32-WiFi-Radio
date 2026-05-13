# UART Commands

Radio debug UART is `COM7` at `115200` baud in the current bench setup.

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli monitor -p COM7 -c baudrate=115200
```

## ESP32 WiFi Radio

The radio has a built-in `help` menu:

```text
help
menu
?
status
config
sd
files
saveconfig
reload
ap
apoff
reconnect
clearwifi
pairremote
remote
unpairremote
play
stop
next
prev
station N
list
vol N
dark
dark on
dark off
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

Useful diagnostics:

- `status` prints Wi-Fi, SD, audio, buffer, I2S, codec, battery, clock, pilot,
  and resource state.
- `pairremote` opens a 120 second ESP-NOW pairing window.
- `remote` prints pilot link summary, radio MAC, channel, and paired MAC.
- `toneon` injects an I2S test tone through the audio callback.
- `i2slog on` and `codecdebug on` auto-disable after 60 seconds.

## ESP32 Bin Loader

The loader uses the same S3 debug UART on `COM7`:

```text
up
down
enter
run
run N
ap
reload
list
```

## ESP32 WiFi Radio Pilot

The pilot uses `COM5` at `115200`. It currently logs OTA/update details and
radio link activity; interaction is primarily through touch.
