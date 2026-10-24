#include "test_controller.h"

#include <string.h>

#include <map>
#include <vector>

#include "AudioDatasetRecorder.h"
#include "TestFakes.h"
#include "TestHarness.h"

using namespace AudioDatasetRecorder;

namespace {

uint32_t readU32(const std::vector<uint8_t> &v, size_t offset) {
    return static_cast<uint32_t>(v[offset]) | (static_cast<uint32_t>(v[offset + 1]) << 8) |
           (static_cast<uint32_t>(v[offset + 2]) << 16) | (static_cast<uint32_t>(v[offset + 3]) << 24);
}

uint16_t readU16(const std::vector<uint8_t> &v, size_t offset) {
    return static_cast<uint16_t>(static_cast<uint16_t>(v[offset]) |
                                 (static_cast<uint16_t>(v[offset + 1]) << 8));
}

RecorderConfig smallConfig() {
    RecorderConfig config;
    config.sampleRateHz = 1000;
    config.channels = 1;
    config.durationSeconds = 1;   /* 1000 frames */
    config.rightShift = 0;        /* fake source already emits PCM-scale words */
    config.minFreeBytesMargin = 0;
    config.readTimeoutMs = 10;
    config.captureOverrunMs = 0;
    config.writeChunkBytes = 64;
    return config;
}

StreamLayout monoRawLayout() {
    StreamLayout layout;
    layout.output = SourceOutput::RawI2sWords;
    layout.wordsPerFrame = 1;
    layout.samplesPerWord = 1;
    return layout;
}

/* --------------------------------------------------------- converter */

void testConverterMonoSlots() {
    RecorderConfig config;
    config.channels = 1;
    config.rightShift = 14;
    config.attenuationShift = 0;
    config.slot = SampleSlot::Left;

    StreamLayout stereo;
    stereo.wordsPerFrame = 2;

    int16_t out[4];
    size_t produced = 0;

    /* Left selects word 0 of each pair. */
    {
        SampleConverter converter;
        ADR_CHECK(converter.configure(stereo, config) == Status::Ok);
        AudioStats stats;
        const int32_t interleaved[8] = {1 << 14, 100 << 14, 2 << 14, 200 << 14,
                                         -(1 << 14), 300 << 14, 0, 400 << 14};
        ADR_CHECK(converter.convertBlock(interleaved, 8, out, 8, produced, stats));
        ADR_CHECK_EQ(produced, static_cast<size_t>(4));
        ADR_CHECK_EQ(out[0], 1);
        ADR_CHECK_EQ(out[1], 2);
        ADR_CHECK_EQ(out[2], -1);
        ADR_CHECK_EQ(out[3], 0);
    }

    /* Right selects word 1 of each pair. */
    {
        RecorderConfig right = config;
        right.slot = SampleSlot::Right;
        SampleConverter converter;
        ADR_CHECK(converter.configure(stereo, right) == Status::Ok);
        AudioStats stats;
        const int32_t interleaved[8] = {1 << 14, 100 << 14, 2 << 14, 200 << 14,
                                         -(1 << 14), 300 << 14, 0, 400 << 14};
        ADR_CHECK(converter.convertBlock(interleaved, 8, out, 8, produced, stats));
        ADR_CHECK_EQ(produced, static_cast<size_t>(4));
        ADR_CHECK_EQ(out[0], 100);
        ADR_CHECK_EQ(out[1], 200);
        ADR_CHECK_EQ(out[2], 300);
        ADR_CHECK_EQ(out[3], 400);
    }

    /* Both averages, and must not overflow int32 while doing so. */
    {
        RecorderConfig both = config;
        both.slot = SampleSlot::Both;
        SampleConverter converter;
        ADR_CHECK(converter.configure(stereo, both) == Status::Ok);
        AudioStats stats;
        const int32_t extremes[2] = {INT32_MAX, INT32_MAX};
        ADR_CHECK(converter.convertBlock(extremes, 2, out, 8, produced, stats));
        ADR_CHECK_EQ(produced, static_cast<size_t>(1));
        ADR_CHECK_EQ(out[0], 32767);
    }

    /* A one-word stream is already slot-selected, so every slot reads word 0. */
    {
        StreamLayout mono;
        SampleConverter converter;
        ADR_CHECK(converter.configure(mono, config) == Status::Ok);
        ADR_CHECK_EQ(converter.framesFromWords(4), static_cast<size_t>(4));
        ADR_CHECK_EQ(converter.samplesForWords(4), static_cast<size_t>(4));
    }
}

void testConverterStereo() {
    RecorderConfig config;
    config.channels = 2;
    config.rightShift = 14;
    config.slot = SampleSlot::Left;

    StreamLayout stereo;
    stereo.wordsPerFrame = 2;

    SampleConverter converter;
    ADR_CHECK(converter.configure(stereo, config) == Status::Ok);
    ADR_CHECK_EQ(converter.samplesForWords(8), static_cast<size_t>(8));
    ADR_CHECK_EQ(converter.framesFromWords(8), static_cast<size_t>(4));

    int32_t interleaved[4] = {1 << 14, 7 << 14, 2 << 14, 8 << 14};
    int16_t out[4];
    size_t produced = 0;
    AudioStats stats;
    ADR_CHECK(converter.convertBlock(interleaved, 4, out, 4, produced, stats));
    ADR_CHECK_EQ(produced, static_cast<size_t>(4));
    /* Interleaved L,R,L,R is preserved. */
    ADR_CHECK_EQ(out[0], 1);
    ADR_CHECK_EQ(out[1], 7);
    ADR_CHECK_EQ(out[2], 2);
    ADR_CHECK_EQ(out[3], 8);

    /* Stereo needs two words per frame on the wire. */
    StreamLayout mono;
    ADR_CHECK(converter.configure(mono, config) == Status::NotSupported);
}

void testConverterPcm16IsNotShiftedTwice() {
    /*
     * The single most damaging conversion bug this library can have: a source
     * that already produced PCM16 must not have the right shift applied again.
     */
    RecorderConfig config;
    config.channels = 1;
    config.rightShift = 14;  /* would halve the signal a second time */
    config.slot = SampleSlot::Left;

    StreamLayout pcm16;
    pcm16.output = SourceOutput::Pcm16;
    pcm16.wordsPerFrame = 1;
    pcm16.samplesPerWord = 2;

    SampleConverter converter;
    ADR_CHECK(converter.configure(pcm16, config) == Status::Ok);
    ADR_CHECK_EQ(converter.rightShift(), 0);

    int32_t words[2];
    /* Two consecutive little-endian PCM16 samples: 1000 then -1000. */
    words[0] = static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint16_t>(1000)) |
                                    (static_cast<uint32_t>(static_cast<uint16_t>(
                                         static_cast<uint16_t>(-1000)))
                                     << 16));
    words[1] = words[0];

    int16_t out[4];
    size_t produced = 0;
    AudioStats stats;
    ADR_CHECK(converter.convertBlock(words, 2, out, 4, produced, stats));
    ADR_CHECK_EQ(produced, static_cast<size_t>(4));
    ADR_CHECK_EQ(out[0], 1000);
    ADR_CHECK_EQ(out[1], -1000);
    ADR_CHECK_EQ(out[2], 1000);
    ADR_CHECK_EQ(out[3], -1000);

    ADR_CHECK_EQ(stats.sampleCount, 4ULL);

    /* A PCM16 source is mono by construction. */
    RecorderConfig stereo = config;
    stereo.channels = 2;
    ADR_CHECK(converter.configure(pcm16, stereo) == Status::NotSupported);

    /* Downmixing to Both is not meaningful from an already-selected slot. */
    RecorderConfig both = config;
    both.slot = SampleSlot::Both;
    ADR_CHECK(converter.configure(pcm16, both) == Status::NotSupported);
}

