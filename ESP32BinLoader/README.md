# ESP32 Bin Loader

Factory/rescue loader for the LCDWiki ESP32-S3 ES3C28P board.

The ESP32 cannot execute firmware directly from SD. This loader keeps a small
factory app in flash, reads `.bin` files from `/apps` on microSD, installs the
selected image through the ESP32 OTA API, and reboots into it. Apps launched by
the loader should set the next boot target back to the factory partition early
in `setup()`, so a normal reset returns to `ESP32 Bin Loader`.

## Features

- Touch UI with app list, up/down/select controls, and install progress.
- `/apps/manifest.txt` support for friendly app names.
- Fallback scan for `.bin` files in `/apps`.
- Wi-Fi client mode with fallback AP when saved Wi-Fi is missing or fails.
- Web UI for launching apps, saving Wi-Fi, browsing SD, upload/download/delete,
  folder creation, AP start, and reboot.
- UART menu on `COM7` for up/down/run/AP/reload/list.

## SD Layout

```text
/apps/
  manifest.txt
  radio_lvgl.bin
  esp_wifi_scanner.bin
  esp_gif_player.bin
```

Manifest format:

```text
# label|firmware_file|notes
ESP32 WiFi Radio|radio_lvgl.bin|Official LVGL radio firmware
ESP-WiFi-Scanner|esp_wifi_scanner.bin|LVGL Wi-Fi scanner, host finder, and port scanner
ESP-GiF-Player|esp_gif_player.bin|LVGL GIF player with touch and web upload
```

## Build And Upload

`ESP32BinLoader` uses the custom partition table stored in this sketch folder.

```powershell
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$fqbnLoader = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=default"
& $cli compile --fqbn $fqbnLoader ESP32BinLoader
& $cli upload -p COM6 --fqbn $fqbnLoader ESP32BinLoader
& $cli monitor -p COM7 -c baudrate=115200
```

## Web UI

- AP SSID: `ESP32BinLoader-xxxx`
- AP password: `launcher123`
- AP address: `http://192.168.4.1`

When connected to saved Wi-Fi, open the IP printed on `COM7`.

## UART

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

## Reset Behavior

`ESP32WiFiRadio` already calls the shared helper in
`shared/esp32_bin_loader_return.h`. For other apps, include the same helper and
call this near the start of `setup()`:

```cpp
#include "../shared/esp32_bin_loader_return.h"

void setup() {
  esp32BinLoaderReturnToFactoryOnNextBoot();
}
```

Without that call, the ESP32 OTA boot data keeps booting the last launched app
until another firmware changes the boot partition.

## Preparing A Radio App Bin

Compile `ESP32WiFiRadio` and copy the app binary to the loader SD card as
`/apps/radio_lvgl.bin`. Do not use the merged image, bootloader image, or
partition image; the loader expects the sketch app `.bin`.
