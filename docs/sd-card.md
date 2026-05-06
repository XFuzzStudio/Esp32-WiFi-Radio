# SD Card Files

## ESP32 WiFi Radio

Copy the sample files from `sd_card/` to the root of the radio microSD card:

```text
/radio.cfg
/stations.csv
/covers/       optional, created by firmware when possible
/logo.bmp      optional boot logo
```

`radio.cfg` stores runtime settings. The sample file keeps Wi-Fi blank:

```text
wifi_ssid=
wifi_password=
```

Fill those values on your own SD card, or leave them blank and use the setup AP
at `http://192.168.4.1`.

For the ESP32-C6 pilot link, the radio writes `remote_paired_mac=` into
`/radio.cfg` during pairing. Leave that value empty in a fresh SD image.

## stations.csv

Station rows use this format:

```text
Station name|stream_url|optional_cover_bmp_url
```

The third field is optional. Cover downloading expects small plain `http://` BMP
files. If cover art is missing or invalid, the UI draws a station badge.

The radio loads up to 50 station rows.

## ESP32 Bin Loader

The loader SD card uses a separate `/apps` folder:

```text
/apps/
  manifest.txt
  radio_lvgl.bin
```

Manifest rows use:

```text
label|firmware_file|notes
```

Do not use merged firmware images in `/apps`; use the sketch app `.bin`.

## Safety

Do not commit a real SD card dump with private Wi-Fi credentials. Keep private
copies in a folder ignored by git, such as `sd_private/`.
