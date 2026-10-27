/*
 * HOTPIN dataset recorder -- reference board, ready to flash.
 *
 * This is the replacement for the standalone 20-second sketch: same hardware,
 * same commands, but it is a thin wiring layer over this library and it carries
 * the three corrections that sketch needed. All three were measured on the
 * AI-Thinker ESP32-CAM reference, not guessed.
 *
 *   1. SDMMC runs at 4 MHz (ADR_SD_FREQUENCY_KHZ). At the 20 MHz default the
 *      board browned out the moment the first write started. At 4 MHz two
 *      consecutive 20 s clips (640044 bytes each) were written without a
 *      reset. Lowering the clock is a stopgap for a rail with too little bulk
 *      capacitance, not a cure.
 *   2. The microphone is read from the RIGHT I2S slot. With L/R tied to GND the
 *      datasheet says left, but this board delivered signal in the right slot
 *      and exact zeros in the left one.
 *   3. The PCM right shift ships at 15, +6 dB over the unity 24-to-16
 *      reduction (16). At 16 the measured peaks were 2001-8777; at 14
 *      a loud take reached 31863/32768 and an earlier run clipped 571 of
 *      80000 frames. 15 measured 14889 peak with zero clipped samples.
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
 * Configure (optional): copy .env.example to .env, edit it, run
 *   python tools/gen_config.py
 * then build. The generated config.h is gitignored; without it the defaults
 * below apply.
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

/* Build-time settings generated from .env (see tools/gen_config.py). */
#if __has_include("config.h")
#include "config.h"
#endif

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
#define ADR_I2S_PCM_RIGHT_SHIFT 15
#endif

#ifndef ADR_I2S_SLOT_RIGHT
#define ADR_I2S_SLOT_RIGHT 1
#endif

#ifndef ADR_PIN_BCLK
#define ADR_PIN_BCLK 14
#endif

#ifndef ADR_PIN_WS
#define ADR_PIN_WS 15
#endif

#ifndef ADR_PIN_DOUT
#define ADR_PIN_DOUT 12
#endif

#ifndef ADR_PIN_LED
#define ADR_PIN_LED 4
#endif

#ifndef ADR_LED_ACTIVE_HIGH
#define ADR_LED_ACTIVE_HIGH 1
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

/* BCLK and WS are outputs on the original ESP32; GPIO34-39 cannot drive them. */
#define ADR_DATA_PINS_VALID                                                   \
    (ADR_PIN_BCLK >= 0 && ADR_PIN_BCLK < 34 && ADR_PIN_WS >= 0 &&               \
     ADR_PIN_WS < 34 && ADR_PIN_DOUT >= 0 && ADR_PIN_DOUT <= 39 &&              \
     ADR_PIN_LED >= 0 && ADR_PIN_LED <= 39 && ADR_PIN_BCLK != ADR_PIN_WS &&     \
     ADR_PIN_BCLK != ADR_PIN_DOUT && ADR_PIN_BCLK != ADR_PIN_LED &&             \
     ADR_PIN_WS != ADR_PIN_DOUT && ADR_PIN_WS != ADR_PIN_LED &&                 \
     ADR_PIN_DOUT != ADR_PIN_LED)

static_assert(ADR_RECORD_SECONDS > 0, "ADR_RECORD_SECONDS must be positive");
static_assert(ADR_SAMPLE_RATE_HZ > 0, "ADR_SAMPLE_RATE_HZ must be positive");
static_assert(ADR_DATA_PINS_VALID,
              "pins must be distinct GPIOs 0-39; BCLK and WS must be <= 33");

/* -------------------------------------------------------------- objects */

static AudioSource g_audio;
static SdMmcStorage g_storage;
static GpioIndicator g_indicator;
static ArduinoMillisClock g_clock;
static PsramFirstAllocator g_memory;
static ArduinoLogger g_log(Serial);
static StreamTransport g_transport(Serial);
static SerialProgressSink g_progress(Serial);

static const profiles::AudioPinMap kPins(static_cast<int8_t>(ADR_PIN_BCLK),
                                         static_cast<int8_t>(ADR_PIN_WS),
                                         static_cast<int8_t>(ADR_PIN_DOUT));

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
    g_log.log(" Reset    %s", resetReasonName(reason));
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
        g_indicator.begin(static_cast<int8_t>(ADR_PIN_LED), ADR_LED_ACTIVE_HIGH != 0, false);
    if (indicatorStatus != Status::Ok) {
        g_log.log("[setup] WARNING: no indicator on pin %d", ADR_PIN_LED);
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

    const uint64_t clipBytes = g_recorder.clipFileBytes(
        static_cast<uint64_t>(ADR_SAMPLE_RATE_HZ) * ADR_RECORD_SECONDS);

    g_log.log("");
    g_log.log("============================================================");
    g_log.log(" HOTPIN dataset recorder");
    g_log.log(" ESP32-CAM + INMP441 + SD_MMC");
    g_log.log("============================================================");
    reportResetReason();
    g_log.log(" Audio    %s on BCLK %d, WS %d, DOUT %d", g_audio.name(), kPins.bclk, kPins.ws,
              kPins.data);
    g_log.log(" Slot     %s, PCM right shift %d", ADR_I2S_SLOT_RIGHT ? "right" : "left",
              ADR_I2S_PCM_RIGHT_SHIFT);
    g_log.log(" Format   %u Hz mono PCM16, %u s per clip (%llu bytes)", ADR_SAMPLE_RATE_HZ,
              ADR_RECORD_SECONDS, static_cast<unsigned long long>(clipBytes));
    if (ADR_SD_FREQUENCY_KHZ > 0) {
        g_log.log(" Storage  %s, 1-bit, %d kHz, mounted only for SAVE/LIST", g_storage.name(),
                  ADR_SD_FREQUENCY_KHZ);
    } else {
        g_log.log(" Storage  %s, 1-bit, board default clock, mounted only for SAVE/LIST",
                  g_storage.name());
    }
    g_log.log(" LED      GPIO %d active-%s, on while recording, 3 blinks after SAVE",
              ADR_PIN_LED, ADR_LED_ACTIVE_HIGH ? "high" : "low");
    g_log.log("============================================================");
    g_log.log("");

    if (!psramFound()) {
        g_log.log("[setup] WARNING: no PSRAM detected. %u s needs %llu bytes; reduce",
                  ADR_RECORD_SECONDS, static_cast<unsigned long long>(clipBytes));
        g_log.log("[setup] ADR_RECORD_SECONDS or the first RECORD will fail with OUT_OF_MEMORY.");
    }

    /* Let the USB serial bridge settle before the menu is printed. */
    delay(700);
    g_console.showHelp();
}

void loop() { g_console.poll(); }
