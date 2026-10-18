#include "PcmConvert.h"

namespace AudioDatasetRecorder {

int16_t i2sWordToPcm16(int32_t word, uint8_t rightShift, bool *wasClipped) {
    if (wasClipped) *wasClipped = false;
    if (rightShift > 31U) rightShift = 31U;

    // Right-shifting a negative signed value is implementation-defined before
    // C++20 but is an arithmetic shift on every ESP target and on gcc/clang/msvc.
    int32_t scaled = word >> rightShift;
    if (scaled > INT16_MAX) {
        scaled = INT16_MAX;
        if (wasClipped) *wasClipped = true;
    } else if (scaled < INT16_MIN) {
        scaled = INT16_MIN;
        if (wasClipped) *wasClipped = true;
    }
    return static_cast<int16_t>(scaled);
}

int16_t applyAttenuation(int16_t sample, uint8_t shift) {
    if (shift == 0U) return sample;
    if (shift >= 15U) return 0;
    return static_cast<int16_t>(static_cast<int32_t>(sample) >> shift);
}

SampleConverter::SampleConverter()
    : output_(SourceOutput::RawI2sWords),
      wordsPerFrame_(1),
      samplesPerWord_(1),
      channels_(1),
      slot_(SampleSlot::Left),
      rightShift_(0),
      attenuationShift_(0),
      configured_(false) {}

Status SampleConverter::configure(const StreamLayout &layout, const RecorderConfig &config) {
    configured_ = false;

    if (config.channels != 1 && config.channels != 2) return Status::InvalidConfig;
    if (config.attenuationShift > 31U) return Status::InvalidConfig;

    if (layout.wordsPerFrame != 1 && layout.wordsPerFrame != 2) return Status::NotSupported;

    if (layout.output == SourceOutput::Pcm16) {
        /* The driver already converted and already chose the slot. Shifting
         * again would halve or quarter an already-correct signal, and a stereo
         * request cannot be served from a stream that was reduced to mono. */
        if (layout.wordsPerFrame != 1) return Status::NotSupported;
        if (layout.samplesPerWord != 1 && layout.samplesPerWord != 2) return Status::NotSupported;
        if (config.channels != 1) return Status::NotSupported;
        if (config.slot == SampleSlot::Both) return Status::NotSupported;
    } else {
        if (layout.samplesPerWord != 1) return Status::NotSupported;
        if (config.channels == 2 && layout.wordsPerFrame != 2) return Status::NotSupported;
    }

    output_ = layout.output;
    wordsPerFrame_ = layout.wordsPerFrame;
    samplesPerWord_ = layout.samplesPerWord;
    channels_ = config.channels;
    slot_ = config.slot;
    /* The right shift only applies to raw words. */
    rightShift_ = (output_ == SourceOutput::RawI2sWords) ? config.rightShift : 0U;
    attenuationShift_ = config.attenuationShift;
    configured_ = true;
    return Status::Ok;
}

size_t SampleConverter::framesFromWords(size_t wordCount) const {
    if (!configured_ || wordsPerFrame_ == 0U) return 0;
    return wordCount / wordsPerFrame_;
}

size_t SampleConverter::samplesForWords(size_t wordCount) const {
    if (!configured_ || wordsPerFrame_ == 0U) return 0;
    const size_t frames = wordCount / wordsPerFrame_;
    if (output_ == SourceOutput::Pcm16) return frames * samplesPerWord_;
    return frames * channels_;
}

int16_t SampleConverter::scale(int32_t raw, bool &clipped) const {
    int16_t value = i2sWordToPcm16(raw, rightShift_, &clipped);
    value = applyAttenuation(value, attenuationShift_);
    return value;
}

bool SampleConverter::convertBlock(const int32_t *words,
                                  size_t wordCount,
                                  int16_t *out,
                                  size_t outCapacitySamples,
                                  size_t &outSamples,
                                  AudioStats &stats) const {
    outSamples = 0;
    if (!configured_) return false;
    if (words == nullptr || out == nullptr) return false;
    if (wordCount < wordsPerFrame_) return false;

    const size_t frames = wordCount / wordsPerFrame_;
    if (frames == 0) return true;

    const size_t required = samplesForWords(wordCount);
    if (required > outCapacitySamples) return false;

    size_t written = 0;

    if (output_ == SourceOutput::Pcm16) {
        /* One word carries one or two little-endian PCM16 samples that the
         * driver already converted and already slot-selected. */
        for (size_t f = 0; f < frames; ++f) {
            const uint32_t packed = static_cast<uint32_t>(words[f]);
            for (uint8_t s = 0; s < samplesPerWord_; ++s) {
                const uint16_t raw = static_cast<uint16_t>((packed >> (16U * s)) & 0xFFFFU);
                const int16_t value = applyAttenuation(static_cast<int16_t>(raw), attenuationShift_);
                out[written++] = value;
                /* Attenuation cannot overflow, so nothing was clipped here. */
                stats.addSample(value, false);
            }
        }
        outSamples = written;
        return true;
    }

    for (size_t f = 0; f < frames; ++f) {
        const int32_t *frame = words + (f * wordsPerFrame_);

        if (channels_ == 2) {
            for (int c = 0; c < 2; ++c) {
                bool clipped = false;
                const int16_t value = scale(frame[c], clipped);
                out[written++] = value;
                stats.addSample(value, clipped);
            }
            continue;
        }

        int32_t selected;
        if (wordsPerFrame_ == 1) {
            selected = frame[0];
        } else if (slot_ == SampleSlot::Right) {
            selected = frame[1];
        } else if (slot_ == SampleSlot::Both) {
            /* Average before scaling so the sum cannot overflow int32. */
            selected = static_cast<int32_t>((static_cast<int64_t>(frame[0]) +
                                             static_cast<int64_t>(frame[1])) / 2);
        } else {
            selected = frame[0];
        }

        bool clipped = false;
        const int16_t value = scale(selected, clipped);
        out[written++] = value;
        stats.addSample(value, clipped);
    }

    outSamples = written;
    return true;
}

}  // namespace AudioDatasetRecorder