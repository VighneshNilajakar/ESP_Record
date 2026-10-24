# Troubleshooting

## Recorder compiles but no microphone signal changes

- Confirm INMP441 VDD is 3.3 V and all grounds are common.
- Verify BCLK, WS and DOUT pins against the active profile.
- Confirm L/R selection matches the configured I2S slot. On the reference board L/R is tied to GND but the data appears in the **right** slot; set `I2S_SLOT=left` only if a scan shows the left slot is live.
- Confirm the driver is actually receiving 32-bit words, and determine where the valid microphone bits are located.
- Temporarily print/min/max of raw I2S words in a short diagnostic build. Avoid printing every sample, which can disturb capture timing.

## WAV is silent / Edge Impulse won't split it

- Check the file with `python tools/analyze_wav.py filename.wav`.
- Confirm file format is mono PCM16 at the expected rate, with file size `44 + frames * 2` for mono.
- Compare peak and RMS; near-zero values indicate a capture/conversion issue, not just low playback volume.
- Verify the microphone channel, data pin, and correct signed bit shift before applying gain.
- Do not trust a complete sample count as proof of valid audio: a disconnected or incorrectly configured input can still deliver zeros.

## WAV sounds noisy or has repeated clicks

- Lower digital gain if clipped; gain cannot remove the noise floor.
- Improve microphone power/ground and shorten loose jumper wires; keep BCLK away from noisy power/USB wiring.
- Check raw I2S conversion, selected channel, core version, DMA buffers and periodic read behavior.
- Do not filter out clicks without diagnosing regular block-boundary faults first.

## Brownout when mounting/writing SD card

- Keep brownout detection enabled during regular use.
- Try a known-good regulated 5 V supply with enough current and short leads; measure the rail during SD initialization if possible.
- Avoid feeding the board from two independent 5 V outputs at once.
- Try a second known-good FAT32 microSD card.
- Ensure I2S has been uninstalled before SD_MMC mount on the ESP32-CAM profile.
- If a brownout is still present with a stable measured rail, inspect card wiring and hardware for shorts/current issues.

## Board doesn't boot after rewiring GPIO12

GPIO12 is an original ESP32 strapping pin. Power off before disconnecting DOUT, then retry boot. Another profile/pin plan may be needed for that board revision.

## Arduino-ESP32 3.x

3.x builds select `EspI2sAudioSource` (the `ESP_I2S.h` API) automatically; 2.x
builds use `LegacyI2sAudioSource` (`driver/i2s.h`). Both paths are compile-tested,
but only the 2.x path has been run on the reference board here. If a 3.x build
fails, check `src/adapters/CoreVersion.h` and the adapter's `begin()` arguments
against the core version, not the sketch. See
https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2s.html.
