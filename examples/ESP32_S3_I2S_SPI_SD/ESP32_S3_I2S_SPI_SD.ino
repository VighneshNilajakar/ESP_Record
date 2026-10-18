/*
 * ESP32-S3 with an I2S or PDM microphone and a microSD card on SPI.
 *
 * Target core: Arduino-ESP32 3.x, because the PDM receive path exists only in
 * the ESP_I2S API introduced there. On core 2.x this sketch will not build.
 *
 * Build with -DADR_USE_PDM=1 for a digital PDM microphone (MP34GT01,
 * ICS-43434, INMP441D) on I2S port 0, or -DADR_USE_PDM=0 for an analogue I2S
 * microphone such as an INMP441.
 *
 * The PDM adapter reports SourceOutput::Pcm16 because the hardware converts
 * PDM to PCM before the data ever reaches the application. The recorder core
 * therefore does not apply the configured right shift on that path, which is
 * the whole reason SourceOutput exists: shifting an already-converted PCM16
 * stream a second time would halve it.
 *
 * Not yet run on hardware by this project. See docs/hardware-checklist.md.
 */

#include <Arduino.h>

#include "AudioDatasetRecorder.h"
#include "CommandConsole.h"
#include "adapters/ArduinoEnvironment.h"
#include "adapters/CoreVersion.h"
#include "adapters/EspI2sAudioSource.h"
#include "adapters/GpioIndicator.h"
#include "adapters/PdmI2sAudioSource.h"
#include "adapters/SpiSdStorage.h"
#include "profiles/BoardProfiles.h"

using namespace AudioDatasetRecorder;
using namespace AudioDatasetRecorder::adapters;

#if !ADR_HAS_ESP_I2S
#error "This sketch requires Arduino-ESP32 3.x: the PDM and ESP_I2S APIs do not exist in 2.x."
#endif

/* --------------------------------------------------------------- config */

#ifndef ADR_USE_PDM
#define ADR_USE_PDM 1
#endif

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
#define ADR_RECORDING_LED_PIN 48
#endif

#ifndef ADR_SD_CS_PIN
#define ADR_SD_CS_PIN 39
#endif
#ifndef ADR_SD_SCK_PIN
#define ADR_SD_SCK_PIN 40
#endif
#ifndef ADR_SD_MISO_PIN
#define ADR_SD_MISO_PIN 41
#endif
#ifndef ADR_SD_MOSI_PIN
#define ADR_SD_MOSI_PIN 42
#endif

static_assert(ADR_RECORD_SECONDS > 0, "ADR_RECORD_SECONDS must be positive");
static_assert(ADR_SAMPLE_RATE_HZ > 0, "ADR_SAMPLE_RATE_HZ must be positive");

/* -------------------------------------------------------------- objects */

#if ADR_USE_PDM
static PdmI2sAudioSource g_audio;
#else
static EspI2sAudioSource g_audio;
#endif

static SpiSdStorage g_storage;
static GpioIndicator g_indicator;
static ArduinoMillisClock g_clock;
static PsramFirstAllocator g_memory;
static ArduinoLogger g_log(Serial);
static StreamTransport g_transport(Serial);
static SerialProgressSink g_progress(Serial);

static const AudioPinMap kI2sPins = profiles::kGenericEsp32S3;
static const AudioPinMap kPdmPins = profiles::kGenericEsp32S3Pdm;

static RecorderController g_recorder(g_audio, g_storage, g_indicator, g_clock, g_memory, g_log);
static CommandConsole g_console(g_recorder, g_transport, g_log);

void setup() {
    Serial.begin(ADR_SERIAL_BAUD);
    Serial.setTimeout(2000UL);

#if ADR_USE_PDM
    g_audio.configurePins(kPdmPins.pdmClock, kPdmPins.data);
#else
    g_audio.configurePins(kI2sPins.bclk, kI2sPins.ws, kI2sPins.data);
#endif

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
    /* The S3 has no SD_MMC pin conflict with these pins, so streaming works and
     * a clip never has to be held whole in RAM. */
    config.strategy = CaptureStrategy::Streaming;
    config.minFreeBytesMargin = 32u * 1024u;

    const Status configStatus = g_recorder.configure(config);
    if (configStatus != Status::Ok) {
        g_log.log("[setup] refusing to start: invalid configuration (%s)", toString(configStatus));
        return;
    }

    g_recorder.setProgressSink(&g_progress);

    g_log.log("");
    g_log.log("AudioDatasetRecorder -- ESP32-S3 + %s + SPI microSD",
              ADR_USE_PDM ? "PDM microphone" : "I2S microphone");
    g_log.log("  audio      %s", g_audio.name());
#if ADR_USE_PDM
    g_log.log("  pins       PDM clock %d, data %d (I2S port 0 only)", kPdmPins.pdmClock,
              kPdmPins.data);
    g_log.log("  note       PDM RX converts to PCM in hardware, so no right shift is applied");
#else
    g_log.log("  pins       BCLK %d, WS %d, DOUT %d", kI2sPins.bclk, kI2sPins.ws, kI2sPins.data);
    g_log.log("  right shift %u (smaller = more gain)", ADR_I2S_PCM_RIGHT_SHIFT);
#endif
    g_log.log("  storage    %s, CS %d, SCK %d, MISO %d, MOSI %d", g_storage.name(), ADR_SD_CS_PIN,
              ADR_SD_SCK_PIN, ADR_SD_MISO_PIN, ADR_SD_MOSI_PIN);
    g_log.log("  format     %u Hz mono PCM16, %u s per clip", ADR_SAMPLE_RATE_HZ,
              ADR_RECORD_SECONDS);
    g_log.log("");
    g_log.log("[setup] Pin defaults only. Check them against your board schematic, and");
    g_log.log("[setup] verify with docs/hardware-checklist.md before collecting a dataset.");

    delay(700);
    g_console.printBanner();
}

void loop() { g_console.poll(); }