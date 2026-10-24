# Architecture

The design goal is a single, honest set of rules for recording labelled audio on
ESP-class boards, expressed as small interfaces plus composition, rather than one
large `#if` cascade that claims to support everything and silently does the wrong
thing on half the targets.

## Layers

```text
examples/<profile>/              Board wiring + command entry point
  |
src/adapters/                    The ONLY code that knows a chip exists
  LegacyI2sAudioSource           core 2.x, driver/i2s.h
  EspI2sAudioSource              core 3.x, ESP_I2S.h, 32-bit slots
  PdmI2sAudioSource              core 3.x, PDM RX on I2S0
  SdMmcStorage / SpiSdStorage    Storage buses
  GpioIndicator                  Status LED
  ArduinoEnvironment             millis, Serial, PSRAM-first allocator
  profiles/BoardProfiles.h       Named pin maps (documentation + defaults)
  |
src/RecorderController           Board-agnostic sequencing
src/CommandConsole               Line protocol over a byte transport
  |
src/IAudioSource.h               Interfaces
src/IStorageBackend.h
src/IIndicator.h
src/Services.h                  IClock, IAllocator, ILogger
  |
src/AudioStats.h                 Pure utilities, no Arduino or Espressif headers
src/PcmConvert.h
src/WavWriter.h
src/Labels.h
src/RecorderTypes.h
```

Everything below `RecorderController` compiles on a host with only a nine-line
`Arduino.h` stub. That is not a stunt; it is what makes the core testable on
every commit.

## Interface contracts

### `IAudioSource`

```cpp
Status begin(const AudioFormat&, SampleSlot);
Status readWords(int32_t* dst, size_t capacityWords, size_t& wordsRead, uint32_t timeoutMs);
void end();
bool isRunning() const;
SourceOutput outputFormat() const;   // RawI2sWords or Pcm16
StreamLayout layout() const;         // words per frame, samples per word
```

Three rules an adapter must follow:

1. **It must not rescale samples.** If it hands back raw I2S words, the core
   applies the configured `rightShift`. If the driver already produced PCM16, it
   must say so via `outputFormat()`.
2. **It must not hide microphone data alignment.** The raw-word path exists so
   alignment is one decision, in one place, visible and testable.
3. **It must not pre-select a channel without reporting it.** A driver running in
   mono reports `wordsPerFrame == 1`; an interleaved stream reports 2.

Rule 1 is not theoretical. Arduino-ESP32 3.x can convert 32-bit I2S words to
PCM16 inside the driver via `I2S_RX_TRANSFORM_32_TO_16`. A PDM microphone is
converted to PCM by hardware. Applying the right shift to either of those would
halve the signal, and a mistake like that is easy to make and hard to see.
`SourceOutput` makes the distinction explicit and `tests/test_controller.cpp`
pins it with `testConverterPcm16IsNotShiftedTwice`.

### `IStorageBackend` / `IFileWriter`

Every write reports **exactly how many bytes were accepted**. A backend that
cannot say would force the core to trust it, and a short write is how a WAV ends
up with a header that promises 60 seconds and 3 seconds of audio.

`createWriter` hands ownership of the writer to the caller. The controller
always deletes it, including on failure paths.

### `IIndicator`

A status LED. `setRecording()` holds it on for the capture; `setSuccess()` is
momentary and must end inactive (the GPIO adapter blinks three times), and the
core calls `allOff()` on every exit path, including every failure, so an
interrupted recording cannot leave a pin driven.

### `IClock`

Monotonic and **must advance on its own**. The capture watchdog is the only
thing that ends a stalled recording, so a clock that never moves turns a silent
microphone into an infinite loop.

### `IAllocator`

Injected because the right answer is board-dependent. The ESP32-CAM reference
needs PSRAM for a 60 s clip; a board without PSRAM must shorten the clip or
stream. `largestFreeBlock()` lets the core fail early with a message that says
which knob to turn, instead of half-allocating and crashing later.

## Capture strategies

| | `FullBuffer` | `Streaming` |
|---|---|---|
| RAM | whole clip | one DMA block |
| Storage mounted during capture | no | yes |
| Pins | may overlap audio | must be disjoint |
| Short capture | discarded by default, or saved with a corrected header | always discarded |
| Power loss | loses the clip | file on the card is already written |

The ESP32-CAM needs `FullBuffer` because GPIO14/15 are shared between the I2S
microphone and SD_MMC. The controller therefore releases the audio peripheral
*before* mounting the card — that ordering is the whole reason the ESP32-CAM
profile works at all, and it is why `record()` and `savePending()` are separate
calls.

`Streaming` writes the header before the payload because a streaming writer
cannot seek backwards. A short capture would therefore leave a header that
overstates the file length, so the partial file is deleted instead. That is a
deliberate trade, documented in the header.

## Conversion pipeline

```text
IAudioSource.readWords
        |
        v
StreamLayout { output, wordsPerFrame, samplesPerWord }
        |
        v
SampleConverter  -- RawI2sWords: slot selection -> i2sWordToPcm16 -> attenuation
                 \- Pcm16:       unpack LE halves -> attenuation   (never shifted)
        |
        v
   PCM16 frames  ->  AudioStats  ->  WAV header + payload
```

Slot selection for raw words:

| `wordsPerFrame` | `Left` | `Right` | `Both` |
|---|---|---|---|
| 1 (driver already selected) | word 0 | word 0 | word 0 |
| 2 (interleaved) | word 0 | word 1 | mean of both |

Gain is **scaling, not denoising**. `rightShift` aligns a raw word to PCM16 and
`attenuationShift` reduces amplitude; nothing filters, and a quieter recording is
fixed with better wiring and placement rather than more software gain.

## Error handling

Every fallible call returns a `Status`, never a bare `bool` and never a driver
error code. Adapters map `esp_err_t` and filesystem failures onto the list, so
the core never depends on a chip SDK's error space.

A few invariants the tests enforce:

- The audio peripheral is released on **every** exit path from `record()`.
- `allOff()` is called on every exit path, including a failed `begin()`.
- A failed save deletes the partial file, so a truncated WAV is never left
  looking complete.
- A failed save keeps the clip in RAM so `SAVE` can be retried.
- A captured frame count that falls short is reported and, by default,
  discarded rather than written as if it were complete.

## What this design deliberately does not do

- **No automatic gain control and no denoising.** Both destroy the information a
  classifier needs and both hide a misconfigured input.
- **No dynamic allocation for small control objects.** The label is a fixed
  array; the only heap use is the clip buffer, which is sized deliberately.
- **No brownout suppression.** A brownout on SD mount means the supply cannot
  supply the card. Disabling the detector converts a diagnosable fault into an
  undefined one.