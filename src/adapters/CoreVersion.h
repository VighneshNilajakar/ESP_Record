#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_CORE_VERSION_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_CORE_VERSION_H

/*
 * Arduino-ESP32 core version detection.
 *
 * The macro that carries the version lives in esp_arduino_version.h, which
 * Arduino.h pulls in. An adapter's .cpp includes its own header before
 * Arduino.h, so relying on the macro being visible by then is how the whole
 * 3.x adapter once compiled itself out and left an undefined vtable: the
 * translation unit decided it was 2.x while the sketch decided it was 3.x.
 * Including the header explicitly makes every translation unit agree.
 *
 * Do not include this from the portable core. It exists only for adapters.
 *
 * Both 2.x and 3.x ship esp_arduino_version.h, but a very old core may not, so
 * the include is conditional and the fallback stays 2.
 */
#if defined(__has_include)
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif
#endif

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