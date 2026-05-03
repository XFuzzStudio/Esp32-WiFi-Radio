# LCDWiki ESP32-S3 ES3C28P Pinout

Board: LCDWiki 2.8inch ESP32-S3 Display, touch SKU `ES3C28P`.

## Display - ILI9341V SPI TFT

| Function | ESP32-S3 GPIO | Notes |
| --- | ---: | --- |
| LCD CS | GPIO10 | Chip select, active low |
| LCD DC | GPIO46 | Command/data select |
| LCD SCK | GPIO12 | SPI clock |
| LCD MOSI | GPIO11 | SPI write data |
| LCD MISO | GPIO13 | SPI read data |
| LCD RST | EN / `-1` in code | Shared with ESP32-S3 reset |
| LCD BL | GPIO45 | Backlight, active high |

Display parameters:

- Driver IC: `ILI9341V`
- Interface: 4-wire SPI
- Resolution: `240x320`
- Recommended rotation for TamaFi profile: `0`
- Required color inversion in tested TamaFi port: enabled
- Tested SPI clock: `27000000` Hz

## Touch - D-FT6336G I2C

| Function | ESP32-S3 GPIO | Notes |
| --- | ---: | --- |
| Touch SDA | GPIO16 | I2C data |
| Touch SCL | GPIO15 | I2C clock |
| Touch RST | GPIO18 | Reset, active low |
| Touch INT | GPIO17 | Interrupt, active low on touch |

Touch parameters:

- Controller: `D-FT6336G`
- I2C address: `0x38`
- Valid area: `240x320`
- Suggested I2C clock: `400000` Hz

## RGB LED

| Function | ESP32-S3 GPIO | Notes |
| --- | ---: | --- |
| RGB data | GPIO42 | One addressable RGB LED |

Recommended NeoPixel settings:

- Count: `1`
- Color order: `NEO_GRB`
- Bitrate: `NEO_KHZ800`

## Audio - ES8311 / I2S

| Function | ESP32-S3 GPIO | Notes |
| --- | ---: | --- |
| Audio enable | GPIO1 | Active low: low = enabled, high = disabled |
| I2S MCLK | GPIO4 | Master clock |
| I2S BCLK | GPIO5 | Bit clock |
| I2S LRCK / WS | GPIO7 | Word select / left-right clock |
| I2S DOUT | GPIO8 | ESP32-S3 to codec ES8311 DIN, validated by tone probe |
| I2S DIN | GPIO6 | Codec ES8311 DOUT to ESP32-S3 |

Audio parameters:

- Codec: `ES8311`
- Control bus: I2C, address commonly `0x18`
- Output path uses the horn/speaker connector
- Start with mono/stereo 16-bit PCM at `16000` Hz for bring-up

## MicroSD - SDIO

| Function | ESP32-S3 GPIO | Notes |
| --- | ---: | --- |
| SD CLK | GPIO38 | SDIO clock |
| SD CMD | GPIO40 | SDIO command |
| SD D0 | GPIO39 | Data 0 |
| SD D1 | GPIO41 | Data 1 |
| SD D2 | GPIO48 | Data 2 |
| SD D3 | GPIO47 | Data 3 |

## System And Expansion

| Function | ESP32-S3 GPIO | Notes |
| --- | ---: | --- |
| BOOT key | GPIO0 | Download mode key, can be used as input if needed |
| UART0 RX | GPIO43 | USB/serial RX |
| UART0 TX | GPIO44 | USB/serial TX |
| Battery ADC | GPIO9 | Battery voltage sensing input |
| Expansion | GPIO2 | External header |
| Expansion | GPIO3 | External header |
| Expansion | GPIO14 | External header |
| Expansion | GPIO21 | External header |

## Reserved Pins To Treat Carefully

- GPIO45 drives the display backlight.
- GPIO46 is used by the LCD as DC. It is a strapping/input-capable pin on some
  ESP32-S3 designs, so do not repurpose it casually.
- GPIO47 and GPIO48 are used by SDIO data lines and may be unavailable if SD is
  enabled.
- EN is shared with the display reset path.
