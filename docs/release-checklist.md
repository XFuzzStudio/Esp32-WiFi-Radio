# GitHub Release Checklist

Use this before tagging a public release.

## Version 1.1 Scope

- `ESP32WiFiRadio/` is the official radio firmware.
- `ESP-WiFi-Scanner/` is the official scanner utility app.
- `ESP-GiF-Player/` is the official GIF player app.
- `ESP32WiFiRadioPilot/` is the official ESP32-C6 remote.
- `ESP32BinLoader/` is the factory/rescue app loader.
- Legacy diagnostics and superseded UI experiments are intentionally kept out
  of the release tree.

## Before Push

- Review `README.md`, project READMEs, and docs for stale project names.
- Confirm `LICENSE` and `LICENSES.md` are present.
- Confirm `sd_card/apps_data/ESP32WiFiRadio/radio.cfg` has blank `wifi_ssid=`
  and `wifi_password=`.
- Confirm `sd_card/apps_data/ESP-WiFi-Scanner/wifi_creds.csv` is not present.
- Do not commit private SD card dumps, Wi-Fi credentials, local logs, or built
  `.bin` files.

## Build Verification

Run:

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnS3 = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
$fqbnC6 = "esp32:esp32:esp32c6:FlashSize=8M,PartitionScheme=default_8MB"
$fqbnLoader = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=default"
& $cli compile --fqbn $fqbnS3 ESP32WiFiRadio
& $cli compile --fqbn $fqbnC6 ESP32WiFiRadioPilot
& $cli compile --fqbn $fqbnLoader ESP32BinLoader
```

Optional hardware smoke test:

- Upload `ESP32WiFiRadio` to `COM6`, check `COM7 status`.
- Upload `ESP32WiFiRadioPilot` to `COM5`, pair with the radio.
- Upload `ESP32BinLoader` to `COM6`, check app list and web SD browser.

## Release

```powershell
git status
git add .
git commit -m "Release ESP32 WiFi Radio 1.1"
git tag -a v1.1 -m "ESP32 WiFi Radio 1.1"
git push origin main
git push origin v1.1
```
