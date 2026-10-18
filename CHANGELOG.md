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

- No hardware validation was performed for this release. The ESP32-CAM profile
  still rests on the user's earlier report of a successful save. Every other
  board is compile-tested only. See `docs/support-matrix.md`.

## 0.1.0 — initial GitHub-ready baseline

- Extract PCM16 saturation, audio metering and WAV-header writing into a small Arduino utility library.
- Refactor the AI-Thinker ESP32-CAM/INMP441 reference sketch to use the library.
- Make duration, audio format, pins, indicator polarity, startup-discard interval and PCM word shift compile-time configurable.
- Make labels generic while blocking unsafe filename characters.
- Correct stale 20-second and fixed-size log messages; latest supplied source's 60-second duration remains the example default.
- Add WAV diagnostics, board/hardware docs, Edge Impulse workflow, troubleshooting guide and a detailed coding-agent prompt.
- No hardware tests are claimed for this packaged refactor. The user previously reported a successful save with GPIO12 DOUT in their setup.