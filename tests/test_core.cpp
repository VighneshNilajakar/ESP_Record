#include "test_core.h"

#include <limits.h>

#include <vector>

#include "AudioDatasetRecorder.h"
#include "TestHarness.h"

using namespace AudioDatasetRecorder;

namespace {

/* Print sink that records bytes, optionally accepting fewer than requested. */
class MemoryPrint : public Print {
public:
    explicit MemoryPrint(size_t capacity) : limit_(capacity) {}
    size_t write(uint8_t value) { return write(&value, 1); }
    size_t write(const uint8_t *data, size_t length) {
        const size_t accepted = length < limit_ ? length : limit_;
        for (size_t i = 0; i < accepted; ++i) bytes.push_back(data[i]);
        return accepted;
    }
    std::vector<uint8_t> bytes;

private:
    size_t limit_;
};

uint32_t readU32(const std::vector<uint8_t> &v, size_t offset) {
    return static_cast<uint32_t>(v[offset]) | (static_cast<uint32_t>(v[offset + 1]) << 8) |
           (static_cast<uint32_t>(v[offset + 2]) << 16) | (static_cast<uint32_t>(v[offset + 3]) << 24);
}

uint16_t readU16(const std::vector<uint8_t> &v, size_t offset) {
    return static_cast<uint16_t>(static_cast<uint16_t>(v[offset]) |
                                 (static_cast<uint16_t>(v[offset + 1]) << 8));
}

/* ------------------------------------------------------------------ stats */

void testAudioStats() {
    AudioStats stats;
    stats.addSample(1000);
    stats.addSample(-1000);
    stats.addSample(32767, true);

    ADR_CHECK_EQ(stats.sampleCount, 3ULL);
    ADR_CHECK_EQ(stats.peakMagnitude, 32767);
    ADR_CHECK_EQ(stats.clippedSamples, 1U);
    ADR_CHECK(stats.rms() > 0.0);
    ADR_CHECK(stats.normalizedRms() < 1.0);

    /* Mean of 1000, -1000, 32767 is a large positive DC bias. */
    ADR_CHECK(stats.mean() > 10000.0);

    stats.reset();
    ADR_CHECK_EQ(stats.sampleCount, 0ULL);
    ADR_CHECK_EQ(stats.peakMagnitude, 0);
    ADR_CHECK_NEAR(stats.rms(), 0.0, 1e-9);
    ADR_CHECK_NEAR(stats.mean(), 0.0, 1e-9);

    /* -32768 has the largest magnitude, and the peak field is uint16_t so
     * 32768 must not wrap. */
    AudioStats extremes;
    extremes.addSample(-32768);
    ADR_CHECK_EQ(extremes.peakMagnitude, 32768);

    /* Empty stats must not divide by zero. */
    AudioStats empty;
    ADR_CHECK_NEAR(empty.rms(), 0.0, 1e-12);
    ADR_CHECK_NEAR(empty.normalizedRms(), 0.0, 1e-12);
}

/* ------------------------------------------------------- PCM conversion */

void testPcmConversion() {
    bool clipped = true;

    ADR_CHECK_EQ(i2sWordToPcm16(0, 14, &clipped), 0);
    ADR_CHECK(!clipped);

    /* 1 << 14 shifted right by 14 is 1. */
    ADR_CHECK_EQ(i2sWordToPcm16(1 << 14, 14, nullptr), 1);
    ADR_CHECK_EQ(i2sWordToPcm16(-(1 << 14), 14, nullptr), -1);

    /* Saturate high. */
    ADR_CHECK_EQ(i2sWordToPcm16(INT32_MAX, 0, &clipped), 32767);
    ADR_CHECK(clipped);

    /* Saturate low. */
    ADR_CHECK_EQ(i2sWordToPcm16(INT32_MIN, 0, &clipped), -32768);
    ADR_CHECK(clipped);

    /* Exactly full scale must not be reported as clipping. */
    ADR_CHECK_EQ(i2sWordToPcm16(32767, 0, &clipped), 32767);
    ADR_CHECK(!clipped);

    /* A shift above 31 is clamped rather than undefined. */
    ADR_CHECK_EQ(i2sWordToPcm16(INT32_MIN, 200, nullptr), -1);

    /* wasClipped is written on every call, not only when clipping occurs. */
    clipped = true;
    i2sWordToPcm16(0, 0, &clipped);
    ADR_CHECK(!clipped);

    /* A null pointer must be accepted. */
    ADR_CHECK_EQ(i2sWordToPcm16(1234, 1, nullptr), 617);
}

void testAttenuation() {
    ADR_CHECK_EQ(applyAttenuation(1234, 0), 1234);
    ADR_CHECK_EQ(applyAttenuation(-1234, 0), -1234);
    ADR_CHECK_EQ(applyAttenuation(1234, 1), 617);
    ADR_CHECK_EQ(applyAttenuation(-1234, 1), -617);
    ADR_CHECK_EQ(applyAttenuation(100, 2), 25);
    /* 15 or more must be silence, never a wrap. */
    ADR_CHECK_EQ(applyAttenuation(1234, 15), 0);
    ADR_CHECK_EQ(applyAttenuation(-1234, 15), 0);
    ADR_CHECK_EQ(applyAttenuation(-1234, 14), -1);
}

/* --------------------------------------------------------- WAV header */

void testWavHeaderFields() {
    MemoryPrint sink(1024);
    ADR_CHECK(writePcm16WavHeader(sink, 16000, 1, 320000));

    ADR_CHECK_EQ(sink.bytes.size(), static_cast<size_t>(44));
    ADR_CHECK(memcmp(&sink.bytes[0], "RIFF", 4) == 0);
    ADR_CHECK_EQ(readU32(sink.bytes, 4), 640036U);
    ADR_CHECK(memcmp(&sink.bytes[8], "WAVE", 4) == 0);
    ADR_CHECK(memcmp(&sink.bytes[12], "fmt ", 4) == 0);
    ADR_CHECK_EQ(readU32(sink.bytes, 16), 16U);
    ADR_CHECK_EQ(readU16(sink.bytes, 20), 1);
    ADR_CHECK_EQ(readU16(sink.bytes, 22), 1);
    ADR_CHECK_EQ(readU32(sink.bytes, 24), 16000U);
    ADR_CHECK_EQ(readU32(sink.bytes, 28), 32000U);
    ADR_CHECK_EQ(readU16(sink.bytes, 32), 2);
    ADR_CHECK_EQ(readU16(sink.bytes, 34), 16);
    ADR_CHECK(memcmp(&sink.bytes[36], "data", 4) == 0);
    ADR_CHECK_EQ(readU32(sink.bytes, 40), 640000U);
}

void testWavHeaderStereo() {
    MemoryPrint sink(1024);
    ADR_CHECK(writePcm16WavHeader(sink, 48000, 2, 48000));
    ADR_CHECK_EQ(sink.bytes.size(), static_cast<size_t>(44));
    ADR_CHECK_EQ(readU16(sink.bytes, 22), 2);
    ADR_CHECK_EQ(readU16(sink.bytes, 32), 4);
    ADR_CHECK_EQ(readU32(sink.bytes, 28), 192000U);
    ADR_CHECK_EQ(readU32(sink.bytes, 40), 192000U);
    ADR_CHECK_EQ(readU32(sink.bytes, 4), 192036U);
}

void testWavHeaderRejections() {
    MemoryPrint sink(1024);
    ADR_CHECK(!writePcm16WavHeader(sink, 0, 1, 100));
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, 0, 100));
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, 1, 0));
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, 1, UINT64_MAX));
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, UINT16_MAX, 100));
    /* blockAlign is 16-bit, so the channel count times 2 must fit. */
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, 40000, 100));
    ADR_CHECK_EQ(sink.bytes.size(), static_cast<size_t>(0));

    /* 8.9 GB exceeds the 32-bit RIFF limit and must be refused rather than
     * silently truncated into a file whose header disagrees with its payload. */
    const uint64_t tooMany = 9000000000ULL;
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, 1, tooMany));
    ADR_CHECK(!wavFitsClassicRiff(1, tooMany));
    ADR_CHECK_EQ(wavDataBytes(1, tooMany), 0ULL);
    ADR_CHECK_EQ(wavFileBytes(1, tooMany), 0ULL);
}

