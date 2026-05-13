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
ap_password=radio1234
```

Fill those values on your own SD card, or leave them blank and use the setup AP
at `http://192.168.4.1`.

The radio setup AP password can be changed with `ap_password=` or from the web
GUI. Keep private SD card copies with real Wi-Fi credentials out of Git.

For the ESP32-C6 pilot link, the radio writes `remote_paired_mac=` into
`/apps_data/ESP32WiFiRadio/radio.cfg` during pairing. Leave that value empty in
a fresh SD image.

## stations.csv

Station rows use this format:

```text
Station name|stream_url
```

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

The scanner can create `/apps_data/ESP-WiFi-Scanner/wifi_creds.csv` on the SD
card after successful Wi-Fi connections. That file contains saved Wi-Fi
passwords in plain text and is ignored by Git.
