#ifndef AUDIO_DATASET_RECORDER_WAV_WRITER_H
#define AUDIO_DATASET_RECORDER_WAV_WRITER_H

#include <Arduino.h>

#include <stdint.h>

namespace AudioDatasetRecorder {

/* Size of the canonical PCM RIFF/WAVE header this library writes. */
static const uint32_t kWavHeaderBytes = 44U;

/*
 * Largest payload representable by the classic 32-bit RIFF size fields.
 * Anything beyond this would need RF64 and is rejected rather than truncated.
 */
static const uint64_t kWavMaxDataBytes = 0xFFFFFFFFULL - 36ULL;

/*
 * Write a canonical 44-byte RIFF/WAVE PCM16 header to any Arduino Print sink.
 *
 * `sampleFramesPerChannel` counts frames, not samples, so a stereo file passes
 * the same value for either channel count. Returns false for a zero rate, zero
 * channels, zero frames, a channel count that overflows the 16-bit blockAlign
 * field, or a length the 32-bit RIFF fields cannot express. Also returns false
 * if the sink does not accept all 44 bytes.
 */
bool writePcm16WavHeader(Print &output,
                         uint32_t sampleRateHz,
                         uint16_t channels,
                         uint64_t sampleFramesPerChannel);

/* Bytes of PCM payload a clip of `frames` frames occupies. 0 if it cannot fit. */
uint64_t wavDataBytes(uint16_t channels, uint64_t frames);

/* Total on-disk size including the header. 0 if the payload cannot fit. */
uint64_t wavFileBytes(uint16_t channels, uint64_t frames);

/* True when the combination is representable in the classic 32-bit WAV format. */
bool wavFitsClassicRiff(uint16_t channels, uint64_t frames);

/*
 * Read the 44-byte header of an already-written WAV and check it against the
 * expected rate, channel count and payload size. Used after a save to confirm
 * the file on storage really is what we intended to write.
 */
enum class WavHeaderCheck : uint8_t {
    Ok = 0,
    TooShort,
    BadMagic,
    BadFormat,
    RateMismatch,
    ChannelMismatch,
    SizeMismatch,
    Truncated,
};

struct WavHeaderInfo {
    uint32_t sampleRateHz;
    uint16_t channels;
    uint16_t bitsPerSample;
    uint64_t dataBytes;

    WavHeaderInfo() : sampleRateHz(0), channels(0), bitsPerSample(0), dataBytes(0) {}
};

/*
 * `header` must hold at least kWavHeaderBytes bytes. On `Truncated`, `info`
 * still reports what the header claimed, which is how an incomplete write is
 * detected.
 */
WavHeaderCheck inspectPcm16WavHeader(const uint8_t *header,
                                     size_t headerLength,
                                     uint32_t expectedSampleRateHz,
                                     uint16_t expectedChannels,
                                     uint64_t expectedDataBytes,
                                     WavHeaderInfo &info);

const char *toString(WavHeaderCheck check);

/*
 * Minimal Print sink over a caller-owned byte array. Lets the core reuse the
 * one canonical header writer without the core itself depending on Arduino's
 * Print implementation details beyond the single virtual it needs.
 */
class ByteArrayPrint : public Print {
public:
    ByteArrayPrint(uint8_t *buffer, size_t capacity)
        : buffer_(buffer), capacity_(capacity), written_(0), lastWriteShort_(false) {}

    size_t write(const uint8_t *data, size_t length) {
        if (buffer_ == nullptr || data == nullptr) return 0;
        if (written_ + length > capacity_) {
            lastWriteShort_ = true;
            const size_t room = capacity_ - written_;
            for (size_t i = 0; i < room; ++i) buffer_[written_ + i] = data[i];
            written_ = capacity_;
            return room;
        }
        for (size_t i = 0; i < length; ++i) buffer_[written_ + i] = data[i];
        written_ += length;
        return length;
    }

    size_t written() const { return written_; }
    bool lastWriteShort() const { return lastWriteShort_; }

private:
    uint8_t *buffer_;
    size_t capacity_;
    size_t written_;
    bool lastWriteShort_;
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_WAV_WRITER_H