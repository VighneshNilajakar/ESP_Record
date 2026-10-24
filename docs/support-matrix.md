# Support matrix

This project aims to work on as many ESP-family boards as it honestly can, which
is **not** the same as claiming one sketch works everywhere. What follows is what
is actually evidenced. Every row not marked **Verified on hardware** compiles
but has never been run on that board by this project.

## What the two automated gates prove

| Gate | Proves | Does not prove |
|---|---|---|
| Host tests (`g++ -Werror`, 442 checks) | The portable core is warning-free and logically correct on a host compiler | Anything about a chip, a peripheral, or a pin |
| Board compiles (`arduino-cli`, matrix in `.github/workflows/tests.yml`) | Each declared board/core/example combination builds | That audio is captured correctly |

The seven PlatformIO environments in `platformio.ini` also build locally:
espressif32 6.5.0 for the 2.x rows and pioarduino 54.3.20 (Arduino-ESP32 3.2.0)
for the 3.x rows. That is a second toolchain, not a second gate: it proves the
same thing the arduino-cli matrix proves.

Correctness on hardware is tracked by hand in
[hardware-checklist.md](hardware-checklist.md). No claim below outruns those
three sources of evidence.

## Core version matrix

| Arduino-ESP32 core | I2S API used | Adapter | Status |
|---|---|---|---|
| 2.x | `driver/i2s.h` | `LegacyI2sAudioSource` | **Verified on hardware** (ESP32-CAM, capture + byte-exact save) |
| 3.x | `ESP_I2S.h` | `EspI2sAudioSource`, `PdmI2sAudioSource` | Compile-tested only |

The two APIs are incompatible, so the adapter is chosen at compile time in
`src/adapters/CoreVersion.h`. Nothing else in the library knows which core it is
running on.

## Board matrix

| Board | SoC | Audio | Storage | Strategy | Status |
|---|---|---|---|---|---|
| AI-Thinker ESP32-CAM | ESP32 | INMP441, I2S | on-board SD_MMC, 1-bit | Full buffer | **Verified on hardware** (see measurements below) |
| Generic ESP32 DevKit | ESP32 | I2S or PDM | SPI microSD | Streaming | Compile-tested as `esp32:esp32:esp32` (ESP32 Dev Module), 2.x and 3.x |
| ESP32-S3 | ESP32-S3 | I2S or PDM | SPI microSD | Streaming | Compile-tested on `esp32s3`, core 3.x |
| ESP32-C3 | ESP32-C3 | I2S or PDM | SPI microSD | Streaming | Adapter set compiles for `esp32c3`; no runnable sketch yet |
| ESP32-S2 | ESP32-S2 | I2S | SPI microSD | Streaming | Adapter set compiles for `esp32s2`; no runnable sketch yet |
| ESP32-C6 | ESP32-C6 | I2S or PDM | SPI microSD | Streaming | Adapter set compiles for `esp32c6`; no runnable sketch yet |

### ESP32-CAM bring-up measurements (2026-10)

Recorded on the reference board so that later changes have something concrete to
disagree with. Chip: ESP32-D0WDQ6-V3 (revision v3.1), Arduino-ESP32 2.0.14,
FQBN `esp32:esp32:esp32cam`, SD_MMC in 1-bit mode, 16 kHz mono PCM16.

