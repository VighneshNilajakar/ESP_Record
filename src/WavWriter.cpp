#include "WavWriter.h"

#include <string.h>

namespace AudioDatasetRecorder {
namespace {

uint16_t readU16LE(const uint8_t *p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t readU32LE(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void putU16LE(uint8_t *p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFFU);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void putU32LE(uint8_t *p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value & 0xFFUL);
    p[1] = static_cast<uint8_t>((value >> 8) & 0xFFUL);
    p[2] = static_cast<uint8_t>((value >> 16) & 0xFFUL);
    p[3] = static_cast<uint8_t>((value >> 24) & 0xFFUL);
}

const uint8_t kRiffMagic[4] = {'R', 'I', 'F', 'F'};
const uint8_t kWaveMagic[4] = {'W', 'A', 'V', 'E'};
const uint8_t kFmtMagic[4] = {'f', 'm', 't', ' '};
const uint8_t kDataMagic[4] = {'d', 'a', 't', 'a'};

}  // namespace

bool wavFitsClassicRiff(uint16_t channels, uint64_t frames) {
    if (channels == 0) return false;
    if (channels > UINT16_MAX / 2U) return false;  // blockAlign is stored in 16 bits
    const uint64_t bytesPerFrame = static_cast<uint64_t>(channels) * 2ULL;
    if (bytesPerFrame == 0) return false;
    return frames <= (kWavMaxDataBytes / bytesPerFrame);
}

uint64_t wavDataBytes(uint16_t channels, uint64_t frames) {
    if (!wavFitsClassicRiff(channels, frames)) return 0;
    return frames * static_cast<uint64_t>(channels) * 2ULL;
}

uint64_t wavFileBytes(uint16_t channels, uint64_t frames) {
    const uint64_t data = wavDataBytes(channels, frames);
    if (data == 0) return 0;
    return data + kWavHeaderBytes;
}

bool writePcm16WavHeader(Print &output,
                         uint32_t sampleRateHz,
                         uint16_t channels,
                         uint64_t sampleFramesPerChannel) {
    if (sampleRateHz == 0 || sampleFramesPerChannel == 0) return false;
    if (!wavFitsClassicRiff(channels, sampleFramesPerChannel)) return false;

    const uint64_t dataBytes64 = wavDataBytes(channels, sampleFramesPerChannel);
    if (dataBytes64 > kWavMaxDataBytes) return false;

    const uint16_t blockAlign = static_cast<uint16_t>(static_cast<uint64_t>(channels) * 2ULL);
    if (sampleRateHz > UINT32_MAX / static_cast<uint32_t>(blockAlign)) return false;

    const uint32_t dataBytes = static_cast<uint32_t>(dataBytes64);
    const uint32_t byteRate = sampleRateHz * static_cast<uint32_t>(blockAlign);

    uint8_t header[44] = {0};
    memcpy(header + 0, kRiffMagic, 4);
    putU32LE(header + 4, 36U + dataBytes);
    memcpy(header + 8, kWaveMagic, 4);
    memcpy(header + 12, kFmtMagic, 4);
    putU32LE(header + 16, 16U);
    putU16LE(header + 20, 1U);  // PCM
    putU16LE(header + 22, channels);
    putU32LE(header + 24, sampleRateHz);
    putU32LE(header + 28, byteRate);
    putU16LE(header + 32, blockAlign);
    putU16LE(header + 34, 16U);
    memcpy(header + 36, kDataMagic, 4);
    putU32LE(header + 40, dataBytes);

    return output.write(header, sizeof(header)) == sizeof(header);
}

WavHeaderCheck inspectPcm16WavHeader(const uint8_t *header,
                                     size_t headerLength,
                                     uint32_t expectedSampleRateHz,
                                     uint16_t expectedChannels,
                                     uint64_t expectedDataBytes,
                                     WavHeaderInfo &info) {
    if (header == nullptr || headerLength < kWavHeaderBytes) return WavHeaderCheck::TooShort;
    if (memcmp(header + 0, kRiffMagic, 4) != 0) return WavHeaderCheck::BadMagic;
    if (memcmp(header + 8, kWaveMagic, 4) != 0) return WavHeaderCheck::BadMagic;
    if (memcmp(header + 12, kFmtMagic, 4) != 0) return WavHeaderCheck::BadMagic;
    if (memcmp(header + 36, kDataMagic, 4) != 0) return WavHeaderCheck::BadMagic;
    if (readU32LE(header + 16) != 16U) return WavHeaderCheck::BadFormat;
    if (readU16LE(header + 20) != 1U) return WavHeaderCheck::BadFormat;  // PCM only

    info.sampleRateHz = readU32LE(header + 24);
    info.channels = readU16LE(header + 22);
    info.bitsPerSample = readU16LE(header + 34);
    info.dataBytes = readU32LE(header + 40);

    if (info.bitsPerSample != 16U) return WavHeaderCheck::BadFormat;
    if (info.sampleRateHz != expectedSampleRateHz) return WavHeaderCheck::RateMismatch;
    if (info.channels != expectedChannels) return WavHeaderCheck::ChannelMismatch;

    const uint64_t declaredRiffBytes = static_cast<uint64_t>(readU32LE(header + 4));
    if (declaredRiffBytes != 36ULL + info.dataBytes) return WavHeaderCheck::BadFormat;
    if (info.dataBytes != expectedDataBytes) return WavHeaderCheck::SizeMismatch;

    return WavHeaderCheck::Ok;
}

const char *toString(WavHeaderCheck check) {
    switch (check) {
        case WavHeaderCheck::Ok: return "OK";
        case WavHeaderCheck::TooShort: return "HEADER_TOO_SHORT";
        case WavHeaderCheck::BadMagic: return "BAD_MAGIC";
        case WavHeaderCheck::BadFormat: return "BAD_FORMAT";
        case WavHeaderCheck::RateMismatch: return "RATE_MISMATCH";
        case WavHeaderCheck::ChannelMismatch: return "CHANNEL_MISMATCH";
        case WavHeaderCheck::SizeMismatch: return "SIZE_MISMATCH";
        case WavHeaderCheck::Truncated: return "TRUNCATED";
    }
    return "UNKNOWN";
}

}  // namespace AudioDatasetRecorder