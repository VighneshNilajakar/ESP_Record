/*
 * Compile-only check for the 3.x adapter set.
 *
 * This is deliberately not an example and has no pins: its job is to prove that
 * the ESP_I2S, PDM and SPI-SD adapters compile for chips that have no runnable
 * sketch in this repository yet. A sketch that guessed pin numbers for those
 * boards would compile just as happily and then fail on hardware, which is the
 * failure mode this file avoids.
 *
 * It is compiled by the CI board matrix for every target listed there. No
 * behaviour is claimed: nothing here has been run.
 */

#include <Arduino.h>

#include "AudioDatasetRecorder.h"
#include "adapters/ArduinoEnvironment.h"
#include "adapters/CoreVersion.h"
#include "adapters/GpioIndicator.h"
#include "adapters/SpiSdStorage.h"

#if ADR_HAS_ESP_I2S
#include "adapters/EspI2sAudioSource.h"
#include "adapters/PdmI2sAudioSource.h"
#else
#include "adapters/LegacyI2sAudioSource.h"
#endif

using namespace AudioDatasetRecorder;
using namespace AudioDatasetRecorder::adapters;

#if ADR_HAS_ESP_I2S
static EspI2sAudioSource g_i2s;
#if defined(SOC_I2S_SUPPORTS_PDM_RX) && SOC_I2S_SUPPORTS_PDM_RX
static PdmI2sAudioSource g_pdm;
#endif
#else
static LegacyI2sAudioSource g_i2s;
#endif

static SpiSdStorage g_storage;
static GpioIndicator g_indicator;
static ArduinoMillisClock g_clock;
static PsramFirstAllocator g_memory;
static ArduinoLogger g_log(Serial);

static RecorderController g_recorder(g_i2s, g_storage, g_indicator, g_clock,
                                     g_memory, g_log);

void setup() {
    Serial.begin(115200);
    g_log.log("compile check: %s on core %d.x", g_i2s.name(),
              ADR_CORE_MAJOR_VERSION);
#if ADR_HAS_ESP_I2S
    g_log.log("compile check: esp_i2s adapter present");
#if defined(SOC_I2S_SUPPORTS_PDM_RX) && SOC_I2S_SUPPORTS_PDM_RX
    g_log.log("compile check: pdm adapter present");
    (void)g_pdm;
#endif
#endif
    (void)g_recorder;
    (void)g_indicator;
    (void)g_clock;
}

void loop() {}
