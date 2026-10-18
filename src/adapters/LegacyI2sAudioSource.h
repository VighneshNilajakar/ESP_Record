#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_LEGACY_I2S_AUDIO_SOURCE_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_LEGACY_I2S_AUDIO_SOURCE_H

#include "CoreVersion.h"

#include "../IAudioSource.h"

#if defined(ESP32) && !ADR_HAS_ESP_I2S

#include "driver/i2s.h"

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * I2S microphone for Arduino-ESP32 2.x, using the legacy driver/i2s.h API.
 *
 * This is the adapter behind the AI-Thinker ESP32-CAM + INMP441 reference
 * profile, which is the one setup the project has observed working.
 *
 * Data alignment: the peripheral is configured for 32-bit slots, so an INMP441
 * sample arrives as a 24-bit value left-aligned in a 32-bit word. The core
 * therefore applies the configured right shift. Mono uses
 * I2S_CHANNEL_FMT_ONLY_LEFT / ONLY_RIGHT, which makes the driver emit one word
 * per frame with the slot already selected.
 */
class LegacyI2sAudioSource : public IAudioSource {
public:
    LegacyI2sAudioSource()
        : port_(I2S_NUM_0), bclk_(-1), ws_(-1), data_(-1), mclk_(-1),
          dmaBufCount_(8), dmaBufLen_(256), running_(false), wordsPerFrame_(1) {}

    void configurePins(int8_t bclk, int8_t ws, int8_t data, int8_t mclk = -1) {
        bclk_ = bclk;
        ws_ = ws;
        data_ = data;
        mclk_ = mclk;
    }

    void configurePort(i2s_port_t port) { port_ = port; }

    /* DMA depth. Larger buffers tolerate longer blocking reads; the ESP-IDF
     * guidance is to keep each buffer under 4092 bytes. */
    void configureDma(uint8_t bufferCount, uint16_t bufferFrames) {
        dmaBufCount_ = bufferCount;
        dmaBufLen_ = bufferFrames;
    }

    Status begin(const AudioFormat &format, SampleSlot slot);
    Status readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead, uint32_t timeoutMs);
    void end();
    bool isRunning() const { return running_; }

    SourceOutput outputFormat() const { return SourceOutput::RawI2sWords; }
    StreamLayout layout() const;
    const char *name() const { return "esp32-legacy-i2s"; }

private:
    i2s_port_t port_;
    int8_t bclk_;
    int8_t ws_;
    int8_t data_;
    int8_t mclk_;
    uint8_t dmaBufCount_;
    uint16_t dmaBufLen_;
    bool running_;
    uint16_t wordsPerFrame_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32 && !ADR_HAS_ESP_I2S

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_LEGACY_I2S_AUDIO_SOURCE_H