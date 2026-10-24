# Hardware profiles and wiring

## Reference profile: AI-Thinker ESP32-CAM + INMP441

This is the setup the original HOTPIN recorder was developed around.

| INMP441 | ESP32-CAM pin |
|---|---|
| VDD | 3V3 (not 5 V) |
| GND | GND |
| SCK/BCLK | GPIO14 |
| WS/LRCL | GPIO15 |
| SD/DOUT | GPIO12 (user-reported working setup; see warning below) |
| L/R | GND; the reference board still delivered data in the right slot |

Other settings:

- Recording LED: GPIO4, active HIGH in this profile.
- Sample rate: 16,000 Hz.
- Audio output: mono, signed PCM16 little-endian WAV.
- Input I2S slot: 32-bit **right** slot on the reference board (measured; see
  below), then a right shift of 16 to PCM16.
- Storage: onboard microSD through `SD_MMC.begin("/sdcard", true)` (one-bit mode).
- Capture buffer: PSRAM, one whole clip at a time.

## Critical pin caveats

For an original ESP32, the default SD_MMC pins are fixed to GPIO14 (CLK), GPIO15 (CMD), GPIO2 (D0), GPIO4 (D1), GPIO12 (D2), and GPIO13 (D3). In one-bit mode, the active SD signals are CLK/CMD/D0; extra data lines are not used for data transfer but board wiring and pull-ups still exist. See the upstream mapping at https://github.com/espressif/arduino-esp32/blob/master/libraries/SD_MMC/README.md.

That means the reference's I2S clock pins overlap SD_MMC pins. The sketch must stop/uninstall I2S before mounting the card. The user found that moving microphone DOUT from GPIO13 to GPIO12 allowed a WAV to save; this is a hardware observation for that particular setup, not a universally safe mapping.

**GPIO12 is a strapping pin** on the original ESP32 and is also wired to SD DAT2 on the camera board. Different cards, pull-ups, modules and reset conditions may affect boot. If the board stops booting, disconnect the microphone DOUT wire while powered off, then reassess the pin profile. GPIO32 is not broken out on the user's module and should not be assumed available.

GPIO4 drives the flash LED and is also the SD_MMC D1 line. The example only toggles GPIO4 while SD_MMC is unmounted and uses one-bit SD mode when saving. Don't switch to four-bit SD mode without redesigning the pin/LED arrangement.

## Power

Use a regulated 5 V supply with sufficient peak-current capability for the ESP32-CAM and SD-card startup/write loads. Supply ESP32-CAM power through its **5V** pin and ground, not its 3.3 V pin. If UART is connected through an FTDI adapter while a separate 5 V supply powers the board, share ground and TX/RX as required but do not tie two independent 5 V outputs together.

Keep brownout detection enabled in normal operation. A brownout at SD initialization is evidence to check voltage droop, wiring, source capability, card, and peripheral sharing—not a reason to silently disable protection.

On the reference board, writes at the 20 MHz SDMMC default browned out the board. At 4 MHz, two consecutive 20 s clips (640044 bytes each) were written without a reset. That is a measurement of one board on one supply, not a general guarantee: treat 4 MHz as the conservative default and 20 MHz as something to re-test only after the supply has bulk capacitance.

## Porting to other boards

Never copy this pin map to a different ESP board. For every new profile document:

- exact module/SoC/board revision;
- Arduino or ESP-IDF version and driver API;
- microphone model, I2S/PDM mode, slot/word format and channel-select wiring;
- valid exposed GPIOs, input-only/strapping/reserved pins;
- storage backend and bus pins;
- PSRAM presence and memory strategy;
- power and LED polarity;
- compile test and known-good recording validation.