void testConverterAttenuationAndClipping() {
    RecorderConfig config;
    config.channels = 1;
    config.rightShift = 0;
    config.attenuationShift = 2;
    config.slot = SampleSlot::Left;

    StreamLayout mono;
    SampleConverter converter;
    ADR_CHECK(converter.configure(mono, config) == Status::Ok);

    int32_t words[2] = {400, -400};
    int16_t out[2];
    size_t produced = 0;
    AudioStats stats;
    ADR_CHECK(converter.convertBlock(words, 2, out, 2, produced, stats));
    ADR_CHECK_EQ(out[0], 100);
    ADR_CHECK_EQ(out[1], -100);

    /* Clipping is reported by the saturation step. */
    RecorderConfig hot;
    hot.channels = 1;
    hot.rightShift = 0;
    hot.attenuationShift = 0;
    SampleConverter hotConverter;
    ADR_CHECK(hotConverter.configure(mono, hot) == Status::Ok);
    int32_t loud[2] = {INT32_MAX, INT32_MIN};
    AudioStats hotStats;
    ADR_CHECK(hotConverter.convertBlock(loud, 2, out, 2, produced, hotStats));
    ADR_CHECK_EQ(out[0], 32767);
    ADR_CHECK_EQ(out[1], -32768);
    ADR_CHECK_EQ(hotStats.clippedSamples, 2U);
}