void testWavSizeHelpers() {
    ADR_CHECK_EQ(wavDataBytes(1, 16000), 32000ULL);
    ADR_CHECK_EQ(wavFileBytes(1, 16000), 32044ULL);
    ADR_CHECK_EQ(wavDataBytes(2, 16000), 64000ULL);
    ADR_CHECK_EQ(wavFileBytes(2, 16000), 64044ULL);
    ADR_CHECK_EQ(wavDataBytes(1, 0), 0ULL);
    ADR_CHECK_EQ(wavFileBytes(1, 0), 0ULL);
}

void testWavShortWriteIsReported() {
    /* A sink that accepts fewer than 44 bytes must produce false, so a short
     * write is never mistaken for a complete header. */
    MemoryPrint sink(10);
    ADR_CHECK(!writePcm16WavHeader(sink, 16000, 1, 16000));
    ADR_CHECK_EQ(sink.bytes.size(), static_cast<size_t>(10));
}

void testWavHeaderInspection() {
    MemoryPrint sink(1024);
    ADR_CHECK(writePcm16WavHeader(sink, 16000, 1, 16000));

    WavHeaderInfo info;
    ADR_CHECK(inspectPcm16WavHeader(&sink.bytes[0], sink.bytes.size(), 16000, 1, 32000, info) ==
              WavHeaderCheck::Ok);
    ADR_CHECK_EQ(info.sampleRateHz, 16000U);
    ADR_CHECK_EQ(info.channels, 1);
    ADR_CHECK_EQ(info.bitsPerSample, 16);
    ADR_CHECK_EQ(info.dataBytes, 32000ULL);

    ADR_CHECK(inspectPcm16WavHeader(&sink.bytes[0], 10, 16000, 1, 32000, info) ==
              WavHeaderCheck::TooShort);
    ADR_CHECK(inspectPcm16WavHeader(nullptr, 44, 16000, 1, 32000, info) ==
              WavHeaderCheck::TooShort);
    ADR_CHECK(inspectPcm16WavHeader(&sink.bytes[0], 44, 44100, 1, 32000, info) ==
              WavHeaderCheck::RateMismatch);
    ADR_CHECK(inspectPcm16WavHeader(&sink.bytes[0], 44, 16000, 2, 32000, info) ==
              WavHeaderCheck::ChannelMismatch);
    ADR_CHECK(inspectPcm16WavHeader(&sink.bytes[0], 44, 16000, 1, 99, info) ==
              WavHeaderCheck::SizeMismatch);

    /* Corrupt the RIFF magic. */
    std::vector<uint8_t> corrupted = sink.bytes;
    corrupted[0] = 'X';
    ADR_CHECK(inspectPcm16WavHeader(&corrupted[0], corrupted.size(), 16000, 1, 32000, info) ==
              WavHeaderCheck::BadMagic);

    /* A header that claims a different bit depth. */
    corrupted = sink.bytes;
    corrupted[34] = 24;
    ADR_CHECK(inspectPcm16WavHeader(&corrupted[0], corrupted.size(), 16000, 1, 32000, info) ==
              WavHeaderCheck::BadFormat);
}

