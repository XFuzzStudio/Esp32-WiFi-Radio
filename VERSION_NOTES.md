# Version Notes

## V1.2

Source folder: `Desktop/github/Esp32-WiFi-Radio/V1.2`

Base: copied from `V1.1`.

Planned release tag: `v1.2`

Status: media-frame update.

Summary:

- `ESP-GiF-Player` now works as a landscape media frame.
- GIFs still load from `/apps_data/ESP-GiF-Player/gifs`.
- JPG/JPEG and uncompressed 24-bit BMP photos load from `/apps_data/ESP-GiF-Player/photos`.
- Web upload accepts GIF/JPG/BMP media and stores files in the matching SD folder.
- Slideshow playback advances from GIFs to photos and back through the shared media list.
- `JPEGDEC` is now required for the media frame build.

## V1.1

Source folder: `Desktop/github/Esp32-WiFi-Radio/V1.1`

Base: copied from `V1.0`.

Planned release tag: `v1.1`

Status: active rebuild version.

Summary:

- Radio UI cleanup: no public theme selector, only dark mode.
- Radio AP password is configurable and visible in setup mode.
- Radio station list has scroll controls.
- Radio cover/thumbnail support was removed; stations use `name|stream_url`.
- Pilot UI redraws fewer screen regions to reduce flicker.
- Scanner header, output panel, Wi-Fi row selection, and busy spinner were updated.
- Release/build workflow includes a PowerShell progress bar and fast cached builds tuned for the local i7-6600U laptop.

## V1.0

Source: `https://github.com/XFuzzStudio/Esp32-WiFi-Radio.git`

Base commit: `2ee6ff5e687dc1f30ef4c947f1a217b317bbc0f8`

Release tag: `v1.0`

Status: clean GitHub baseline for the new local workflow.

Summary:

- Official LVGL ESP32 WiFi Radio firmware.
- ESP32-C6 pilot firmware.
- ESP32 Bin Loader with SD app manifest.
- Extra loader apps: ESP-WiFi-Scanner and ESP-GiF-Player.
- SD card layout uses per-app data folders under `/apps_data`.

Local workflow:

- Keep each released working version in its own folder under `Desktop/github/Esp32-WiFi-Radio/`.
- For a small update, copy the newest version folder and increment by `0.1`, for example `V1.1` to `V1.2`.
- For a large/final update, increment the major version, for example `V1.9` to `V2.0`.
- Update `CHANGELOG.md` before every GitHub push/release.
- Create a GitHub release for every version that is published.
- Keep private Wi-Fi credentials, private SD dumps, and local-only binaries out of Git.
