/*
 * AI-Thinker ESP32-CAM + INMP441 + on-board microSD -- the reference profile.
 *
 * Target core: Arduino-ESP32 2.x or 3.x. The I2S adapter is selected at compile
 * time from the core version (see adapters/CoreVersion.h), so this one sketch
 * builds against both APIs. Core 2.x with the legacy driver is the combination
 * this project has actually observed producing a successful WAV save; on 3.x
 * re-run docs/hardware-checklist.md before trusting the output.
 *
 * Pin constraints that are not obvious from the wiring table:
 *   - GPIO14 and GPIO15 are shared with the SD_MMC bus. The card is therefore
 *     NOT mounted while I2S is running. CaptureStrategy::FullBuffer exists for
 *     exactly this: capture to PSRAM, release I2S, then mount and write.
 *   - GPIO12 is a boot-strapping pin on the original ESP32 and is also SD DAT2.
 *     It was moved here from GPIO13 because a save then succeeded. Treat that
 *     as an observed workaround on one board revision, not a recommendation.
 *   - GPIO4 is the flash LED and is part of the SD bus, so the indicator is
 *     only driven while the card is unmounted.
 *   - GPIO16 is typically occupied by PSRAM. Do not use it.
 *
 * A reset or power loss between RECORD and SAVE loses the clip. RAM does not
 * survive it; the alternative is a board whose audio and storage pins do not
 * collide, which is what examples/ESP32_S3_I2S_SPI_SD does.
 */

#include <Arduino.h>

#include "AudioDatasetRecorder.h"
#include "CommandConsole.h"
#include "adapters/ArduinoEnvironment.h"
#include "adapters/CoreVersion.h"
#include "adapters/GpioIndicator.h"
#include "adapters/SdMmcStorage.h"
#include "profiles/BoardProfiles.h"

#if ADR_HAS_ESP_I2S
#include "adapters/EspI2sAudioSource.h"
using AudioSource = AudioDatasetRecorder::adapters::EspI2sAudioSource;
#else
#include "adapters/LegacyI2sAudioSource.h"
using AudioSource = AudioDatasetRecorder::adapters::LegacyI2sAudioSource;
#endif

using namespace AudioDatasetRecorder;
using namespace AudioDatasetRecorder::adapters;

/* ---------------------------------------------------------------- config */

#ifndef ADR_SERIAL_BAUD
#define ADR_SERIAL_BAUD 115200UL
#endif

#ifndef ADR_SAMPLE_RATE_HZ
#define ADR_SAMPLE_RATE_HZ 16000UL
#endif

#ifndef ADR_RECORD_SECONDS
#define ADR_RECORD_SECONDS 60UL
#endif

/*
 * Alignment between the INMP441's raw 24-bit sample and PCM16, for a 32-bit
 * I2S slot. 14 matches the working setup. It is a tuning parameter for this
 * microphone and this peripheral configuration, not an INMP441 constant: check
 * the recorded levels with tools/analyze_wav.py before changing it.
 */
#ifndef ADR_I2S_PCM_RIGHT_SHIFT
#define ADR_I2S_PCM_RIGHT_SHIFT 14
#endif

#ifndef ADR_RECORDING_LED_PIN
#define ADR_RECORDING_LED_PIN 4
#endif

#ifndef ADR_RECORDING_LED_ON_LEVEL
#define ADR_RECORDING_LED_ON_LEVEL HIGH
#endif

#ifndef ADR_SD_MOUNT_POINT
#define ADR_SD_MOUNT_POINT "/sdcard"
#endif

static_assert(ADR_RECORD_SECONDS > 0, "ADR_RECORD_SECONDS must be positive");
static_assert(ADR_SAMPLE_RATE_HZ > 0, "ADR_SAMPLE_RATE_HZ must be positive");

/* -------------------------------------------------------------- objects */

static AudioSource g_audio;
static SdMmcStorage g_storage;
static GpioIndicator g_indicator;
static ArduinoMillisClock g_clock;
static PsramFirstAllocator g_memory;
static ArduinoLogger g_log(Serial);
static StreamTransport g_transport(Serial);
static SerialProgressSink g_progress(Serial);

static const AudioPinMap kPins = profiles::kAiThinkerEsp32CamInmp441;

static RecorderController g_recorder(g_audio, g_storage, g_indicator, g_clock, g_memory, g_log);
static CommandConsole g_console(g_recorder, g_transport, g_log);

void setup() {
    Serial.begin(ADR_SERIAL_BAUD);
    const uint32_t serialTimeoutMs = 2000UL;
    Serial.setTimeout(serialTimeoutMs);

    g_audio.configurePins(kPins.bclk, kPins.ws, kPins.data);
#if !ADR_HAS_ESP_I2S
    g_audio.configurePort(I2S_NUM_0);
    g_audio.configureDma(8, 256);
#endif
    g_storage.configure(ADR_SD_MOUNT_POINT, /* oneBit */ true);

    const Status indicatorStatus =
        g_indicator.begin(ADR_RECORDING_LED_PIN, ADR_RECORDING_LED_ON_LEVEL == HIGH, false);
    if (indicatorStatus != Status::Ok) {
        g_log.log("[setup] WARNING: no indicator on pin %d", ADR_RECORDING_LED_PIN);
    }

    RecorderConfig config;
    config.sampleRateHz = ADR_SAMPLE_RATE_HZ;
    config.channels = 1;
    config.durationSeconds = ADR_RECORD_SECONDS;
    config.slot = SampleSlot::Left;  /* L/R tied to GND on the INMP441 */
    config.rightShift = ADR_I2S_PCM_RIGHT_SHIFT;
    config.strategy = CaptureStrategy::FullBuffer;
    config.minFreeBytesMargin = 64u * 1024u;
    const Status configStatus = g_recorder.configure(config);
    if (configStatus != Status::Ok) {
        g_log.log("[setup] refusing to start: invalid configuration (%s)",
                  toString(configStatus));
        return;
    }

    g_recorder.setProgressSink(&g_progress);

    g_log.log("");
    g_log.log("AudioDatasetRecorder -- AI-Thinker ESP32-CAM + INMP441 + SD_MMC");
    g_log.log("  audio      %s, BCLK %d, WS %d, DOUT %d, left slot", g_audio.name(), kPins.bclk,
              kPins.ws, kPins.data);
    g_log.log("  storage    %s (one-bit)", g_storage.name());
    g_log.log("  format     %u Hz mono PCM16, %u s per clip", ADR_SAMPLE_RATE_HZ,
              ADR_RECORD_SECONDS);
    g_log.log("  right shift %u (smaller = more gain)", ADR_I2S_PCM_RIGHT_SHIFT);
    g_log.log("  strategy   full-buffer: capture to PSRAM, then mount and write");
    g_log.log("");

    if (!psramFound()) {
        g_log.log("[setup] WARNING: no PSRAM detected. %u s needs %llu bytes; reduce",
                  ADR_RECORD_SECONDS,
                  static_cast<unsigned long long>(g_recorder.clipFrameBytes(
                      static_cast<uint64_t>(ADR_SAMPLE_RATE_HZ) * ADR_RECORD_SECONDS)));
        g_log.log("[setup] ADR_RECORD_SECONDS, or the first RECORD will fail with OUT_OF_MEMORY.");
    }

    /* Let the USB serial bridge settle before the first prompt is missed. */
    delay(700);
    g_console.printBanner();
}

void loop() { g_console.poll(); }