void testByteArrayPrint() {
    uint8_t buffer[8] = {0};
    ByteArrayPrint sink(buffer, sizeof(buffer));

    ADR_CHECK_EQ(sink.write(reinterpret_cast<const uint8_t *>("abcd"), 4), static_cast<size_t>(4));
    ADR_CHECK(!sink.lastWriteShort());
    ADR_CHECK_EQ(sink.written(), static_cast<size_t>(4));

    /* Overrunning the buffer must report the shortfall, not hide it. */
    ADR_CHECK_EQ(sink.write(reinterpret_cast<const uint8_t *>("efghij"), 6), static_cast<size_t>(4));
    ADR_CHECK(sink.lastWriteShort());
    ADR_CHECK_EQ(sink.written(), static_cast<size_t>(8));
    ADR_CHECK(memcmp(buffer, "abcdefgh", 8) == 0);
}

/* -------------------------------------------------------------- labels */

void testLabelValidation() {
    ADR_CHECK(isValidLabel("hey_hotpin"));
    ADR_CHECK(isValidLabel("noise"));
    ADR_CHECK(isValidLabel("unknown"));
    ADR_CHECK(isValidLabel("A-Z_09"));
    ADR_CHECK(isValidLabel("a"));

    ADR_CHECK(!isValidLabel(nullptr));
    ADR_CHECK(!isValidLabel(""));
    ADR_CHECK(!isValidLabel("has space"));
    ADR_CHECK(!isValidLabel("../etc"));
    ADR_CHECK(!isValidLabel("a/b"));
    ADR_CHECK(!isValidLabel("a\\b"));
    ADR_CHECK(!isValidLabel("a.b"));

    /* Exactly at the length limit. */
    const char *maxLabel = "abcdefghijklmnopqrstuvwx";  /* 24 */
    ADR_CHECK_EQ(strlen(maxLabel), kMaxLabelLength);
    ADR_CHECK(isValidLabel(maxLabel));
    ADR_CHECK(!isValidLabel("abcdefghijklmnopqrstuvwxy"));  /* 25 */

    /* Traversal attempts must be impossible to express. */
    const char *attacks[] = {"..", "../sd", "/etc/passwd", "./x", "%2e%2e"};
    for (size_t i = 0; i < sizeof(attacks) / sizeof(attacks[0]); ++i) {
        ADR_CHECK(!isValidLabel(attacks[i]));
    }
}

