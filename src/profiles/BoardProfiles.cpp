#include "BoardProfiles.h"

namespace AudioDatasetRecorder {
namespace profiles {

/*
 * AI-Thinker ESP32-CAM + INMP441 + on-board SD_MMC.
 * BCLK GPIO14, WS GPIO15, DOUT GPIO12, L/R tied to GND (left slot).
 * See the header for why these three pins are a constrained set.
 */
const AudioPinMap kAiThinkerEsp32CamInmp441(14, 15, 12);

/* Generic ESP32 DevKit. GPIO12/13/14/27 are the usual free set on a DevKit v1;
 * none of these touch flash or PSRAM. Verify before use. */
const AudioPinMap kGenericEsp32DevKit(14, 15, 13);

/* Generic ESP32-S3. The S3 has no flash/PSRAM conflict on these GPIOs. */
const AudioPinMap kGenericEsp32S3(4, 5, 6);

/* Generic ESP32-C3. Single I2S controller; use I2S_NUM_0 only. */
const AudioPinMap kGenericEsp32C3(4, 5, 6);

/* Generic ESP32-S2. */
const AudioPinMap kGenericEsp32S2(10, 11, 12);

/* Generic ESP32-S3 PDM RX on I2S0: clock GPIO7, data GPIO8. */
const AudioPinMap kGenericEsp32S3Pdm = AudioPinMap::pdm(7, 8);

}  // namespace profiles
}  // namespace AudioDatasetRecorder