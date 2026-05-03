# UART Commands

Debug UART is `COM7` at `115200` baud in the current bench setup.

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli monitor -p COM7 -c baudrate=115200
```

## Stable InternetRadio

The stable project has a built-in `help` menu:

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
play
stop
next
prev
station N
list
vol N
theme
theme NAME
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

## LVGL Fork

Current LVGL fork commands:

```text
status
play
stop
next
prev
reload
theme
theme NAME
clearwifi
vol N
station N
list
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

## Useful Diagnostics

- `status` prints Wi-Fi, SD, audio, buffer, I2S, codec, battery, and clock state.
- `toneon` injects an I2S test tone through the audio callback.
- `i2slog on` prints I2S frame/callback activity every second.
- `codecsummary` prints key ES8311 registers.
- `codecdump` prints the ES8311 register range.
