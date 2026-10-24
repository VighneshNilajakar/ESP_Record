# ESP Audio Dataset Recorder

Record labelled microphone samples on ESP-class boards and save them as WAV files
for TinyML audio classification, such as Edge Impulse keyword spotting.

**Scope, stated honestly:** this project supports a range of ESP-family boards and
both major Arduino-ESP32 core lines through a set of named adapters, and it is
tested against them. It does **not** claim that one sketch works unchanged on
every ESP. Chips differ in I2S port count, PDM availability, pin routing and
storage buses, and a pin map that is correct on one board revision can brick the
boot on the next. See the [support matrix](docs/support-matrix.md) for exactly what
is evidenced and what is not.

## Features

- Serial-controlled capture with safe, validated labels: `RECORD hey_hotpin`,
  `RECORD noise`, `RECORD unknown`.
- Configurable sample rate, duration, channels, and digital gain.
- Two capture strategies: hold the whole clip in RAM, or stream it to the card.
- Correct RIFF/WAVE PCM16 output, byte-exact and size-verified after every save.
- Peak, RMS, DC-offset and clipped-sample diagnostics after each capture.
- Partial files are deleted rather than left looking complete; a failed save
  keeps the clip in RAM so `SAVE` can be retried.
- A standard-library WAV validator for the PC side.
- Host unit tests covering the portable core, with no hardware and no framework.

## Hardware profiles

| Example | Board | Microphone | Storage | Core |
|---|---|---|---|---|
| `AIThinker_ESP32_CAM_INMP441_SDMMC` | AI-Thinker ESP32-CAM | INMP441 (I2S) | on-board SD_MMC, 1-bit | 2.x or 3.x |
| `ESP32_I2S_SPI_SD` | Generic ESP32 | I2S or PDM | SPI microSD | 2.x or 3.x |
| `ESP32_S3_I2S_SPI_SD` | ESP32-S3 | I2S or PDM | SPI microSD | 3.x |

The C3, S2 and C6 have no runnable sketch here yet: their pin maps are not
confirmed on hardware, so the CI matrix compiles the adapter set for them
instead of guessing wiring. See [docs/support-matrix.md](docs/support-matrix.md).

The ESP32-CAM example is the reference profile: it is the one setup with an
observed successful save, and it carries the most pin constraints. Its wiring:

| INMP441 | ESP32-CAM | Note |
|---|---|---|
| VDD | 3V3 | |
| GND | GND | |
| SCK/BCLK | GPIO14 | shared with SD_MMC CLK |
| WS/LRCL | GPIO15 | shared with SD_MMC CMD |
| SD/DOUT | GPIO12 | boot-strapping pin, and SD DAT2 |
| L/R | GND | datasheet says left slot; measured right on the reference board |

Read [docs/hardware.md](docs/hardware.md) before changing pins. GPIO14/15 are
shared with SD_MMC, GPIO12 is a boot-strapping pin, and GPIO4 is both the flash
LED and part of the SD bus. That is why the reference captures to PSRAM, releases
I2S, and only then mounts the card.

The GPIO12 placement came from moving DOUT there from GPIO13 and observing a
successful save. Treat it as an observed workaround on one board revision,
**not** a recommendation.

The reference board was brought up on Arduino-ESP32 2.0.14: capture reported
80000/80000 frames in 5008 ms and a 160044-byte WAV was written and listed. Two
results from that run contradict the obvious reading of the datasheet, and both
are configurable at build time: the microphone delivered its data in the right
I2S slot even with L/R tied to GND (`ADR_I2S_SLOT_RIGHT`), and SD writes browned
out the board at the default 20 MHz clock but completed at 4 MHz
(`ADR_SD_FREQUENCY_KHZ`). The clock is a stopgap; the real fix is bulk
capacitance on the 5 V rail. See
[docs/support-matrix.md](docs/support-matrix.md) for the measurements.

## Ready-to-flash sketch

`esp-audio-dataset-recorder.ino` in the repository root is the reference board
wired to this library: 20 s clips, 16 kHz mono, the right I2S slot, a 16-bit
shift and SDMMC at 4 MHz -- the clock that stopped the reference board from
browning out mid-save. It also reports the reset reason at boot, so a repeat
brownout is visible instead of silent.

It includes `src/` directly and is compiled from the repository root, so do not
pass `--library` (that would compile every source twice):

```
arduino-cli compile --fqbn esp32:esp32:esp32cam .
```

## Install

Copy this repository into your Arduino libraries directory, or install the ZIP via
**Sketch → Include Library → Add .ZIP Library**, then restart the IDE.

Install an Arduino-ESP32 board package:

- **2.x** gives the legacy `driver/i2s.h` API, which is what the reference profile
  was observed on (originally 2.0.16).
- **3.x** replaces it with `ESP_I2S.h`. The examples select the adapter at compile
  time, so the same sketch builds on both; only the ESP32-S3 example, which uses
  PDM receive, requires 3.x.

## Use

1. Open `examples/<profile>/<same-name>.ino`.
2. Select your board. Enable PSRAM if the board menu offers it; use a FAT32 card.
3. Upload, then open the Serial Monitor at **115200 baud**, line ending **Newline**.
4. Type commands:

   ```text
   RECORD hey_hotpin
   RECORD noise
   RECORD unknown
   LIST
   STATUS
   HELP
   ```

   With `CaptureStrategy::FullBuffer`, `RECORD` captures into RAM and `SAVE`
   writes it. With `Streaming`, `RECORD` captures and writes in one step. Run
   `STATUS` to see which is active.

