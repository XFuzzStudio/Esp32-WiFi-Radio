# ESP32 WiFi Radio SD Card Starter Files

Copy these files to the root of the microSD card:

- `radio.cfg`
- `stations.csv`

The sample `radio.cfg` is intentionally sanitized:

```text
wifi_ssid=
wifi_password=
```

Fill those values on your own SD card, or leave them empty and use the setup AP
at `http://192.168.4.1`.

Do not commit a private SD card copy with real Wi-Fi credentials.
