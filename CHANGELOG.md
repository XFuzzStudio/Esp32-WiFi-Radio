# Changelog

## Unreleased

- Documented the local folder-per-version workflow.
- Added release workflow notes for changelog and GitHub releases.
- Added `private_files/` as an ignored local folder for credentials and private SD data.

## 1.1 - 2026-05-13

- Reworked the radio UI settings from multi-theme selection to a simple dark mode.
- Added station-list scroll buttons on the radio LVGL screen.
- Removed radio cover/thumbnail support from screen UI, web settings, CSV format, SD folders, and BMP download code.
- Added configurable setup AP password in the radio web GUI and SD config.
- Shows the radio setup AP IP and AP password together in status/web UI.
- Improved the radio pilot link indicator to show `P1`/`P2`/`P3` based on recent ESP-NOW contact.
- Reduced ESP32-C6 pilot screen flicker by redrawing only changed dynamic fields.
- Fixed ESP32-C6 pilot battery scaling while keeping the ADC off the display SPI clock pin.
- Shows radio and pilot battery percentages on the pilot screensaver and uses compact battery percentages in the main header.
- Reduced pilot screensaver flicker by updating only changed fields instead of clearing the whole screen.
- Removed ellipsis dots from clipped pilot station/title text.
- Improved ESP-WiFi-Scanner keyboard handling: tap background/output to close the keyboard.
- Replaced scanner auto-scrolling output text with a touch-scrollable output panel.
- Allows tapping scanned Wi-Fi rows in the scanner output to populate the SSID field.
- Reworked the scanner header to show Wi-Fi/AP state and moved results into a larger output panel.
- Prevents host/port output taps from selecting stale Wi-Fi rows after a Wi-Fi scan.
- Added a top-right scanner activity indicator while Wi-Fi, host, or port scans are running.
- Starts the scanner display/UI before SD and AP initialization to avoid a black screen during slow startup.
- Restored the scanner busy indicator as a lightweight animated dot and fixed Wi-Fi row selection after output scrolling.
- Rebuilt Wi-Fi scan results as real scrollable LVGL row buttons and added the onboard RGB LED as a busy indicator.
- Saves scanner Wi-Fi passwords locally on SD in `/apps_data/ESP-WiFi-Scanner/wifi_creds.csv`.
- Added live PowerShell build progress reporting to `scripts/build-all.ps1`.
- Optimized `scripts/build-all.ps1` for this laptop: cached incremental builds, single-sketch builds, `-Clean`, and default 3-job compilation on 4 logical CPU threads.

## 1.0 - 2026-05-06

- Promoted `ESP32WiFiRadio/` LVGL firmware to the official radio project.
- Added `ESP32WiFiRadioPilot/` ESP32-C6 touch remote.
- Added `ESP32BinLoader/` factory/rescue loader for SD-stored app `.bin` files.
- Added ESP-NOW pairing, station/volume/play controls, battery and Wi-Fi status.
- Added OTA with on-screen progress for the radio and pilot.
- Added radio and pilot screensavers for lower battery drain.
- Removed superseded diagnostics and UI experiments from the public repo.
- Added `ESP-WiFi-Scanner` and `ESP-GiF-Player` app projects for the bin loader.
- Moved radio SD data to `/apps_data/ESP32WiFiRadio/`.
- Added per-project versioning policy starting from `1.0`.
