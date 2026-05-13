# Security Notes

This repository keeps only sanitized sample configuration files.

## Do Not Commit

- Real `wifi_ssid=` or `wifi_password=` values.
- Private SD card dumps.
- `private_files/` contents except `private_files/README.md`.
- Scanner saved credentials from
  `/apps_data/ESP-WiFi-Scanner/wifi_creds.csv`.
- Personal station lists, MAC addresses, IP maps, or local network notes.

## AP Passwords

The firmware uses default AP passwords for local setup and OTA modes:

- Radio setup AP: `radio1234` by default, configurable with `ap_password=`.
- Pilot OTA AP: `pilot1234`.
- Bin Loader AP: `launcher123`.
- Wi-Fi Scanner AP: `scanner123`.
- GIF Player AP: `gifplayer123`.

Change the radio setup AP password before using it outside a trusted local
bench. The other AP passwords are compile-time defaults in their app sources.

## Plain-Text Storage

The radio stores Wi-Fi credentials in NVS and can write them to
`/apps_data/ESP32WiFiRadio/radio.cfg` when saving config. The scanner can save
Wi-Fi passwords on SD in `wifi_creds.csv` so the user does not need to retype
them. Treat the SD card as sensitive if real credentials were used.

Before publishing:

```powershell
git status --short
git diff --check
rg -n "wifi_password=.+|wifi_ssid=.+|token|secret|api[_-]?key" -S .
```
