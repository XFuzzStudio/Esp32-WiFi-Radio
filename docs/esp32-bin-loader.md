# ESP32 Bin Loader

`ESP32BinLoader/` is the rescue launcher for the LCDWiki ESP32-S3 radio board.
It is useful when you want to keep a small factory app in flash and launch test
firmwares from SD without reflashing from a PC each time.

## How It Works

1. The loader boots from the factory partition.
2. It mounts SD and reads `/apps/manifest.txt`.
3. The user selects an app through touch, web UI, or UART.
4. The loader writes the selected app `.bin` through the ESP32 OTA API.
5. The board reboots into the selected firmware.
6. The selected firmware should set the next boot target back to factory, so a
   normal reset returns to `ESP32 Bin Loader`.

The selected app still runs from internal flash. SD is only storage for firmware
images.

## SD Card

```text
/apps/
  manifest.txt
  radio_lvgl.bin
  esp_wifi_scanner.bin
  esp_gif_player.bin
/apps_data/
  ESP32WiFiRadio/
  ESP-WiFi-Scanner/
  ESP-GiF-Player/
```

Manifest:

```text
# label|firmware_file|notes
ESP32 WiFi Radio|radio_lvgl.bin|Official LVGL radio firmware
```

## Web UI

The loader starts saved Wi-Fi when available and falls back to AP mode when
needed.

- AP SSID: `ESP32BinLoader-xxxx`
- AP password: `launcher123`
- AP address: `http://192.168.4.1`

The web UI can:

- launch apps
- save Wi-Fi credentials
- browse SD
- upload/download/delete files
- create folders
- start AP
- reboot

## UART

Debug UART is `COM7`, `115200`.

```text
up
down
enter
run
run N
ap
reload
list
```

## Build

Use `PartitionScheme=custom`; Arduino ESP32 reads `partitions.csv` from
`ESP32BinLoader/`.

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnLoader = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=default"
& $cli compile --fqbn $fqbnLoader ESP32BinLoader
& $cli upload -p COM6 --fqbn $fqbnLoader ESP32BinLoader
```

## Preparing ESP32 WiFi Radio

Compile `ESP32WiFiRadio` and copy `ESP32WiFiRadio.ino.bin` to the loader SD card
as `/apps/radio_lvgl.bin`.

`scripts/build-all.ps1` also builds `ESP-WiFi-Scanner` and `ESP-GiF-Player`,
then copies their app binaries to `/apps/esp_wifi_scanner.bin` and
`/apps/esp_gif_player.bin`.

`ESP32WiFiRadio` includes `shared/esp32_bin_loader_return.h` and automatically
sets the next reset target back to the loader. Other apps should do the same near
the start of `setup()`:

```cpp
#include "../shared/esp32_bin_loader_return.h"

void setup() {
  esp32BinLoaderReturnToFactoryOnNextBoot();
}
```
