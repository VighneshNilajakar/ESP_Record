# Changelog

All notable changes to this project are documented here.

## 0.2.0 — multi-board architecture

### Added

- `RecorderController`: board-agnostic capture, metering and byte-exact saving.
  Everything a profile repeats identically now lives in one tested place.
- Interfaces the core depends on instead of a chip SDK: `IAudioSource`,
  `IStorageBackend`, `IFileWriter`, `IIndicator`, `IClock`, `IAllocator`,
  `ILogger`, `ICommandTransport`, `IProgressSink`.
- Audio adapters for both core lines: `LegacyI2sAudioSource` (2.x,
  `driver/i2s.h`), `EspI2sAudioSource` (3.x, `ESP_I2S.h`, 32-bit slots),
  `PdmI2sAudioSource` (3.x PDM receive on I2S0).
- Storage adapters `SdMmcStorage` and `SpiSdStorage` over a shared
  `FsStorageBase`, plus `GpioIndicator`, `ArduinoLogger`,
  `ArduinoMillisClock`, `PsramFirstAllocator`, `StreamTransport` and
  `SerialProgressSink`.
- `SourceOutput`, so a source that already produced PCM16 is never shifted a
  second time. This matters for 3.x's `I2S_RX_TRANSFORM_32_TO_16` and for PDM,
  which converts in hardware.
- `CaptureStrategy::Streaming` alongside `FullBuffer`, so boards without PSRAM,
  or with pins that let audio and storage run at once, work without holding a
  whole clip in RAM.
- `RecorderConfig::acceptTruncatedClip`, which keeps a short clip when asked and
  writes a header matching the frames actually captured.
- `CommandConsole`: CRLF-tolerant, case-insensitive, label-validating command
  parsing, moved out of the examples and into the library.
- `profiles/BoardProfiles.h`: named pin maps with their constraints documented.
- Examples `ESP32_I2S_SPI_SD` and `ESP32_S3_I2S_SPI_SD`; the ESP32-CAM example
  now selects its adapter by core version, so all three build on 2.x and 3.x.
- `esp-audio-dataset-recorder.ino` in the repository root: the reference board
  as a single ready-to-flash sketch, including the 4 MHz SDMMC setting and a
  boot-time brownout report. It builds in place without `--library`.
- `platformio.ini` and a CI board-compile matrix via `arduino-cli`.
- `docs/support-matrix.md` and `docs/hardware-checklist.md`.
- `keywords.txt` for Arduino IDE highlighting.
- Host tests: 442 checks over the utilities, converter, controller and console,
  driven by in-memory fakes. No hardware, no test framework dependency.
- Python tests: 21 checks over the WAV tool, including RIFF header
  cross-validation, truncation and trailing-byte detection.

### Fixed

- `CommandConsole` measured each line with `strlen` after writing its first
  character, which walked past the terminator into uninitialised memory and
  scrambled every command.
- `buildClipPath` produced `//label.001.wav` for the card root.
- Clip indices past 999 produced corrupt filenames containing non-digit
  characters. Three- and four-digit indices are now handled consistently, and
  four-digit names written by 0.1.x keep working.
- `parseClipName` returned a pointer into the filename rather than a
  NUL-terminated label, so every label comparison failed and clip numbering
  always restarted at 001.
- An over-long console line was silently truncated, turning a mistyped label
  into a different, valid one. It is now rejected.
- Writers are deleted on every controller path, including failures.

### Fixed during hardware bring-up

These only surface when the code is actually compiled by Arduino-ESP32 and run,
which is why the CI board matrix was added alongside them.

- `ArduinoLogger::log` printed no line terminator, so the banner, `STATUS` and
  every other message ran together into one unreadable line on the serial
  console.
- `ByteArrayPrint` did not implement `Print::write(uint8_t)`, which is pure
  virtual in the real Arduino `Print`. The host stub had omitted it, so the
  host tests could not catch this; the stub now mirrors the real interface.
- `PsramFirstAllocator` called a non-existent `heap_caps_get_free_block` with a
  non-existent `ESP_HEAP_CAP_SPIRAM`. The real API is
  `heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)`.
- `LegacyI2sAudioSource` used `I2S_CHANNEL_FMT_ALL`, which does not exist;
  stereo is `I2S_CHANNEL_FMT_RIGHT_LEFT`. Its `i2s_config_t` was also left
  partially uninitialised, feeding stack garbage for `mclk_multiple` and
  `bits_per_chan`.
- `FsStorageBase` asked `fs::FS` for `totalBytes()`/`freeBytes()`, neither of
  which exists on core 2.x (`totalBytes`/`usedBytes` live on `SDMMCFS`/`SDFS`).
  Capacity is now obtained from the concrete bus.
- The examples used `AudioPinMap` unqualified although it lives in
  `AudioDatasetRecorder::profiles`.
- `SpiSdStorage` had never been compiled, because only the SD_MMC example ran
  on hardware. It initialised the inherited `mounted_` in its constructor's
  member-initialiser list, which is not legal, and passed three pin numbers to
  `SD.begin()`, whose second parameter is a `SPIClass&`, not a pin. The bus is
  now configured with `SPI.begin(sck, miso, mosi, cs)` and handed to
  `SD.begin(cs, SPI)`.
- The CI board matrix named `esp32:esp32:esp32dev`, which is PlatformIO's board
  ID. Arduino-ESP32 calls the generic board `esp32:esp32:esp32`, so that row
  could never have installed a usable target.

### Fixed by compiling the 3.x targets

The 2.x path had been built and run; the 3.x path had only ever been written.
Compiling it found that it did not build at all, for four independent reasons.

