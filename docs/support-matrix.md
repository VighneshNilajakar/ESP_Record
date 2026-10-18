# Support matrix

This project aims to work on as many ESP-family boards as it honestly can, which
is **not** the same as claiming one sketch works everywhere. What follows is what
is actually evidenced. Rows marked *not validated* compile but have never been
run on the board by this project.

## What the two automated gates prove

| Gate | Proves | Does not prove |
|---|---|---|
| Host tests (`g++ -Werror`, 442 checks) | The portable core is warning-free and logically correct on a host compiler | Anything about a chip, a peripheral, or a pin |
| Board compiles (`arduino-cli`, matrix in `.github/workflows/tests.yml`) | Each declared board/core/example combination builds | That audio is captured correctly |

Correctness on hardware is tracked by hand in
[hardware-checklist.md](hardware-checklist.md). No claim below outruns those
three sources of evidence.

## Core version matrix

| Arduino-ESP32 core | I2S API used | Adapter | Status |
|---|---|---|---|
| 2.x | `driver/i2s.h` | `LegacyI2sAudioSource` | **Reference.** User-observed successful save on ESP32-CAM |
| 3.x | `ESP_I2S.h` | `EspI2sAudioSource`, `PdmI2sAudioSource` | Compile-tested only |

The two APIs are incompatible, so the adapter is chosen at compile time in
`src/adapters/CoreVersion.h`. Nothing else in the library knows which core it is
running on.

## Board matrix

| Board | SoC | Audio | Storage | Strategy | Status |
|---|---|---|---|---|---|
| AI-Thinker ESP32-CAM | ESP32 | INMP441, I2S | on-board SD_MMC, 1-bit | Full buffer | **Reference.** User reports a successful save. Not CI-verified on hardware |
| Generic ESP32 DevKit | ESP32 | I2S or PDM | SPI microSD | Streaming | Compile-tested on `esp32dev`, 2.x and 3.x |
| ESP32-S3 | ESP32-S3 | I2S or PDM | SPI microSD | Streaming | Compile-tested on `esp32-s3-devkitc-1`, core 3.x |
| ESP32-C3 | ESP32-C3 | I2S or PDM | SPI microSD | Streaming | Compile-tested on `esp32-c3-devkitm-1`. Single I2S controller |
| ESP32-S2 | ESP32-S2 | I2S | SPI microSD | Streaming | Compile-tested on `esp32-s2-saola-1` |
| ESP32-C6 | ESP32-C6 | I2S or PDM | SPI microSD | Streaming | Compile-tested on `esp32-c6-devkitc-1` |

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