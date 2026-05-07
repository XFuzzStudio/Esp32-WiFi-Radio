# SD Card Files

All apps that use local data expect their own folder under `/apps_data`.

Copy the sample files from `sd_card/` to the root of the microSD card:

```text
/apps/
  manifest.txt
  radio_lvgl.bin
  esp_wifi_scanner.bin
  esp_gif_player.bin
/apps_data/
  ESP32WiFiRadio/
    radio.cfg
    stations.csv
    covers/
  ESP-WiFi-Scanner/
    logs/
  ESP-GiF-Player/
    gifs/
    uploads/
```

If an app cannot mount SD or cannot find its required data folder/files, it
shows an error instead of silently using root-card files.

## ESP32 WiFi Radio

`radio.cfg` stores runtime settings. The sample file keeps Wi-Fi blank:

```text
wifi_ssid=
wifi_password=
```

Fill those values on your own SD card, or leave them blank and use the setup AP
at `http://192.168.4.1`.

For the ESP32-C6 pilot link, the radio writes `remote_paired_mac=` into
`/apps_data/ESP32WiFiRadio/radio.cfg` during pairing. Leave that value empty in
a fresh SD image.

## stations.csv

Station rows use this format:

```text
Station name|stream_url|optional_cover_bmp_url
```

The third field is optional. Cover downloading expects small plain `http://` BMP
files. If cover art is missing or invalid, the UI draws a station badge.

The radio loads up to 50 station rows.

## ESP32 Bin Loader

The loader SD card uses `/apps` for firmware images:

```text
/apps/
  manifest.txt
  radio_lvgl.bin
  esp_wifi_scanner.bin
  esp_gif_player.bin
```

Manifest rows use:

```text
label|firmware_file|notes
```

Do not use merged firmware images in `/apps`; use the sketch app `.bin`.

## Safety

Do not commit a real SD card dump with private Wi-Fi credentials. Keep private
copies in a folder ignored by git, such as `sd_private/`.
