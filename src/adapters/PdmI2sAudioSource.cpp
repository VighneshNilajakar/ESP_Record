#include "PdmI2sAudioSource.h"

#if defined(ESP32) && ADR_HAS_ESP_I2S && defined(SOC_I2S_SUPPORTS_PDM_RX) && SOC_I2S_SUPPORTS_PDM_RX

namespace AudioDatasetRecorder {
namespace adapters {

Status PdmI2sAudioSource::begin(const AudioFormat &format, SampleSlot slot) {
    if (running_) end();
    if (clock_ < 0 || data_ < 0) return Status::InvalidConfig;
    if (format.sampleRateHz == 0) return Status::InvalidConfig;
    if (format.channels != 1) return Status::NotSupported;

    /* PDM RX is bound to I2S port 0 by the hardware, and I2SClass chooses it. */
    i2s_.setPinsPdmRx(clock_, data_);

    int8_t slotMask = I2S_STD_SLOT_LEFT;
    if (slot == SampleSlot::Right) slotMask = I2S_STD_SLOT_RIGHT;

    /* PDM receive is fixed at 16-bit and converts to PCM in hardware. */
    if (!i2s_.begin(I2S_MODE_PDM_RX, format.sampleRateHz, I2S_DATA_BIT_WIDTH_16BIT,
                    I2S_SLOT_MODE_MONO, slotMask)) {
        return Status::AudioBeginFailed;
    }

    int32_t discard[64];
    i2s_.readBytes(reinterpret_cast<char *>(discard), sizeof(discard));

    running_ = true;
    return Status::Ok;
}

Status PdmI2sAudioSource::readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead,
                                    uint32_t timeoutMs) {
    wordsRead = 0;
    if (!running_) return Status::AudioReadFailed;
    if (dst == nullptr || capacityWords == 0) return Status::InvalidConfig;

    const size_t received =
        i2s_.readBytes(reinterpret_cast<char *>(dst), capacityWords * sizeof(int32_t));
    wordsRead = received / sizeof(int32_t);
    (void)timeoutMs;
    return Status::Ok;
}

void PdmI2sAudioSource::end() {
    if (!running_) return;
    i2s_.end();
    running_ = false;
}

StreamLayout PdmI2sAudioSource::layout() const {
    StreamLayout result;
    result.output = SourceOutput::Pcm16;
    result.wordsPerFrame = 1;
    result.samplesPerWord = 2;
    return result;
}

}  // namespace adapters
}  // namespace AudioDatasetRecorder

#endif  // ESP32 && 3.x && PDM RX