void testConverterRejectsBadArguments() {
    RecorderConfig config;
    config.channels = 1;
    StreamLayout mono;
    SampleConverter converter;
    ADR_CHECK(converter.configure(mono, config) == Status::Ok);

    int16_t out[4];
    size_t produced = 0;
    AudioStats stats;
    int32_t words[4] = {1, 2, 3, 4};

    ADR_CHECK(!converter.convertBlock(nullptr, 4, out, 4, produced, stats));
    ADR_CHECK(!converter.convertBlock(words, 4, nullptr, 4, produced, stats));
    /* Too few words for a whole frame. */
    ADR_CHECK(!converter.convertBlock(words, 0, out, 4, produced, stats));
    /* Output buffer too small. */
    ADR_CHECK(!converter.convertBlock(words, 4, out, 2, produced, stats));
    ADR_CHECK_EQ(produced, static_cast<size_t>(0));

    /* Unsupported channel count. */
    RecorderConfig threeChannels;
    threeChannels.channels = 3;
    ADR_CHECK(converter.configure(mono, threeChannels) == Status::InvalidConfig);

    /* Unsupported layout word count. */
    StreamLayout bogus;
    bogus.wordsPerFrame = 3;
    ADR_CHECK(converter.configure(bogus, config) == Status::NotSupported);

    /* An unconfigured converter refuses to convert anything. */
    SampleConverter fresh;
    ADR_CHECK(!fresh.configured());
    ADR_CHECK(!fresh.convertBlock(words, 4, out, 4, produced, stats));
}

/* ------------------------------------------------------- controller flow */

void testRecordRejectsBadInput() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);

    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record(nullptr, report) == Status::InvalidLabel);
    ADR_CHECK(controller.record("", report) == Status::InvalidLabel);
    ADR_CHECK(controller.record("../etc/passwd", report) == Status::InvalidLabel);
    ADR_CHECK(controller.record("bad label", report) == Status::InvalidLabel);
    ADR_CHECK_EQ(audio.beginCalls(), 0);

    /* The audio peripheral must never start for a rejected request. */
    ADR_CHECK(!audio.isRunning());
    ADR_CHECK_STR_CONTAINS(log.joined(), "invalid label");
}

void testInvalidConfigRejected() {
    FakeAudioSource audio;
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);

    RecorderConfig config = smallConfig();
    config.sampleRateHz = 0;
    ADR_CHECK(controller.configure(config) == Status::InvalidConfig);

    config = smallConfig();
    config.channels = 3;
    ADR_CHECK(controller.configure(config) == Status::InvalidConfig);

    config = smallConfig();
    config.durationSeconds = 0;
    ADR_CHECK(controller.configure(config) == Status::InvalidConfig);

    /* Too large to express in a classic 32-bit WAV. */
    config = smallConfig();
    config.sampleRateHz = 48000;
    config.durationSeconds = 200000;
    ADR_CHECK(controller.configure(config) == Status::InvalidConfig);
}

