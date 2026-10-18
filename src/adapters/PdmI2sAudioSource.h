#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_PDM_I2S_AUDIO_SOURCE_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_PDM_I2S_AUDIO_SOURCE_H

#include "CoreVersion.h"

#include "../IAudioSource.h"

#if defined(ESP32) && ADR_HAS_ESP_I2S

#include <ESP_I2S.h>

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * Digital PDM microphone (MP34GT01, ICS-43434, INMP441D and similar) on the
 * I2S PDM receive port, Arduino-ESP32 3.x.
 *
 * PDM RX exists only on I2S port 0, and the PDM data width is fixed at 16 bits.
 * The hardware converts PDM to PCM, so this adapter reports
 * SourceOutput::Pcm16 and the core applies no right shift -- that is the whole
 * point of SourceOutput: shifting an already-converted PCM16 stream a second
 * time would halve it.
 *
 * The driver reduces the stream to the selected slot, so samplesPerWord is 2
 * (two consecutive 16-bit samples per 32-bit read) and wordsPerFrame is 1.
 * A Pcm16 source is mono by construction, so a stereo request is refused by
 * the converter rather than silently faked.
 */
class PdmI2sAudioSource : public IAudioSource {
public:
    PdmI2sAudioSource() : clock_(-1), data_(-1), running_(false) {}

    void configurePins(int8_t clock, int8_t data) {
        clock_ = clock;
        data_ = data;
    }

    Status begin(const AudioFormat &format, SampleSlot slot);
    Status readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead, uint32_t timeoutMs);
    void end();
    bool isRunning() const { return running_; }

    SourceOutput outputFormat() const { return SourceOutput::Pcm16; }
    StreamLayout layout() const;
    const char *name() const { return "esp32-pdm-rx"; }

private:
    /* PDM RX is bound to I2S0 by the hardware. */
    I2SClass i2s_;

    int8_t clock_;
    int8_t data_;
    bool running_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32 && ADR_HAS_ESP_I2S

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_PDM_I2S_AUDIO_SOURCE_H