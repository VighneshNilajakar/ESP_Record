#include "EspI2sAudioSource.h"

#if defined(ESP32) && ADR_HAS_ESP_I2S

namespace AudioDatasetRecorder {
namespace adapters {

Status EspI2sAudioSource::begin(const AudioFormat &format, SampleSlot slot) {
    if (running_) end();
    if (bclk_ < 0 || ws_ < 0 || data_ < 0) return Status::InvalidConfig;
    if (format.sampleRateHz == 0) return Status::InvalidConfig;

    i2s_.setPort(port_);
    /* Signature is (bclk, ws, dout, din, mclk); this is a receive-only source. */
    i2s_.setPins(bclk_, ws_, -1, data_, mclk_);

    i2s_slot_mode_t slotMode =
        format.channels >= 2 ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;

    int8_t slotMask = I2S_STD_SLOT_LEFT;
    wordsPerFrame_ = 1;
    if (format.channels >= 2) {
        slotMask = I2S_STD_SLOT_BOTH;
        wordsPerFrame_ = 2;
    } else if (slot == SampleSlot::Right) {
        slotMask = I2S_STD_SLOT_RIGHT;
    } else if (slot == SampleSlot::Both) {
        slotMask = I2S_STD_SLOT_BOTH;
        wordsPerFrame_ = 2;
    }

    /* 32-bit slots keep the raw word intact so the core's right shift is the
     * single place where alignment is decided. */
    if (!i2s_.begin(I2S_MODE_STD, format.sampleRateHz, I2S_DATA_BIT_WIDTH_32BIT, slotMode, slotMask,
                    I2S_ROLE_MASTER)) {
        return Status::AudioBeginFailed;
    }

    /* Absorb DMA fill transients so the clip does not open with a click. */
    int32_t discard[64];
    size_t read = 0;
    i2s_.readBytes(reinterpret_cast<char *>(discard), sizeof(discard));
    (void)read;

    running_ = true;
    return Status::Ok;
}

Status EspI2sAudioSource::readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead,
                                    uint32_t timeoutMs) {
    wordsRead = 0;
    if (!running_) return Status::AudioReadFailed;
    if (dst == nullptr || capacityWords == 0) return Status::InvalidConfig;

    const size_t requested = capacityWords * sizeof(int32_t);
    const size_t received =
        i2s_.readBytes(reinterpret_cast<char *>(dst), requested);

    wordsRead = received / sizeof(int32_t);
    /* readBytes is non-blocking and returns 0 while the DMA buffer is still
     * filling. That is normal; the controller's watchdog owns stall detection
     * and timeoutMs is accepted for interface uniformity. */
    (void)timeoutMs;
    return Status::Ok;
}

void EspI2sAudioSource::end() {
    if (!running_) return;
    i2s_.end();
    running_ = false;
}

StreamLayout EspI2sAudioSource::layout() const {
    StreamLayout result;
    result.output = SourceOutput::RawI2sWords;
    result.samplesPerWord = 1;
    result.wordsPerFrame = wordsPerFrame_;
    return result;
}

}  // namespace adapters
}  // namespace AudioDatasetRecorder

#endif  // ESP32 && ADR_HAS_ESP_I2S