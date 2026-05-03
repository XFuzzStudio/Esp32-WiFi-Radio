# ES8311 Audio Notes

The LCDWiki ES3C28P board exposes a speaker connector and uses an ES8311 audio
path. TamaFi currently uses this path for simple tone output, not for a full
music/speech pipeline.

## Pins

| Function | GPIO | Notes |
| --- | ---: | --- |
| Audio enable | GPIO1 | Active low |
| MCLK | GPIO4 | I2S master clock |
| BCLK | GPIO5 | I2S bit clock |
| LRCK / WS | GPIO7 | Word select |
| DOUT | GPIO8 | ESP32-S3 to codec ES8311 DIN, validated by tone probe |
| DIN | GPIO6 | Codec ES8311 DOUT to ESP32-S3 |

## Bring-Up Rules

1. Set GPIO1 as output.
2. Drive GPIO1 low to enable the audio output path.
3. Start I2S with MCLK/BCLK/DOUT/LRCK/DIN pins above.
4. Start with 16-bit PCM and `16000` Hz sample rate.
5. Keep volume low for the first smoke test.

## Control Interface

- ES8311 I2C address used by TamaFi: `0x18`
- Touch shares the visible I2C pins on GPIO16/GPIO15, but ES8311 control can be
  initialized through the same `Wire` bus if the schematic routes it there.
- The minimal TamaFi audio implementation writes a compact ES8311 init sequence
  and then sends generated PCM samples through ESP32 Arduino `ESP_I2S`.

## Minimal Tone Strategy

For a first project, do this before building a full audio engine:

```cpp
pinMode(LCDWIKI_ES3C28P_AUDIO_EN, OUTPUT);
digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LCDWIKI_ES3C28P_AUDIO_EN_ACTIVE);

// Then configure ESP_I2S with:
// MCLK = GPIO4, BCLK = GPIO5, DOUT = GPIO8, LRCK = GPIO7, DIN = GPIO6
// Sample rate = 16000 Hz, bits = 16.
```

## Practical Notes

- GPIO1 is easy to forget. If I2S appears to run but the speaker is silent,
  check the active-low enable first.
- If I2S counters move and ES8311 ACKs but output is silent, verify DOUT first.
  On this board the working ESP32-S3 transmit data pin is GPIO8.
- Use a blocking generated square/sine tone as a smoke test before decoding WAV
  or streaming audio.
- The horn connector is intended for a small speaker. Do not drive external
  equipment directly without checking levels.
