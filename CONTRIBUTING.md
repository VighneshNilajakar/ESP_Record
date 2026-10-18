# Contributing

Thanks for helping make audio dataset capture repeatable across boards.

## Before submitting a board profile

- Name the exact board/SoC and Arduino-ESP32 or ESP-IDF version.
- Add a dedicated example directory and a configuration/header for that profile.
- Verify that all microphone and storage pins are actually exposed and not reserved for flash, PSRAM, camera, boot strapping, USB or onboard peripherals.
- Document I2S/PDM slot width, valid-bit alignment, channel select, sampling rate and conversion scaling.
- Specify whether the profile buffers the whole clip, uses a ring buffer, or streams to storage.
- Record exact compile commands and actual hardware tests; separate automated compilation from physical verification.
- Add a test WAV and serial log only if licensing/privacy permits. Do not commit private speech recordings by default.

## Running the checks locally

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I tests/native_stubs -I tests -I src \
    $(find src -name '*.cpp') tests/test_*.cpp -o build/test_adr && ./build/test_adr
python -m unittest discover -s tests -p "test_*.py" -v
```

The host suite needs a C++17 compiler and nothing else. CI additionally
cross-compiles every declared board/core example with `arduino-cli`.

## Code standards

- Keep the portable core free of Arduino and Espressif headers. That is what
  lets it be tested on a host, and it is the single most useful property of this
  codebase.
- Implement a new board by writing an adapter, not by adding `#if` to the core.
  Prefer small interfaces and composition over conditional compilation.
- Prefer explicit names and fixed-width integer types.
- Validate every length before allocation, and use 64-bit intermediates when
  calculating file sizes.
- Check partial reads and writes. Always release peripherals and turn off LEDs
  after errors.
- Report every byte count a backend actually accepted, never an assumed one.
- Do not disable brownout protection or hide capture faults with automatic
  filtering.
- Test generated WAV files with an independent validator.
- Update README, the support matrix, troubleshooting and CHANGELOG whenever
  behaviour changes.

## Honesty requirements

This project treats an unverified claim as a defect. Please do not overstate
what you have done.

- Never claim a board "works" on the strength of a successful compile. A build
  is not a capture.
- Do not claim a board works unless you completed
  [docs/hardware-checklist.md](docs/hardware-checklist.md) for it and recorded
  what you observed.
- A full frame count does not prove a valid microphone signal. Silence, the
  wrong channel, a wrong alignment and a disconnected data line all still
  produce a full frame count. Confirm the levels.
- When you verify something, update `docs/support-matrix.md` and the checklist in
  the same commit. Leaving them out is how a support matrix becomes fiction.

## Pull request checklist

- [ ] `g++ -Wall -Wextra -Werror` host tests and the Python tests pass.
- [ ] Builds on the listed core/board via CI or `arduino-cli`.
- [ ] WAV header matches payload, sample rate, channel count and bit depth.
- [ ] No overflow or unchecked allocation.
- [ ] Storage and I2S resources are cleaned up on all error paths.
- [ ] Pin conflicts, power needs and boot-strapping pins documented.
- [ ] Audio levels and clipping checked with real sample(s), if hardware is available.
- [ ] Unverified facts and target combinations are labeled honestly.
