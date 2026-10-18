#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_CORE_VERSION_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_CORE_VERSION_H

/*
 * Arduino-ESP32 core version detection.
 *
 * Core 3.x publishes ESP_ARDUINO_VERSION_MAJOR. Core 2.x does not define it
 * at all, so defaulting to 2 keeps every adapter compiling on both lines --
 * which is the whole point of this file.
 *
 * Do not include this from the portable core. It exists only for adapters.
 */
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ADR_CORE_MAJOR_VERSION 2
#else
#define ADR_CORE_MAJOR_VERSION ESP_ARDUINO_VERSION_MAJOR
#endif

#if ADR_CORE_MAJOR_VERSION >= 3
#define ADR_HAS_ESP_I2S 1
#else
#define ADR_HAS_ESP_I2S 0
#endif

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_CORE_VERSION_H