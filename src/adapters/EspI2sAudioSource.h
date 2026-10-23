#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_ESP_I2S_AUDIO_SOURCE_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_ESP_I2S_AUDIO_SOURCE_H

#include "CoreVersion.h"

#include "../IAudioSource.h"

#if defined(ESP32) && ADR_HAS_ESP_I2S

#include <ESP_I2S.h>

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * I2S microphone for Arduino-ESP32 3.x, using the ESP_I2S.h API that replaced
 * driver/i2s.h.
 *
 * The driver is configured for 32-bit slots even though PCM16 is what gets
 * stored. That is deliberate: 32-bit slots sidestep the original ESP32's
 * hardware v1 FIFO packing defect, which affects 8-bit and 16-bit mono receive
 * and scrambles the data order every other frame. With 32-bit slots the driver
 * hands back the raw word and the core applies the configured right shift, the
 * same code path the 2.x reference profile uses.
 *
 * Requires a chip with an I2S controller. The ESP32-CAM reference can be run on
 * core 3.x with this adapter, but the 2.x profile remains the one this project
 * has observed on real hardware.
 */
class EspI2sAudioSource : public IAudioSource {
public:
    EspI2sAudioSource()
        : bclk_(-1), ws_(-1), data_(-1), mclk_(-1), running_(false),
          wordsPerFrame_(1) {}

    void configurePins(int8_t bclk, int8_t ws, int8_t data, int8_t mclk = -1) {
        bclk_ = bclk;
        ws_ = ws;
        data_ = data;
        mclk_ = mclk;
    }

    Status begin(const AudioFormat &format, SampleSlot slot);
    Status readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead, uint32_t timeoutMs);
    void end();
    bool isRunning() const { return running_; }

    SourceOutput outputFormat() const { return SourceOutput::RawI2sWords; }
    StreamLayout layout() const;
    const char *name() const { return "esp32-esp-i2s"; }

private:
    I2SClass i2s_;

    int8_t bclk_;
    int8_t ws_;
    int8_t data_;
    int8_t mclk_;
    bool running_;
    uint16_t wordsPerFrame_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32 && ADR_HAS_ESP_I2S

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_ESP_I2S_AUDIO_SOURCE_H