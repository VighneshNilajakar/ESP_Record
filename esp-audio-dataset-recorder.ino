/*
 * HOTPIN dataset recorder -- reference board, ready to flash.
 *
 * This is the replacement for the standalone 20-second sketch: same hardware,
 * same commands, but it is a thin wiring layer over this library and it carries
 * the three corrections that sketch needed. All three were measured on the
 * AI-Thinker ESP32-CAM reference, not guessed.
 *
 *   1. SDMMC runs at 4 MHz (ADR_SD_FREQUENCY_KHZ). At the 20 MHz default the
 *      board browned out the moment the first write started. 4 MHz completed
 *      the same 160 KB write on the same supply. Lowering the clock is a
 *      stopgap for a rail with too little bulk capacitance, not a cure.
 *   2. The microphone is read from the RIGHT I2S slot. With L/R tied to GND the
 *      datasheet says left, but this board delivered signal in the right slot
 *      and exact zeros in the left one.
 *   3. The PCM right shift is 16. A shift of 13 (the old sketch's 16x gain)
 *      clipped 571 of 80000 frames; 16 is the correct 24-to-16 bit reduction
 *      for a sample that is MSB-aligned in a 32-bit slot.
 *
 * The library also fixes the sequencing that matters on this board: capture
 * into PSRAM with CaptureStrategy::FullBuffer, release I2S, then mount SD_MMC
 * in 1-bit mode and save. GPIO14/15 are shared between the microphone and the
 * card, so the card must never be mounted while audio is running.
 *
 * Wiring (INMP441 -> ESP32-CAM):
 *   VDD 3V3, GND GND, SCK GPIO14, WS GPIO15, SD GPIO12, L/R GND
 *   GPIO4 is the flash LED; it is part of the SD bus and is off during saves.
 *
 * Build (from this directory):
 *   arduino-cli compile --fqbn esp32:esp32:esp32cam .
 * The includes are prefixed with src/ on purpose: Arduino compiles a sketch's
 * src/ folder as part of the sketch itself, so this file builds without the
 * --library flag. Passing --library . here would compile every source twice.
 * Core 2.x is the combination observed producing a successful save; 3.x builds
 * but has not been run on this board by this project.
 *
 * Serial: 115200, Newline. Commands: RECORD <label>, SAVE, DISCARD, LIST,
 * STATUS, HELP. Labels are 1-24 characters of [A-Za-z0-9_-].
 */

#include <Arduino.h>
#include <esp_system.h>

#include "src/AudioDatasetRecorder.h"
#include "src/CommandConsole.h"
#include "src/adapters/ArduinoEnvironment.h"
#include "src/adapters/CoreVersion.h"
#include "src/adapters/GpioIndicator.h"
#include "src/adapters/SdMmcStorage.h"
#include "src/profiles/BoardProfiles.h"

#if ADR_HAS_ESP_I2S
#include "src/adapters/EspI2sAudioSource.h"
using AudioSource = AudioDatasetRecorder::adapters::EspI2sAudioSource;
#else
#include "src/adapters/LegacyI2sAudioSource.h"
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
#define ADR_RECORD_SECONDS 20UL
#endif

#ifndef ADR_I2S_PCM_RIGHT_SHIFT
#define ADR_I2S_PCM_RIGHT_SHIFT 16
#endif

#ifndef ADR_I2S_SLOT_RIGHT
#define ADR_I2S_SLOT_RIGHT 1
#endif

#ifndef ADR_RECORDING_LED_PIN
#define ADR_RECORDING_LED_PIN 4
#endif

#ifndef ADR_SD_MOUNT_POINT
#define ADR_SD_MOUNT_POINT "/sdcard"
#endif

/*
 * 4 MHz instead of the 20 MHz board default. This is the single setting that
 * stops the reference board from resetting mid-save. If you have confirmed a
 * stiff 5 V supply and added bulk capacitance, 0 restores the default and
 * saves in about a fifth of the time.
 */
#ifndef ADR_SD_FREQUENCY_KHZ
#define ADR_SD_FREQUENCY_KHZ 4000
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

static const profiles::AudioPinMap kPins = profiles::kAiThinkerEsp32CamInmp441;

static RecorderController g_recorder(g_audio, g_storage, g_indicator, g_clock, g_memory, g_log);
static CommandConsole g_console(g_recorder, g_transport, g_log);

/* --------------------------------------------------------- diagnostics */

