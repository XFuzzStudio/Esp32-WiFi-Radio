# GitHub Release Checklist

Use this before creating the public repository.

## Before The First Push

- Review `README.md`, project READMEs, and docs for stale notes.
- Confirm `LICENSE` and `LICENSES.md` are present.
- Confirm `sd_card/radio.cfg` has blank `wifi_ssid=` and `wifi_password=`.
- Do not commit private SD card dumps, Wi-Fi credentials, or local logs.
- Keep `AudioProbe/` and `AudioProbeIdf/` only if diagnostic history is useful
  for the public repo.

## Build Verification

Run:

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbn = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=default"
& $cli compile --fqbn $fqbn InternetRadio
& $cli compile --fqbn $fqbn esp32radioLVGL
```

Optional hardware smoke test:

- Upload `InternetRadio` to `COM6`, check `COM7 status`.
- Upload `esp32radioLVGL` to `COM6`, check `COM7 status`.
- Confirm SD loads, Wi-Fi connects, and `Audio: playing=1 running=1`.

## Suggested Initial Commit

```powershell
git init
git add .
git status
git commit -m "Initial LCDWiki ESP32-S3 internet radio projects"
```

Then create a GitHub repository and push according to the URL GitHub gives you.