void testLabelSanitize() {
    char out[kMaxLabelLength + 1];

    ADR_CHECK_EQ(sanitizeLabel("Hey Hotpin!", out, sizeof(out)), 11ULL);
    ADR_CHECK(strcmp(out, "hey_hotpin_") == 0);

    ADR_CHECK_EQ(sanitizeLabel("../etc", out, sizeof(out)), 6ULL);
    ADR_CHECK(strcmp(out, "___etc") == 0);

    /* Truncates to the label limit. */
    const char *longInput = "abcdefghijklmnopqrstuvwxyz0123456789";
    ADR_CHECK_EQ(sanitizeLabel(longInput, out, sizeof(out)), static_cast<size_t>(kMaxLabelLength));
    ADR_CHECK(isValidLabel(out));

    ADR_CHECK_EQ(sanitizeLabel(nullptr, out, sizeof(out)), 0ULL);
    ADR_CHECK_EQ(sanitizeLabel("", out, sizeof(out)), 0ULL);
}

void testClipPathBuilding() {
    char path[kMaxClipPathLength];

    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 1), 14ULL);
    ADR_CHECK(strcmp(path, "/noise.001.wav") == 0);

    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 999), 14ULL);
    ADR_CHECK(strcmp(path, "/noise.999.wav") == 0);

    /* Four-digit indices are legal past 999 and must not corrupt the name. */
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 1000), 15ULL);
    ADR_CHECK(strcmp(path, "/noise.1000.wav") == 0);
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 9999), 15ULL);
    ADR_CHECK(strcmp(path, "/noise.9999.wav") == 0);
    /* Every byte of the numeric part must be a digit. */
    for (const char *p = path + 6; *p != '\0'; ++p) {
        ADR_CHECK(*p == '.' || (*p >= '0' && *p <= '9') || *p == 'w' || *p == 'a' ||
                  *p == 'v');
    }

    /* Index 0 and anything past the maximum are refused. */
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 0), 0ULL);
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 10000), 0ULL);
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "bad label", 1), 0ULL);
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "../etc", 1), 0ULL);

    /* Too small a buffer must fail rather than truncate a path. */
    char tiny[8];
    ADR_CHECK_EQ(buildClipPath(tiny, sizeof(tiny), "/", "noise", 1), 0ULL);

    /* Trailing slashes in the directory are collapsed. */
    ADR_CHECK(buildClipPath(path, sizeof(path), "/clips//", "noise", 2) > 0);
    ADR_CHECK(strcmp(path, "/clips/noise.002.wav") == 0);
}