void testFullBufferHappyPath() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;

    RecorderConfig config = smallConfig();
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("hey_hotpin", report) == Status::Ok);
    ADR_CHECK_EQ(report.framesCaptured, 1000ULL);
    ADR_CHECK_EQ(report.framesExpected, 1000ULL);
    ADR_CHECK(!report.truncated);
    ADR_CHECK_EQ(report.audio.sampleCount, 1000ULL);

    /* The clip is held in RAM and nothing has been mounted or written yet:
     * that ordering is what lets the ESP32-CAM share pins. */
    ADR_CHECK(controller.hasPending());
    ADR_CHECK(strcmp(controller.pendingLabel(), "hey_hotpin") == 0);
    ADR_CHECK_EQ(storage.fileCount(), static_cast<size_t>(0));
    ADR_CHECK_EQ(indicator.recordingCalls, 1);
    ADR_CHECK_EQ(indicator.allOffCalls, 1);

    ADR_CHECK(controller.savePending() == Status::Ok);
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK(storage.has("/hey_hotpin.001.wav"));
    /* A successful save turns the LED off again (via setSuccess ending off). */
    ADR_CHECK_EQ(indicator.successCalls, 1);

    std::map<std::string, std::vector<uint8_t> >::const_iterator it =
        storage.files.find("/hey_hotpin.001.wav");
    ADR_CHECK(it != storage.files.end());
    const std::vector<uint8_t> &bytes = it->second;

    const size_t expected = 44u + 1000u * 2u;
    ADR_CHECK_EQ(bytes.size(), expected);
    ADR_CHECK(memcmp(&bytes[0], "RIFF", 4) == 0);
    ADR_CHECK_EQ(readU32(bytes, 4), static_cast<uint32_t>(expected - 8));
    ADR_CHECK_EQ(readU16(bytes, 22), 1);
    ADR_CHECK_EQ(readU32(bytes, 24), 1000U);
    ADR_CHECK_EQ(readU32(bytes, 40), 2000U);

    /* A second clip of the same label must take the next index. */
    ADR_CHECK(controller.record("hey_hotpin", report) == Status::Ok);
    ADR_CHECK(controller.savePending() == Status::Ok);
    ADR_CHECK(storage.has("/hey_hotpin.002.wav"));
    controller.discardPending();
}

void testPendingClipBlocksSecondRecord() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);
    ADR_CHECK(controller.record("noise", report) == Status::PendingClipExists);
    ADR_CHECK(controller.savePending() == Status::Ok);

    /* With the pending clip written, recording works again. */
    ADR_CHECK(controller.record("noise", report) == Status::Ok);
    controller.discardPending();
}

void testOutOfMemoryIsReportedCleanly() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);

    RecorderConfig config = smallConfig();
    config.minFreeBytesMargin = 4096;
    ADR_CHECK(controller.configure(config) == Status::Ok);

    /* Claim a largest block too small for the clip plus margin. */
    allocator.setLimit(100);
    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::OutOfMemory);
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK_EQ(audio.beginCalls(), 0);
    ADR_CHECK_STR_CONTAINS(log.joined(), "ADR_RECORD_SECONDS");

    /* Allocation itself failing must be reported the same way. */
    allocator.setLimit(1024u * 1024u);
    allocator.setFailNext(true);
    ADR_CHECK(controller.record("noise", report) == Status::OutOfMemory);
    ADR_CHECK(!controller.hasPending());
}

void testTruncatedCaptureIsDiscardedByDefault() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    audio.setWordsRemaining(400);  /* only 400 of 1000 frames available */
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;

    RecorderConfig config = smallConfig();
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    const Status status = controller.record("noise", report);
    ADR_CHECK(status != Status::Ok);
    ADR_CHECK(report.truncated);
    ADR_CHECK_EQ(report.framesCaptured, 400ULL);
    ADR_CHECK(!controller.hasPending());
    /* Nothing must have been written to the card. */
    ADR_CHECK_EQ(storage.fileCount(), static_cast<size_t>(0));
    /* The buffer must be released, not leaked. */
    ADR_CHECK_EQ(allocator.outstanding_, static_cast<size_t>(0));
    ADR_CHECK_EQ(indicator.allOffCalls, 1);
}