| Observation | Result |
|---|---|
| Capture timing | 80000/80000 frames in 5008 ms (5 s at 16 kHz) |
| Capture timing, 20 s | 320000/320000 frames in 20000-20001 ms, two runs, `clipped 0` |
| Save | 160044-byte WAV written, verified, and listed by `LIST` |
| Save at 4 MHz, 20 s | Two consecutive 640044-byte WAVs written and listed, no reset; write took ~15.4 s |
| Gain at shift 16 | peak 8777, RMS 797.2, DC -15.3 on the first 20 s clip; peak 2001, RMS 139.3 on the second |
| I2S slot | Data appears in the **right** slot although INMP441 L/R was tied to GND; the left slot read exactly `0` for every frame |
| Data pin | GPIO12 is the live data line. A scan of 12/13/2/4 found signal only on 12 |
| Alignment | 24-bit data is MSB-aligned in the 32-bit slot, so the correct 16-bit reduction is a right shift of 16 |
| Gain at shift 14 | peak 32768, 571 of 80000 frames clipped |
| SD mount | One 16 GB card timed out with `sdmmc_init_ocr: send_op_cond ... 0x107`; a second card mounted normally, so that failure was card-specific |
| SD write current | At the default 20 MHz clock the first write triggered the brownout detector; at 4 MHz the same 160 KB write completed |
| Reset lines | DTR/RTS are not wired on this setup: the board cannot be reset into the bootloader by the adapter |

The WAV writer was also cross-checked on the PC: files it produced were opened
with Python's standard `wave` module at one frame, 80000 frames mono at 16 kHz
and 1000 frames stereo at 44.1 kHz, and header fields and frame counts matched
every time. That checks the format, not the audio.

Two of these are firmware-relevant beyond this board. The slot result contradicts
the INMP441 datasheet's L/R=GND => left channel, so the example defaults to the
right slot and exposes `ADR_I2S_SLOT_RIGHT` to override it. The brownout is a
supply problem: a 4 MHz SD clock is a stopgap, and the real fix is 100-470 uF of
bulk capacitance close to the board.

### Known constraints that are not negotiable

- **The original ESP32 has an I2S hardware v1 defect.** Its FIFO packing
  scrambles 8-bit and 16-bit *mono* receive data. Every I2S adapter here is
  therefore configured for **32-bit slots** and converts afterwards, which avoids
  the defect. Do not "optimise" an adapter to 16-bit mono on ESP32 HW v1.
- **PDM receive exists only on I2S port 0**, and the PDM data width is fixed at
  16 bits. `PdmI2sAudioSource` reflects both limits.
- **ESP32 MCLK is restricted to GPIO0, GPIO1 or GPIO3** by the hardware. Leave
  `mclk` at -1 unless you have a specific reason.
- **ESP32-C6, -S2 and -C3 have one I2S controller**, so there is no second
  port for concurrent capture.
- **SD_MMC is not universally available**, and on the ESP32-CAM its pins overlap
  the microphone's. That is the reason `CaptureStrategy::FullBuffer` exists.

### Boards outside this table

Arduino-ESP32 also targets ESP32-C5, ESP32-H2, ESP32-P4, and (as an ESP-IDF
component only) ESP32-C2 and ESP32-C61. They are **not claimed** here: peripheral
counts, pin routing and PDM availability differ per chip, and this project has
neither compiled nor run them. Before adding a row, confirm the chip has an I2S
or PDM-capable peripheral and check its technical reference manual for the pin
constraints, then compile it in `platformio.ini` and the CI matrix.

## Capture strategies

| Strategy | RAM | Pin requirement | Use when |
|---|---|---|---|
| `FullBuffer` | Whole clip (PSRAM preferred) | Audio and storage pins may overlap | Board is pin-constrained, e.g. ESP32-CAM |
| `Streaming` | One DMA block (~1 KB) | Audio and storage pins must be disjoint | Large clips, or a board with no PSRAM |

`FullBuffer` means a reset or power loss between `RECORD` and `SAVE` loses the
clip: RAM does not survive it. `Streaming` avoids that risk but cannot salvage a
short capture, because the WAV header is written before the payload.

## Storage backends

| Backend | Bus | Notes |
|---|---|---|
| `SdMmcStorage` | SD_MMC | 1-bit or 4-bit. Pin availability is chip-specific |
| `SpiSdStorage` | SPI | The portable choice; pins can be routed away from the microphone |

---

Last updated with release 0.2.0. When you verify a board, update this file and
tick the corresponding item in `hardware-checklist.md` in the same commit.