#ifndef AUDIO_DATASET_RECORDER_H
#define AUDIO_DATASET_RECORDER_H

/*
 * AudioDatasetRecorder -- labelled microphone capture to WAV on ESP-class
 * boards, for TinyML audio datasets such as Edge Impulse keyword spotting.
 *
 * This umbrella header pulls in the whole public surface. Including it gives
 * the same three entry points the 0.1.x releases exposed
 * (AudioStats, i2sWordToPcm16, writePcm16WavHeader) plus the 0.2.0 portable
 * core and interfaces.
 *
 * Layering:
 *
 *   AudioStats.h, PcmConvert.h, WavWriter.h, Labels.h
 *       Pure utilities. No Arduino or Espressif headers except Write's
 *       use of the abstract Print sink, so they compile on a host.
 *   IAudioSource.h, IStorageBackend.h, IIndicator.h, Services.h
 *       Interfaces. The core depends on these, never on a chip SDK.
 *   RecorderController.h
 *       Board-agnostic sequencing: capture, statistics, byte-exact save.
 *   adapters/, profiles/
 *       The only code that touches a chip, a core version or a board's pins.
 *
 * Nothing in the portable core configures a peripheral, picks GPIOs, or mounts
 * storage: those vary by chip, Arduino-ESP32 core version, board routing and
 * bus, and claiming otherwise would be a lie the first user hits.
 */
#include "AudioStats.h"
#include "IIndicator.h"
#include "IAudioSource.h"
#include "IStorageBackend.h"
#include "Labels.h"
#include "PcmConvert.h"
#include "RecorderController.h"
#include "RecorderTypes.h"
#include "Services.h"
#include "WavWriter.h"

#endif  // AUDIO_DATASET_RECORDER_H