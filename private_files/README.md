# Private Files

This folder is for local files required to run or test the project but unsafe to
publish on GitHub.

Examples:

- SD card copies with real `wifi_ssid` or `wifi_password`.
- Personal station lists that should not be public.
- Local build outputs or firmware binaries before release.
- Notes with device MAC addresses, IP addresses, or network details.

Everything in this folder is ignored by Git except this README.

Keep clean, publishable versions of required files in the repository. For the
radio SD card, the public template is:

```text
sd_card/apps_data/ESP32WiFiRadio/radio.cfg
sd_card/apps_data/ESP32WiFiRadio/stations.csv
```

Before every commit, check:

```powershell
git status --short
git diff --check
```

Do not commit real Wi-Fi passwords or private SD card backups.
