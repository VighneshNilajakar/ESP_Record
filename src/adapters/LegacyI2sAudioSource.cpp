#include "LegacyI2sAudioSource.h"

#if defined(ESP32) && !ADR_HAS_ESP_I2S

namespace AudioDatasetRecorder {
namespace adapters {

Status LegacyI2sAudioSource::begin(const AudioFormat &format, SampleSlot slot) {
    if (running_) end();
    if (bclk_ < 0 || ws_ < 0 || data_ < 0) return Status::InvalidConfig;
    if (format.sampleRateHz == 0) return Status::InvalidConfig;

    /* Value-initialised: 2.x's i2s_config_t has fields (mclk_multiple,
     * bits_per_chan) that this adapter does not set, and leaving them
     * uninitialised feeds stack garbage to the driver. */
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = format.sampleRateHz;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.communication_format = I2S_COMM_FORMAT_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = dmaBufCount_;
    config.dma_buf_len = dmaBufLen_;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    if (format.channels >= 2) {
        config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        wordsPerFrame_ = 2;
    } else if (slot == SampleSlot::Right) {
        config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
        wordsPerFrame_ = 1;
    } else {
        /* ONLY_LEFT, and also the Both case: with a single data line a
         * one-slot stream is all the cable provides, and the core averages
         * identical words, which is a no-op. */
        config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
        wordsPerFrame_ = 1;
    }

    if (i2s_driver_install(port_, &config, 0, nullptr) != ESP_OK) {
        return Status::AudioBeginFailed;
    }

    i2s_pin_config_t pins;
    pins.bck_io_num = bclk_;
    pins.ws_io_num = ws_;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = data_;
    pins.mck_io_num = mclk_ < 0 ? I2S_PIN_NO_CHANGE : mclk_;

    const esp_err_t pinResult = i2s_set_pin(port_, &pins);
    if (pinResult != ESP_OK) {
        i2s_driver_uninstall(port_);
        return Status::AudioBeginFailed;
    }

    /* Legacy drivers can leave stale DMA contents behind after a reset; drop a
     * short burst so the first recorded frame is not a click. */
    i2s_zero_dma_buffer(port_);
    running_ = true;
    return Status::Ok;
}

Status LegacyI2sAudioSource::readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead,
                                       uint32_t timeoutMs) {
    wordsRead = 0;
    if (!running_) return Status::AudioReadFailed;
    if (dst == nullptr || capacityWords == 0) return Status::InvalidConfig;

    const size_t bytes = capacityWords * sizeof(int32_t);
    size_t received = 0;
    const esp_err_t result =
        i2s_read(port_, dst, bytes, &received, pdMS_TO_TICKS(timeoutMs));

    if (result != ESP_OK) {
        wordsRead = received / sizeof(int32_t);
        /* A timeout just means the peripheral is still filling the buffer. The
         * controller's own watchdog decides when a capture is really stuck. */
        return result == ESP_ERR_TIMEOUT ? Status::Ok : Status::AudioReadFailed;
    }

    wordsRead = received / sizeof(int32_t);
    return Status::Ok;
}

void LegacyI2sAudioSource::end() {
    if (!running_) return;
    i2s_stop(port_);
    i2s_zero_dma_buffer(port_);
    i2s_driver_uninstall(port_);
    running_ = false;
}

StreamLayout LegacyI2sAudioSource::layout() const {
    StreamLayout result;
    result.output = SourceOutput::RawI2sWords;
    result.samplesPerWord = 1;
    /* ONLY_LEFT and ONLY_RIGHT emit one word per frame; ALL emits both. */
    result.wordsPerFrame = wordsPerFrame_;
    return result;
}

}  // namespace adapters
}  // namespace AudioDatasetRecorder

#endif  // ESP32 && !ADR_HAS_ESP_I2S