static const char *resetReasonName(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return "power-on";
        case ESP_RST_EXT:
            return "external reset";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic/exception";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deep sleep";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT (supply dipped)";
        default:
            return "unknown";
    }
}

static void reportResetReason() {
    const esp_reset_reason_t reason = esp_reset_reason();
    g_log.log("  reset      %s", resetReasonName(reason));
    if (reason != ESP_RST_BROWNOUT) return;

    /*
     * A brownout reset is the one failure this sketch is arranged to avoid, so
     * say plainly what it means instead of letting the next save fail the same
     * way silently. Do not "fix" this by disabling the detector.
     */
    g_log.log("");
    g_log.log("[brownout] the previous run lost power before it finished.");
    if (ADR_SD_FREQUENCY_KHZ > 0) {
        g_log.log("[brownout] at %d kHz the SD card is the largest current spike;",
                  ADR_SD_FREQUENCY_KHZ);
    } else {
        g_log.log("[brownout] at the default SD clock the card is the largest spike;");
    }
    g_log.log("[brownout] a stable 5 V supply, a short cable and 100-470 uF");
    g_log.log("[brownout] across 5 V/GND are the actual fix. Lowering the clock");
    g_log.log("[brownout] only narrows the margin.");
    g_log.log("");
}

/* --------------------------------------------------------------- setup */

void setup() {
    Serial.begin(ADR_SERIAL_BAUD);
    Serial.setTimeout(2000UL);

    g_audio.configurePins(kPins.bclk, kPins.ws, kPins.data);
#if !ADR_HAS_ESP_I2S
    g_audio.configurePort(I2S_NUM_0);
    g_audio.configureDma(8, 256);
#endif
    g_storage.configure(ADR_SD_MOUNT_POINT, /* oneBit */ true, ADR_SD_FREQUENCY_KHZ);

    const Status indicatorStatus =
        g_indicator.begin(ADR_RECORDING_LED_PIN, /* activeHigh */ true, false);
    if (indicatorStatus != Status::Ok) {
        g_log.log("[setup] WARNING: no indicator on pin %d", ADR_RECORDING_LED_PIN);
    }

    RecorderConfig config;
    config.sampleRateHz = ADR_SAMPLE_RATE_HZ;
    config.channels = 1;
    config.durationSeconds = ADR_RECORD_SECONDS;
    config.slot = ADR_I2S_SLOT_RIGHT ? SampleSlot::Right : SampleSlot::Left;
    config.rightShift = ADR_I2S_PCM_RIGHT_SHIFT;
    /* Audio and storage share GPIO14/15 on this board, so the clip is held in
     * PSRAM and the card is mounted only after I2S has been released. */
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
    g_log.log("HOTPIN dataset recorder -- ESP32-CAM + INMP441 + SD_MMC");
    reportResetReason();
    g_log.log("  audio      %s, BCLK %d, WS %d, DOUT %d, %s slot", g_audio.name(), kPins.bclk,
              kPins.ws, kPins.data, ADR_I2S_SLOT_RIGHT ? "right" : "left");
    if (ADR_SD_FREQUENCY_KHZ > 0) {
        g_log.log("  storage    %s (one-bit), %d kHz", g_storage.name(), ADR_SD_FREQUENCY_KHZ);
    } else {
        g_log.log("  storage    %s (one-bit), board default (~20 MHz)", g_storage.name());
    }
    g_log.log("  format     %u Hz mono PCM16, %u s per clip", ADR_SAMPLE_RATE_HZ,
              ADR_RECORD_SECONDS);
    g_log.log("  right shift %u (16 = no extra gain)", ADR_I2S_PCM_RIGHT_SHIFT);
    g_log.log("  strategy   full-buffer: capture to PSRAM, then mount and write");
    g_log.log("");

    if (!psramFound()) {
        g_log.log("[setup] WARNING: no PSRAM detected. %u s needs %llu bytes; reduce",
                  ADR_RECORD_SECONDS,
                  static_cast<unsigned long long>(g_recorder.clipFrameBytes(
                      static_cast<uint64_t>(ADR_SAMPLE_RATE_HZ) * ADR_RECORD_SECONDS)));
        g_log.log("[setup] ADR_RECORD_SECONDS or the first RECORD will fail with OUT_OF_MEMORY.");
    }

    /* Let the USB serial bridge settle before the first prompt is missed. */
    delay(700);
    g_console.printBanner();
}

void loop() { g_console.poll(); }
