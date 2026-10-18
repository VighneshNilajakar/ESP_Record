# Coding Agent Brief: Turn this into a reusable ESP Audio Dataset Recorder

You are a senior embedded audio engineer and Arduino/ESP-IDF library maintainer. Work directly in this repository and turn the current reference implementation into a maintainable, testable, reusable audio dataset recording toolkit for ESP boards, with explicit board profiles and adapters. Do not merely describe changes: inspect the repository, implement changes, update documentation, and report exactly what was compiled and tested.

## Goal

Refactor the working prototype into a GitHub-quality open-source project for recording labeled microphone samples as WAV files for TinyML and audio-classification datasets. It must be easy to adapt across ESP32 family boards without falsely claiming that one pin map or I2S implementation works unchanged on every board.

## Starting point and user context

The current example derives from a sketch used for HOTPIN wake-word training. The source is in `examples/AIThinker_ESP32_CAM_INMP441_SDMMC/AIThinker_ESP32_CAM_INMP441_SDMMC.ino`.

Known target setup:

- Board: AI-Thinker ESP32-CAM, original ESP32 (not ESP32-S3), with camera not used during this recording workflow.
- Arduino-ESP32 core: legacy code was originally used with 2.0.16; implementation includes `driver/i2s.h`.
- Microphone: INMP441, 3.3 V, GND, BCLK GPIO14, WS GPIO15, DOUT GPIO12, L/R tied to GND (left slot).
- Storage: board's onboard microSD socket with SD_MMC in one-bit mode. Because GPIO14/15 are shared between I2S and SD_MMC, capture to PSRAM, uninstall I2S, then mount/write the SD card.
- Recording LED: GPIO4 flash LED, active high during recording; SD_MMC is not mounted while the LED is used as a GPIO.
- Available exposed pins reported by user: GPIO0, 1, 2, 3, 4, 12, 13, 14, 15, 16. GPIO32 is not exposed on their module. GPIO16 may be occupied by PSRAM and must not be assumed safe.
- PSRAM: user board reports 4 MB; the example allocates the whole clip in PSRAM.
- Serial: 115200 baud, line ending Newline.
- WAV output: mono PCM16 LE, currently configured 16 kHz; 60 seconds in the most recently supplied sketch (prior project iteration used 20 seconds, so duration must be configurable).
- Commands should include `RECORD <label>`, `SAVE`, `LIST`, `STATUS`, `HELP`. Labels may default to `hey_hotpin`, `noise`, and `unknown`, but the library should not hard-code those only; sanitize labels to safe filename characters.
- HOTPIN's Edge Impulse dataset uses labels `hey_hotpin`, `noise`, `unknown`; the project's model workflow currently splits recordings into 1,000 ms segments.

## Observed user-reported behavior (treat as evidence, not universal truth)

- The original ESP32-CAM could capture the requested number of I2S samples and then reset with `E BOD: Brownout detector was triggered` when mounting the card in one configuration.
- Moving microphone DOUT from GPIO13 to GPIO12 resulted in a successful recording save in the user's setup, with a complete file (for 20 seconds at 16 kHz mono PCM16: 640,044 bytes including 44-byte header). GPIO12 is also an original ESP32 strapping pin and the SD DAT2 connection, so this wiring remains board-specific and should be documented as an observed workaround, not a universal recommendation.
- The user later shared `noise.002.wav` and reported it sounded very noisy. Earlier versions applied aggressive digital gain. The current code reports peak, RMS and clipped sample count, and has a configurable raw-word right shift. Some prior audio showed regular click-like artifacts, so the I2S data alignment/DMA/block handling should be investigated rather than trying to hide artifacts with gain/filtering.
- Do not disable brownout detection in production code. Keep supply/power troubleshooting in docs.
- Counted frames by themselves do not prove a valid microphone signal: zeros, wrong channel, wrong alignment, or disconnected data can still yield a full sample count.

## Critical scope requirement: “any ESP board” means a framework plus tested profiles

Do not claim a universal build across all boards. I2S/PDM capabilities, GPIO availability, flash/PSRAM pins, I2S driver APIs, and storage controllers vary by SoC and core version. Build a portable recording core and add adapters/profiles as capabilities permit. Maintain a support matrix with exact board, SoC, Arduino-ESP32/core version, microphone interface, storage bus, and test evidence.

First-class targets to consider, subject to actual compile tests:

1. Original ESP32 + Arduino-ESP32 2.x legacy I2S API (preserve reference profile).
2. ESP32 / ESP32-S3 + Arduino-ESP32 3.x `ESP_I2S.h` API (separate adapter; use current official docs).
3. Generic ESP32-family board + external I2S microphone + SD over SPI (separate storage adapter to avoid SD_MMC pin conflicts).
4. Boards with no PSRAM: short-clip or bounded ring-buffer/stream-to-storage strategy, not mandatory full-clip allocation.
5. Other SoCs only when their I2S/PDM and storage capability is verified. Use SPI SD where SD_MMC is unavailable.

Official references to consult and cite in docs/source comments where useful:
- Arduino-ESP32 I2S API: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2s.html
- Arduino-ESP32 SD_MMC wiring/mode restrictions: https://github.com/espressif/arduino-esp32/blob/master/libraries/SD_MMC/README.md
- Arduino-ESP32 SPI SD documentation: https://github.com/espressif/arduino-esp32/blob/master/libraries/SD/README.md
- INMP441 datasheet/channel and 24-bit data format: locate and cite a trustworthy manufacturer datasheet.

## Required architecture

Keep hardware dependencies out of the portable core. Use clear interfaces along these lines (adjust names as the implementation warrants):