- `CoreVersion.h` tested `ESP_ARDUINO_VERSION_MAJOR` without including the
  header that defines it. An adapter `.cpp` includes its own header before
  `Arduino.h`, so it concluded "core 2.x" while the sketch concluded "core 3.x":
  the 3.x adapter compiled itself out and the link failed on an undefined
  vtable. The version header is now included from `CoreVersion.h` itself, and
  the comment that claimed 2.x lacks that header is corrected -- 2.0.14 has it.
- `EspI2sAudioSource` used `I2SClass::setPort()` and passed a role as a sixth
  argument to `begin()`. Neither exists in 3.0.2: the class chooses the port
  and `begin()` takes five arguments. `configurePort()` is gone, since it could
  not do anything on that API.
- `PdmI2sAudioSource` was compiled unconditionally for 3.x, but PDM receive is
  an ESP32-S3 capability in IDF 5.1; the S2, C3 and C6 have no PDM RX and no
  `setPinsPdmRx`. The adapter is now gated on `SOC_I2S_SUPPORTS_PDM_RX`, and the
  S3 example refuses to build with a clear message where PDM is unavailable.
- `SerialProgressSink` formatted 32-bit values with `%u`, which is a
  `-Wformat` warning on 3.x where those typedefs are `unsigned long`.

The CI matrix also named four boards by their PlatformIO identifiers
(`esp32dev`, `esp32-s3-devkitc-1`, `esp32-c3-devkitm-1`, `esp32-s2-saola-1`,
`esp32-c6-devkitc-1`), none of which Arduino-ESP32 knows. The correct FQBNs are
`esp32`, `esp32s3`, `esp32c3`, `esp32s2` and `esp32c6`. The C3, S2 and C6 rows
now compile the adapter set from `tests/board/AdapterCompileCheck` instead of a
sketch whose GPIO numbers cannot exist on those chips.

### Fixed in the PlatformIO configuration

`platformio.ini` had never been run, and could not have built anything. Seven
environments now build: `esp32cam_core2`, `esp32dev_core2`, `esp32dev_core3`,
`esp32_s3_core3`, and the C3, S2 and C6 adapter checks. The 2.x environments use
espressif32 6.5.0; the 3.x ones use pioarduino 54.3.20, which ships
Arduino-ESP32 3.2.0, a newer core than the CI matrix tests.

- `src_dir` is a project-level option. Inside `[env:...]` PlatformIO ignores it
  with a warning, so the file built the library's `src/` as if it were the
  sketch and failed to link `setup()`. Each environment now names its sketch in
  `custom_example`, and `tools/pio_select_example.py` rewrites
  `PROJECT_SRC_DIR` at build time.
- `lib_extra_dirs` and `extra_scripts` are `[env]` options; in `[platformio]`
  they are silently unknown. The repository root is now an extra library
  directory, which is what compiles the shared sources and puts their headers on
  the include path.

### Changed by the first hardware measurements

- The reference ESP32-CAM profile now reads the **right** I2S slot. With L/R
  tied to GND the left slot read digital silence on the reference board, while
  the right slot carried the signal. `ADR_I2S_SLOT_RIGHT=0` restores the old
  behaviour.
- The PCM alignment default moved from a right shift of 14 to 16. The INMP441
  is MSB-aligned in a 32-bit slot, and 14 clipped 571 of 80000 frames.
- `SdMmcStorage::configure` accepts an SDMMC clock in kHz. At the 20 MHz default
  the first write browned out the reference board; 4 MHz completed the same
  write. The ESP32-CAM example and the root sketch now default to
  `ADR_SD_FREQUENCY_KHZ=4000`; 0 restores the 20 MHz board default.

### Changed

- `AudioStats` gained `sum`, `mean()` and `normalizedMean()` for DC-offset
  diagnosis.
- The reference ESP32-CAM example is now a thin wiring layer over the shared
  core instead of a self-contained 471-line sketch.

### Compatibility

- `AudioDatasetRecorder::AudioStats`, `i2sWordToPcm16` and
  `writePcm16WavHeader` keep their 0.1.x signatures and semantics. Existing code
  that only used those three still compiles.
- Clip filenames are unchanged for indices up to 999.

### Not claimed

- Capture, metering and saving were verified on one reference ESP32-CAM:
  `RECORD` reported 80000/80000 frames in 5008 ms and `/test.001.wav` was
  written at 160044 bytes and listed back. The root sketch was later run for the
  full 20 s clip (320000/320000 frames, `clipped 0`), and two consecutive
  640044-byte saves completed at 4 MHz with no brownout reset. Every other
  board, and every 3.x build, is compile-tested only; no 3.x board was
  available. See `docs/support-matrix.md` for the measurements and
  `docs/hardware-checklist.md` for the wiring that produced them.

## 0.1.0 — initial GitHub-ready baseline

- Extract PCM16 saturation, audio metering and WAV-header writing into a small Arduino utility library.
- Refactor the AI-Thinker ESP32-CAM/INMP441 reference sketch to use the library.
- Make duration, audio format, pins, indicator polarity, startup-discard interval and PCM word shift compile-time configurable.
- Make labels generic while blocking unsafe filename characters.
- Correct stale 20-second and fixed-size log messages; latest supplied source's 60-second duration remains the example default.
- Add WAV diagnostics, board/hardware docs, Edge Impulse workflow, troubleshooting guide and a detailed coding-agent prompt.
- No hardware tests are claimed for this packaged refactor. The user previously reported a successful save with GPIO12 DOUT in their setup.