void testClipNameParsing() {
    char label[kMaxLabelLength + 1];
    uint32_t index = 0;

    ADR_CHECK(parseClipName("noise.001.wav", label, sizeof(label), index));
    ADR_CHECK_EQ(index, 1U);
    ADR_CHECK(strcmp(label, "noise") == 0);

    ADR_CHECK(parseClipName("hey_hotpin.042.wav", label, sizeof(label), index));
    ADR_CHECK_EQ(index, 42U);
    ADR_CHECK(strcmp(label, "hey_hotpin") == 0);

    ADR_CHECK(parseClipName("a.999.wav", label, sizeof(label), index));
    ADR_CHECK_EQ(index, 999U);
    ADR_CHECK(strcmp(label, "a") == 0);

    /* "noise.999.wav" is 14 characters: '/', 5 label chars, '.', 3 digits, ".wav". */
    char path[kMaxClipPathLength];
    ADR_CHECK_EQ(buildClipPath(path, sizeof(path), "/", "noise", 999), 14ULL);

    /* The label must be a standalone NUL-terminated string, not a view into
     * the filename. */
    ADR_CHECK(parseClipName("noise.007.wav", label, sizeof(label), index));
    ADR_CHECK_EQ(strlen(label), static_cast<size_t>(5));
    ADR_CHECK(strcmp(label, "noise") == 0);

    /* Rejections: wrong extension, wrong digit count, bad label, zero index. */
    ADR_CHECK(!parseClipName("noise.001.wav.bak", label, sizeof(label), index));
    ADR_CHECK(!parseClipName("noise.01.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName("noise.0001.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName("noise.000.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName("noise.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName("no i se.001.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName("noise.abc.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName(".001.wav", label, sizeof(label), index));
    ADR_CHECK(!parseClipName(nullptr, label, sizeof(label), index));
    ADR_CHECK(!parseClipName("", label, sizeof(label), index));

    /* A four-digit index such as 1000 cannot be expressed by the name format. */
    ADR_CHECK(parseClipName("noise.1000.wav", label, sizeof(label), index));
    ADR_CHECK_EQ(index, 1000U);
    ADR_CHECK(parseClipName("noise.9999.wav", label, sizeof(label), index));
    ADR_CHECK_EQ(index, 9999U);
    /* Five digits exceed the supported range. */
    ADR_CHECK(!parseClipName("noise.10000.wav", label, sizeof(label), index));

    /* A label buffer that is too small must fail, never overrun. */
    char tiny[4];
    ADR_CHECK(!parseClipName("noise.001.wav", tiny, sizeof(tiny), index));
}

void testDefaultLabels() {
    ADR_CHECK(isDefaultLabel("hey_hotpin"));
    ADR_CHECK(isDefaultLabel("noise"));
    ADR_CHECK(isDefaultLabel("unknown"));
    ADR_CHECK(!isDefaultLabel("custom"));
    ADR_CHECK(!isDefaultLabel("bad label"));
}

}  // namespace

void runCoreTests() {
    testAudioStats();
    testPcmConversion();
    testAttenuation();
    testWavHeaderFields();
    testWavHeaderStereo();
    testWavHeaderRejections();
    testWavSizeHelpers();
    testWavShortWriteIsReported();
    testWavHeaderInspection();
    testByteArrayPrint();
    testLabelValidation();
    testLabelSanitize();
    testClipPathBuilding();
    testClipNameParsing();
    testDefaultLabels();
}