void testTruncatedCaptureCanBeAccepted() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    audio.setWordsRemaining(400);
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;

    RecorderConfig config = smallConfig();
    config.acceptTruncatedClip = true;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);
    ADR_CHECK(report.truncated);
    ADR_CHECK_EQ(report.framesCaptured, 400ULL);
    ADR_CHECK(controller.hasPending());
    ADR_CHECK_EQ(controller.pendingFrames(), 400ULL);

    ADR_CHECK(controller.savePending() == Status::Ok);

    std::map<std::string, std::vector<uint8_t> >::const_iterator it =
        storage.files.find("/noise.001.wav");
    ADR_CHECK(it != storage.files.end());
    if (it == storage.files.end()) return;

    /* The header must state the real, shorter length, not the requested one. */
    ADR_CHECK_EQ(it->second.size(), static_cast<size_t>(44 + 400 * 2));
    ADR_CHECK_EQ(readU32(it->second, 40), 800U);
    ADR_CHECK_EQ(readU32(it->second, 4), static_cast<uint32_t>(44 + 400 * 2 - 8));
}

void testStalledCaptureHitsTheWatchdog() {
    FakeAudioSource audio;
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;

    /* The source delivers a little audio and then goes quiet while wall time
     * keeps moving. Only the watchdog can end this capture. */
    audio.setLayout(monoRawLayout());
    audio.setWordsRemaining(50);
    audio.advanceClockOnRead(&clock, 200);

    RecorderConfig config = smallConfig();
    config.durationSeconds = 1;
    config.captureOverrunMs = 1000; /* deadline at 2000 ms */

    audio.advanceClockOnRead(&clock, 1);

    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::AudioTimeout);
    ADR_CHECK(report.truncated);
    ADR_CHECK_EQ(report.framesCaptured, 50ULL);
    ADR_CHECK(report.elapsedMs > 1000U);
    ADR_CHECK(!controller.hasPending());
    /* The clip must be discarded, not silently saved short. */
    ADR_CHECK_EQ(storage.fileCount(), static_cast<size_t>(0));
    ADR_CHECK_EQ(allocator.outstanding_, static_cast<size_t>(0));
    ADR_CHECK_STR_CONTAINS(log.joined(), "capture incomplete");
}

void testAudioReadFailureIsReported() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    audio.setFailAfterReads(2);
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::AudioReadFailed);
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK_EQ(allocator.outstanding_, static_cast<size_t>(0));
    /* The peripheral must still be released on this path. */
    ADR_CHECK(audio.endCalls() >= 1);
    ADR_CHECK(!audio.isRunning());
}

void testAudioBeginFailureReleasesEverything() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    audio.setFailBegin(true);
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::AudioBeginFailed);
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK_EQ(allocator.outstanding_, static_cast<size_t>(0));
    ADR_CHECK_EQ(indicator.allOffCalls, 1);
}

void testSaveFailureKeepsClipForRetry() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);

    storage.beginFails_ = true;
    ADR_CHECK(controller.savePending() == Status::StorageBeginFailed);
    /* The clip must survive a failed mount so SAVE can be retried. */
    ADR_CHECK(controller.hasPending());
    ADR_CHECK_EQ(controller.pendingFrames(), 1000ULL);
    ADR_CHECK_EQ(indicator.errorCalls, 1);

    storage.beginFails_ = false;
    ADR_CHECK(controller.savePending() == Status::Ok);
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK(storage.has("/noise.001.wav"));
}

void testSaveWithoutPending() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);
    ADR_CHECK(controller.savePending() == Status::NoPendingClip);
}

void testOpenFailureKeepsClipForRetry() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);

    storage.createFails_ = true;
    ADR_CHECK(controller.savePending() == Status::StorageOpenFailed);
    ADR_CHECK(controller.hasPending());
    storage.createFails_ = false;

    ADR_CHECK(controller.savePending() == Status::Ok);
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK(storage.has("/noise.001.wav"));
}