5. `DISCARD` drops a pending clip. `SAVE` retries a failed save after you fix
   the card.

Labels are 1–24 characters of `a-z A-Z 0-9 _ -`. Everything else is rejected, and
because `.` cannot appear in a label, path traversal is impossible by
construction rather than by filtering. A malformed label is refused with a
sanitized suggestion instead of being silently rewritten.

### Changing duration, gain and pins

Edit the `#define`s at the top of the example, or pass build flags:

```cpp
#define ADR_RECORD_SECONDS 20        // 60 s needs ~1.9 MB of PSRAM at 16 kHz
#define ADR_I2S_PCM_RIGHT_SHIFT 16   // 24-bit data MSB-aligned in a 32-bit slot
#define ADR_MIC_BCLK_PIN 14
#define ADR_MIC_WS_PIN 15
#define ADR_MIC_DATA_PIN 12
#define ADR_USE_PDM 1                // S3 example: PDM microphone
```

The right shift describes the alignment between the microphone's raw word and
PCM16 for the configured peripheral. It is **not** an INMP441 constant. Confirm
it by measuring: record speech, then check the clip.

## Inspect audio before training

```bash
python tools/analyze_wav.py path/to/hey_hotpin.001.wav
```

It reports format, peak, RMS, DC offset, clipping fraction and short-window
levels, cross-checks the RIFF header against the payload on disk, and warns when
data is silent, heavily clipped, or badly offset. It is standard library only: no
packages, no network, and it never modifies a recording.

## Collecting a dataset that is worth training on

1. Record several **sessions** per label with the same microphone placement and
   the same gain settings.
2. For the wake phrase, speak it repeatedly with clear gaps. Do not pad every clip
   with long silences.
3. For negatives, record realistic ambient sound and ordinary speech. The hardest
   negatives are speech that is not the wake phrase.
4. Inspect every clip with `analyze_wav.py` before uploading.
5. Split into the window your model expects (1,000 ms segments is common) and
   check the splits. Automated segmentation is imperfect.
6. **Avoid train/test leakage.** If one recording is chopped into many segments
   and then split across both sets, the model can score well by memorising the
   recording rather than learning the keyword. Split by session, not by segment.

Keep recording gain and format identical between training and inference, or
normalise both through the same documented path.

See [docs/edge-impulse.md](docs/edge-impulse.md) and
[docs/audio-quality.md](docs/audio-quality.md).

## Troubleshooting

- **Quiet audio.** Confirm the peak and RMS before touching gain. Check the DOUT
  pin, the microphone's L/R selection, and the I2S slot configuration.
- **Noise that rises with gain.** Gain amplifies sensor and electrical noise
  along with signal; it cannot improve physical SNR. Improve wiring, grounding
  and placement first.
- **Repeating clicks.** That is an I2S alignment or DMA block problem. Do not
  filter it away; check the slot configuration and the DMA buffer size.
- **Brownout when mounting the card.** Use a stable 5 V supply with real
  current, a short cable, and a common ground. Keep brownout detection enabled.
- **Card fails while I2S is active.** The reference deliberately releases I2S
  before mounting SD_MMC, because GPIO14/15 are shared. Use SPI SD if you want the
  two buses live at the same time.
- **Will not boot after moving DOUT to GPIO12.** GPIO12 is a strapping pin on the
  original ESP32. Disconnect the data line to recover, then re-evaluate.
- **No PSRAM.** The full-buffer strategy needs the whole clip in RAM. Shorten the
  duration, or use a profile whose pins do not collide so streaming works.
- **`OUT_OF_MEMORY` at RECORD.** The message names the exact number of bytes
  needed. Shorten `ADR_RECORD_SECONDS` or switch to streaming.

More in [docs/troubleshooting.md](docs/troubleshooting.md).

## Development

Host tests need only a C++17 compiler:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I tests/native_stubs -I tests -I src \
    $(find src -name '*.cpp') tests/test_*.cpp -o build/test_adr && ./build/test_adr
python -m unittest discover -s tests -p "test_*.py" -v
```

Board profiles are compile-checked by CI using `arduino-cli`:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32cam --library . examples/ESP32_I2S_SPI_SD
```

or with PlatformIO via `platformio.ini`.

Hardware correctness is **not** automated. Before calling any profile supported,
work through [docs/hardware-checklist.md](docs/hardware-checklist.md) and record
what you observed. A build that passes CI and a board that records audio are
different claims.

## Repository layout

```text
src/                    Portable core plus ESP adapters and board profiles
  adapters/             Chip and core-version specific code, all #if ESP32-guarded
  profiles/             Named pin maps
examples/               One sketch folder per hardware profile
tests/                  Host C++ tests and Python tests for the WAV tool
tools/analyze_wav.py    Standard-library WAV diagnostics
docs/                   Architecture, support matrix, hardware, audio, Edge Impulse
.github/workflows/      Host tests and the board compile matrix
```

## Licence

MIT. See [LICENSE](LICENSE).