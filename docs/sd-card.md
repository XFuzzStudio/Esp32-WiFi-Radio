# SD Card Files

Copy the sample files from `sd_card/` to the root of the microSD card:

```text
/radio.cfg
/stations.csv
/covers/
/logo.bmp        optional boot logo
```

`/covers/` is created by the sketches when possible. `logo.bmp` is optional.

## radio.cfg

`radio.cfg` stores runtime settings. The sample file keeps Wi-Fi blank:

```text
wifi_ssid=
wifi_password=
```

Fill those values on your own SD card, or leave them blank and use the setup AP
at `http://192.168.4.1`.

The stable `InternetRadio` project treats SD as the source of truth. If SD,
`/radio.cfg`, or `/stations.csv` is missing, it shows an error on the display and
uses the red LED error state. The AP can still be used to write config back to
SD when a card is mounted.

The `esp32radioLVGL` fork currently keeps the older fallback behavior and can
create default files when SD is mounted.

## stations.csv

Station rows use this format:

```text
Station name|stream_url|optional_cover_bmp_url
```

The third field is optional. Cover downloading currently expects small plain
`http://` BMP files.

Example:

```text
Groove Salad|http://ice5.somafm.com/groovesalad-128-mp3
Drone Zone|http://ice5.somafm.com/dronezone-128-mp3
```

## Safety

Do not commit a real SD card dump with private Wi-Fi credentials. Keep private
copies in a folder ignored by git, such as `sd_private/`.