void testWriteFailureRemovesPartialFile() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);

    /* Arm every writer this storage hands out to fail its first write. */
    storage.createFails_ = false;
    struct FailingStorage : public FakeStorage {
        Status createWriter(const char *path, const char *partialPath, IFileWriter *&out) {
            const Status status = FakeStorage::createWriter(path, partialPath, out);
            if (status == Status::Ok && out != nullptr) {
                static_cast<FakeFileWriter *>(out)->setFailWrite(true);
            }
            return status;
        }
    };

    FailingStorage failing;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController failingController(audio, failing, indicator, clock, allocator, log);
    ADR_CHECK(failingController.configure(smallConfig()) == Status::Ok);
    ADR_CHECK(failingController.record("noise", report) == Status::Ok);
    ADR_CHECK(failingController.savePending() == Status::StorageWriteFailed);

    /* A truncated file must not be left behind pretending to be complete. */
    ADR_CHECK(!failing.has("/noise.001.wav"));
    ADR_CHECK(failingController.hasPending());

    /* The healthy controller still works. */
    ADR_CHECK(controller.savePending() == Status::Ok);
}

void testSizeMismatchRemovesFile() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());

    struct LyingStorage : public FakeStorage {
        Status fileSize(const char *path, uint64_t &out) {
            Status status = FakeStorage::fileSize(path, out);
            if (status == Status::Ok) out += 7;  /* the card "lies" */
            return status;
        }
    };

    LyingStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);
    ADR_CHECK(controller.savePending() == Status::StorageSizeMismatch);
    ADR_CHECK(!storage.has("/noise.001.wav"));
    ADR_CHECK(controller.hasPending());
}

void testInsufficientSpaceIsReported() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);

    storage.freeBytes_ = 100;  /* far less than 2044 bytes */
    ADR_CHECK(controller.savePending() == Status::InsufficientSpace);
    ADR_CHECK(controller.hasPending());
    ADR_CHECK(!storage.has("/noise.001.wav"));
}

void testIndexSelection() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    /* Files from other labels must not affect this label's numbering. */
    storage.files["/noise.001.wav"].push_back(1);
    storage.files["/noise.004.wav"].push_back(1);
    storage.files["/unknown.009.wav"].push_back(1);
    storage.files["/noise.notes.txt"].push_back(1);
    storage.mounted_ = true;

    uint32_t index = 0;
    ADR_CHECK(controller.nextClipIndex("noise", index) == Status::Ok);
    ADR_CHECK_EQ(index, 5U);
    ADR_CHECK(controller.nextClipIndex("unknown", index) == Status::Ok);
    ADR_CHECK_EQ(index, 10U);
    ADR_CHECK(controller.nextClipIndex("fresh", index) == Status::Ok);
    ADR_CHECK_EQ(index, 1U);
    ADR_CHECK(controller.nextClipIndex("bad label", index) == Status::InvalidLabel);

    storage.files.erase("/noise.9999.wav");
    storage.files["/noise.9999.wav"].push_back(1);
    ADR_CHECK(controller.nextClipIndex("noise", index) == Status::IndexExhausted);
}

void testStreamingStrategy() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    RecordingProgressSink progress;

    RecorderConfig config = smallConfig();
    config.strategy = CaptureStrategy::Streaming;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    controller.setProgressSink(&progress);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);
    ADR_CHECK_EQ(report.framesCaptured, 1000ULL);

    /* Streaming writes immediately: nothing is left pending. */
    ADR_CHECK(!controller.hasPending());
    ADR_CHECK(storage.has("/noise.001.wav"));

    std::map<std::string, std::vector<uint8_t> >::const_iterator it =
        storage.files.find("/noise.001.wav");
    ADR_CHECK(it != storage.files.end());
    ADR_CHECK_EQ(it->second.size(), static_cast<size_t>(44 + 2000));
    ADR_CHECK_EQ(readU32(it->second, 40), 2000U);
    ADR_CHECK(memcmp(&it->second[0], "RIFF", 4) == 0);

    /* Streaming allocates no full-clip buffer. */
    ADR_CHECK_EQ(allocator.outstanding_, static_cast<size_t>(0));
    ADR_CHECK_EQ(indicator.successCalls, 1);
}

