#ifndef AUDIO_DATASET_RECORDER_PROFILES_BOARD_PROFILES_H
#define AUDIO_DATASET_RECORDER_PROFILES_BOARD_PROFILES_H

#include <stdint.h>

#include "../RecorderTypes.h"

namespace AudioDatasetRecorder {
namespace profiles {

/*
 * Named pin maps.
 *
 * These are documentation and defaults, NOT verified support. A pin map is only
 * trustworthy for the exact board revision it was measured on: routers move
 * pins, and several of the interesting GPIOs are strapping pins whose state at
 * reset decides whether the chip boots at all.
 *
 * Before trusting any entry: check the board's schematic for what is actually
 * broken out, confirm none of the pins collide with flash, PSRAM or the storage
 * bus, and verify the result with a real recording.
 */
struct AudioPinMap {
    int8_t bclk;
    int8_t ws;
    int8_t data;
    int8_t mclk;      /* -1 when unused */
    int8_t pdmClock;  /* -1 for I2S mics, set for PDM mics */

    AudioPinMap() : bclk(-1), ws(-1), data(-1), mclk(-1), pdmClock(-1) {}
    AudioPinMap(int8_t b, int8_t w, int8_t d) : bclk(b), ws(w), data(d), mclk(-1), pdmClock(-1) {}

    /* PDM receive needs only a clock and a data line, and no BCLK/WS. */
    static AudioPinMap pdm(int8_t clock, int8_t d) {
        AudioPinMap map;
        map.pdmClock = clock;
        map.data = d;
        return map;
    }

    bool isPdm() const { return pdmClock >= 0; }
};

/*
 * AI-Thinker ESP32-CAM (original ESP32) with an INMP441 and the on-board
 * microSD.
 *
 * This is the only pin map in this project backed by an observed successful
 * save. Note how constrained it is:
 *   - GPIO14 and GPIO15 are shared with SD_MMC, so the card must not be
 *     mounted while I2S is running. Pair this with CaptureStrategy::FullBuffer.
 *   - GPIO12 is both a boot-strapping pin and SD DAT2. It was moved here from
 *     GPIO13 because a save succeeded; treat that as an observed workaround
 *     for one board revision, not a recommendation.
 *   - GPIO4 doubles as the flash LED and SD D1, which is why the indicator is
 *     only driven while the card is unmounted.
 *   - GPIO16 is usually occupied by PSRAM. Do not use it.
 */
extern const AudioPinMap kAiThinkerEsp32CamInmp441;

/*
 * Generic ESP32 dev board defaults (e.g. ESP32 DevKit v1). Only safe where the
 * pins are actually free: most classic DevKit boards break out GPIO13, 14, 15,
 * 27, 32 but also route GPIO6-11 to the SPI flash, and only GPIO12 is free
 * among the classic set. Check the schematic.
 */
extern const AudioPinMap kGenericEsp32DevKit;

/*
 * Generic ESP32-S3 defaults. S3 has no SD_MMC pin-sharing conflict with these
 * pins, so streaming capture is viable with SPI SD.
 */
extern const AudioPinMap kGenericEsp32S3;

/* Generic ESP32-C3 defaults. The C3 has a single I2S controller, so no second
 * port is available and dual-channel concurrent capture is impossible. */
extern const AudioPinMap kGenericEsp32C3;

/* Generic ESP32-S2 defaults. */
extern const AudioPinMap kGenericEsp32S2;

/*
 * A PDM microphone map for ESP32-S3 / ESP32-C3 style PDM RX on I2S port 0.
 */
extern const AudioPinMap kGenericEsp32S3Pdm;

}  // namespace profiles
}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_PROFILES_BOARD_PROFILES_H