- `IAudioSource`: begin, read frames with an explicit result/status and timeout, end. The interface must define if it returns PCM frames or raw I2S words. Do not hide microphone-specific bit alignment.
- `IStorageBackend`: begin, end, exists, open/create, write, flush, close, remove, available space. Check every write and handle errors.
- `IIndicator`: idle/recording/success/error indication, active level configurable.
- Recorder/controller: validates user labels, duration/sample rate, memory sizing, capture state, timer, stats, save/retry behavior.
- Portable audio utilities: PCM conversion, saturating gain, stats (peak/RMS/clipped count/DC offset optional), RIFF/WAV header, file-size checks.

Prefer small interfaces and composition over a giant `#if` block. Avoid dynamic allocation for small control objects. Use `String` only where reasonable for Arduino; prevent path traversal and validate labels even when command input is malformed. Use integer overflow-safe arithmetic for clip-size calculations.

## Functional requirements

- User-configurable sample rate (at least common values such as 8/16/22.05/32/44.1/48 kHz where hardware permits), duration, channels when supported, PCM output width (minimum PCM16), label, gain/scaling policy, input pins, indicator pin and active level.
- Serial command interface or equivalent API. Commands must be documented and robust to LF/CRLF, spaces and invalid labels.
- Print a once-per-second elapsed timer and clear start/stop/status messages.
- Always turn off the indicator and release audio drivers on all success/error paths.
- Report actual frames captured, elapsed wall time, peak/RMS and clipped samples. Optionally detect zero/silent buffers and warn, but do not assert that low amplitude always means failure.
- WAV PCM header values must match actual payload, sample rate, channel count and bit depth. Verify file size and don't report success if anything is incomplete.
- Support storage failure recovery when data remains in RAM. Clearly state that a reset or power loss discards RAM-pending audio.
- Make capture strategy configurable (full-clip RAM, ring-buffer, direct stream) and select it only if hardware pin conflicts and storage semantics permit.
- Do not insert automatic aggressive gain or denoising as a blanket default. Default to transparent sample conversion, with optional gain and clip-safe saturation. Document the gain math and signal alignment assumptions.
- Do not disable brownout protection to get around bad power.
- Keep camera use explicitly disabled/unsupported in the ESP32-CAM audio-only profile if pins or power would conflict.

## Audio correctness requirements

- Investigate actual INMP441 word alignment and left/right slot format from the datasheet plus each API; document expected valid bits and add a raw capture/diagnostic mode.
- Avoid assuming `raw >> 14` is universal. If using the new Arduino ESP_I2S API's conversion options, ensure the application does not shift already converted PCM16 a second time.
- Avoid block-boundary clicks, repeated DMA-block artifacts, sign/wrap errors, channel swapping, endian problems and full-buffer race conditions.
- Treat gain as scaling, not denoising. Peak/RMS/clipping and optional WAV plots should make signal quality testable.
- Add a host-side WAV validator that checks RIFF header fields against payload length and reports format/levels.

## Documentation deliverables

- Root README quick start and feature/support matrix.
- Architecture/design doc with adapter contracts and capture strategies.
- Board profile/hardware wiring table including every pin conflict, strapping/reserved pins, pull-ups and board revisions as applicable.
- Setup/build docs for Arduino IDE and (if added) PlatformIO.
- Audio quality guide: gain vs noise floor, sample rate, channel selection, raw-word alignment, clipping, periodic-click troubleshooting.
- Edge Impulse guide: label collection, recording sessions, sample splitting, train/test leakage risk, realistic negatives.
- Troubleshooting page for silent audio, noisy audio/clicks, SD errors, power brownouts, no PSRAM, invalid WAV.
- `CONTRIBUTING.md`, license, changelog, issue templates and CI config where appropriate.
- A clear status marker for anything not physically tested. Never claim a board compiles/works unless evidence exists.

## Tests / CI required

- Host unit tests for PCM saturation and WAV header bytes/sizes where feasible.
- Python tests for WAV validation/diagnostic utility.
- Compile each declared supported board/core combination using arduino-cli or PlatformIO CI if installable and reliable.
- Check mono and stereo sizes, duration variation, boundary values, clipping counters, invalid labels, short writes, failed mounts, and retry paths.
- Keep hardware validation as a separately tracked manual test checklist (mic captured while speaking, silence/noise capture, WAV opens in common audio software, expected duration, no clicks, SD remount/write/power-cycle).

## Repository quality and workflow

- Keep code files small and named consistently.
- Add examples as separate sketch folders so Arduino IDE can open them.
- Add `.gitignore`, MIT license unless repo owner chooses another, and GitHub Actions.
- Never commit user recordings, serial logs containing secrets, API keys, Wi-Fi credentials, or generated build products.
- Update library version and CHANGELOG with behavior changes.
- Keep backwards compatibility for the working ESP32-CAM profile unless a bug fix is needed; explain breaking changes.
- If using external libraries, pin/document versions and justify each dependency.
- Build the actual code; review the diff; fix warnings/errors introduced by the change.

## Definition of done

1. Current ESP32-CAM reference behavior remains documented and its code is no less clear than before.
2. Portable core and hardware adapters have a well-defined contract.
3. Supported matrix is honest and evidence-based.
4. Code compiles for every declared compile-tested target; CI runs applicable host checks.
5. Documentation describes all wiring and known constraints, especially GPIO12 strapping and SD_MMC/I2S sharing.
6. WAV diagnostics give a reproducible way to troubleshoot the user's quiet/noisy audio.
7. Final agent response includes summary, changed files, exact build/test commands and results, open hardware checks, and a truthful list of unverified boards.

Now inspect the code and repository, implement the best coherent version of this design, and do not stop after writing a plan.
