/*
 * Generic ESP32 with an I2S microphone and a microSD card on SPI.
 *
 * Target core: Arduino-ESP32 2.x or 3.x. The audio adapter is selected at
 * compile time by the core version (see adapters/CoreVersion.h), so this same
 * sketch builds against both I2S APIs.
 *
 * Why this profile exists: SPI pins can be routed away from the microphone's
 * pins, so the audio peripheral and the card never contend for the same GPIO.
 * That makes CaptureStrategy::Streaming usable, which keeps only one DMA block
 * in RAM instead of a whole clip, and therefore works on boards with no PSRAM.
 *
 * Nothing about this profile has been run on hardware by this project. The pin
 * numbers below are ordinary defaults for a DevKit-class board; check them
 * against your board's schematic. Pins that touch SPI flash (GPIO6-11 on the
 * classic ESP32) are not usable at all.
 */

#include <Arduino.h>

#include "AudioDatasetRecorder.h"
#include "CommandConsole.h"
#include "adapters/ArduinoEnvironment.h"
#include "adapters/CoreVersion.h"
#include "adapters/GpioIndicator.h"
#include "adapters/SpiSdStorage.h"
#include "profiles/BoardProfiles.h"

#if ADR_HAS_ESP_I2S
#include "adapters/EspI2sAudioSource.h"
using AudioSource = AudioDatasetRecorder::adapters::EspI2sAudioSource;
#define ADR_AUDIO_NAME "esp32-esp-i2s"
#else
#include "adapters/LegacyI2sAudioSource.h"
using AudioSource = AudioDatasetRecorder::adapters::LegacyI2sAudioSource;
#define ADR_AUDIO_NAME "esp32-legacy-i2s"
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
#define ADR_RECORD_SECONDS 30UL
#endif

#ifndef ADR_I2S_PCM_RIGHT_SHIFT
#define ADR_I2S_PCM_RIGHT_SHIFT 14
#endif

#ifndef ADR_RECORDING_LED_PIN
#define ADR_RECORDING_LED_PIN 2
#endif

#ifndef ADR_USE_STREAMING
#define ADR_USE_STREAMING 1
#endif

/* MicroSD on SPI. Override for your wiring. */
#ifndef ADR_SD_CS_PIN
#define ADR_SD_CS_PIN 5
#endif
#ifndef ADR_SD_SCK_PIN
#define ADR_SD_SCK_PIN 18
#endif
#ifndef ADR_SD_MISO_PIN
#define ADR_SD_MISO_PIN 19
#endif
#ifndef ADR_SD_MOSI_PIN
#define ADR_SD_MOSI_PIN 23
#endif

static_assert(ADR_RECORD_SECONDS > 0, "ADR_RECORD_SECONDS must be positive");
static_assert(ADR_SAMPLE_RATE_HZ > 0, "ADR_SAMPLE_RATE_HZ must be positive");

/* -------------------------------------------------------------- objects */

static AudioSource g_audio;
static SpiSdStorage g_storage;
static GpioIndicator g_indicator;
static ArduinoMillisClock g_clock;
static PsramFirstAllocator g_memory;
static ArduinoLogger g_log(Serial);
static StreamTransport g_transport(Serial);
static SerialProgressSink g_progress(Serial);

static const profiles::AudioPinMap kPins = profiles::kGenericEsp32DevKit;

static RecorderController g_recorder(g_audio, g_storage, g_indicator, g_clock, g_memory, g_log);
static CommandConsole g_console(g_recorder, g_transport, g_log);

void setup() {
    Serial.begin(ADR_SERIAL_BAUD);
    Serial.setTimeout(2000UL);

    g_audio.configurePins(kPins.bclk, kPins.ws, kPins.data);
    g_storage.configure(ADR_SD_CS_PIN, ADR_SD_SCK_PIN, ADR_SD_MISO_PIN, ADR_SD_MOSI_PIN);

    const Status indicatorStatus =
        g_indicator.begin(ADR_RECORDING_LED_PIN, /* activeHigh */ true, false);
    if (indicatorStatus != Status::Ok) {
        g_log.log("[setup] WARNING: no indicator on pin %d", ADR_RECORDING_LED_PIN);
    }

    RecorderConfig config;
    config.sampleRateHz = ADR_SAMPLE_RATE_HZ;
    config.channels = 1;
    config.durationSeconds = ADR_RECORD_SECONDS;
    config.slot = SampleSlot::Left;
    config.rightShift = ADR_I2S_PCM_RIGHT_SHIFT;
    config.strategy = ADR_USE_STREAMING ? CaptureStrategy::Streaming : CaptureStrategy::FullBuffer;
    config.minFreeBytesMargin = 32u * 1024u;

    const Status configStatus = g_recorder.configure(config);
    if (configStatus != Status::Ok) {
        g_log.log("[setup] refusing to start: invalid configuration (%s)", toString(configStatus));
        return;
    }

    g_recorder.setProgressSink(&g_progress);

    g_log.log("");
    g_log.log("AudioDatasetRecorder -- generic ESP32 + I2S mic + SPI microSD");
    g_log.log("  audio      %s, BCLK %d, WS %d, DOUT %d", ADR_AUDIO_NAME, kPins.bclk, kPins.ws,
              kPins.data);
    g_log.log("  storage    %s, CS %d, SCK %d, MISO %d, MOSI %d", g_storage.name(), ADR_SD_CS_PIN,
              ADR_SD_SCK_PIN, ADR_SD_MISO_PIN, ADR_SD_MOSI_PIN);
    g_log.log("  format     %u Hz mono PCM16, %u s per clip", ADR_SAMPLE_RATE_HZ,
              ADR_RECORD_SECONDS);
    g_log.log("  strategy   %s",
              ADR_USE_STREAMING ? "streaming (card must be mounted during capture)"
                                : "full-buffer (clip held in RAM until SAVE)");
    g_log.log("");
    g_log.log("[setup] Pin defaults only. Check them against your board schematic, and");
    g_log.log("[setup] verify with docs/hardware-checklist.md before collecting a dataset.");

    delay(700);
    g_console.printBanner();
}

void loop() { g_console.poll(); }