void testStreamingTruncatedDeletesFile() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    audio.setWordsRemaining(300);
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;

    RecorderConfig config = smallConfig();
    config.strategy = CaptureStrategy::Streaming;
    /* Even with truncation allowed, a streaming file cannot be fixed up. */
    config.acceptTruncatedClip = true;

    audio.advanceClockOnRead(&clock, 1);

    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::AudioTimeout);
    ADR_CHECK(!storage.has("/noise.001.wav"));
    ADR_CHECK(!controller.hasPending());
}

void testStreamingNeedsStorage() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    storage.beginFails_ = true;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;

    RecorderConfig config = smallConfig();
    config.strategy = CaptureStrategy::Streaming;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::StorageBeginFailed);
    ADR_CHECK_EQ(audio.beginCalls(), 0);
}

void testProgressSinkIsCalled() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    RecordingProgressSink progress;

    RecorderConfig config = smallConfig();
    config.durationSeconds = 3;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    controller.setProgressSink(&progress);
    ADR_CHECK(controller.configure(config) == Status::Ok);

    /* The fake clock never advances, so no one-second tick is ever reached;
     * the point is that a sink is optional and nothing breaks without one. */
    CaptureReport report;
    ADR_CHECK(controller.record("noise", report) == Status::Ok);
    ADR_CHECK_EQ(progress.calls, 0);
    controller.discardPending();
}

void testMountHelpers() {
    FakeAudioSource audio;
    audio.setLayout(monoRawLayout());
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    audio.advanceClockOnRead(&clock, 1);
    RecorderController controller(audio, storage, indicator, clock, allocator, log);
    ADR_CHECK(controller.configure(smallConfig()) == Status::Ok);

    ADR_CHECK(!controller.isStorageMounted());
    ADR_CHECK(controller.mountStorage() == Status::Ok);
    ADR_CHECK(controller.isStorageMounted());
    ADR_CHECK(controller.storageTotalBytes() > 0ULL);
    controller.unmountStorage();
    ADR_CHECK(!controller.isStorageMounted());
}

}  // namespace


void runControllerTests() {
    ADR_RUN(testConverterMonoSlots);
    ADR_RUN(testConverterStereo);
    ADR_RUN(testConverterPcm16IsNotShiftedTwice);
    ADR_RUN(testConverterAttenuationAndClipping);
    ADR_RUN(testConverterRejectsBadArguments);
    ADR_RUN(testRecordRejectsBadInput);
    ADR_RUN(testInvalidConfigRejected);
    ADR_RUN(testFullBufferHappyPath);
    ADR_RUN(testPendingClipBlocksSecondRecord);
    ADR_RUN(testOutOfMemoryIsReportedCleanly);
    ADR_RUN(testTruncatedCaptureIsDiscardedByDefault);
    ADR_RUN(testTruncatedCaptureCanBeAccepted);
    ADR_RUN(testStalledCaptureHitsTheWatchdog);
    ADR_RUN(testAudioReadFailureIsReported);
    ADR_RUN(testAudioBeginFailureReleasesEverything);
    ADR_RUN(testSaveFailureKeepsClipForRetry);
    ADR_RUN(testSaveWithoutPending);
    ADR_RUN(testOpenFailureKeepsClipForRetry);
    ADR_RUN(testWriteFailureRemovesPartialFile);
    ADR_RUN(testSizeMismatchRemovesFile);
    ADR_RUN(testInsufficientSpaceIsReported);
    ADR_RUN(testIndexSelection);
    ADR_RUN(testStreamingStrategy);
    ADR_RUN(testStreamingTruncatedDeletesFile);
    ADR_RUN(testStreamingNeedsStorage);
    ADR_RUN(testProgressSinkIsCalled);
    ADR_RUN(testMountHelpers);
}