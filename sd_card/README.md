# ESP32 WiFi Radio SD Card Starter Files

Copy the contents of this folder to the microSD card.

Each app keeps its data in its own folder:

```text
/apps/
  manifest.txt
  *.bin
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

The sample radio config is intentionally sanitized:

```text
wifi_ssid=
wifi_password=
```

Fill those values on your own SD card, or leave them empty and use the setup AP.

Do not commit a private SD card copy with real Wi-Fi credentials.
