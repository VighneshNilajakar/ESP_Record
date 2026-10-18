#ifndef AUDIO_DATASET_RECORDER_RECORDER_CONTROLLER_H
#define AUDIO_DATASET_RECORDER_RECORDER_CONTROLLER_H

#include <stdint.h>
#include <stddef.h>

#include "IAudioSource.h"
#include "IIndicator.h"
#include "IStorageBackend.h"
#include "Labels.h"
#include "PcmConvert.h"
#include "RecorderTypes.h"
#include "Services.h"
#include "WavWriter.h"

namespace AudioDatasetRecorder {

/*
 * Optional once-per-second feedback during a long capture. Kept as a hook
 * rather than printing directly so the core stays free of any transport and
 * remains testable.
 */
class IProgressSink {
public:
    virtual ~IProgressSink() {}
    virtual void onCaptureProgress(uint32_t elapsedMs,
                                   uint64_t framesCaptured,
                                   uint64_t framesExpected) = 0;
};

/*
 * Board-agnostic recorder.
 *
 * The controller owns the sequencing that every board profile repeats
 * identically -- validate the label, size the buffer, run the capture with a
 * stall watchdog, always tear the peripheral down, accumulate statistics, and
 * save with byte-exact verification -- while adapters own everything that
 * actually differs between chips.
 *
 * Typical use:
 *
 *     RecorderController controller(audio, storage, indicator, clock, memory, log);
 *     controller.configure(config);
 *     CaptureReport report;
 *     if (controller.record("hey_hotpin", report) == Status::Ok) {
 *         Status saved = controller.savePending();
 *     }
 *
 * CaptureStrategy::FullBuffer holds the clip in RAM after record() and writes it
 * from savePending(). That ordering is what makes boards whose microphone pins
 * are shared with the storage bus usable: the audio peripheral is released
 * before the card is mounted. A reset or power loss between the two calls loses
 * the clip, because RAM does not survive it. That is inherent to the strategy,
 * not a defect.
 *
 * CaptureStrategy::Streaming writes as it captures, keeping only one DMA block
 * in RAM, and needs the microphone pins and the storage bus to be disjoint. The
 * WAV header is written before the payload because a streaming writer cannot
 * seek backwards, so a short capture is reported as a failure and the partial
 * file is deleted rather than left with a header that overstates its length.
 *
 * Not thread-safe: call it from one context only.
 */
class RecorderController {
public:
    RecorderController(IAudioSource &audio,
                       IStorageBackend &storage,
                       IIndicator &indicator,
                       IClock &clock,
                       IAllocator &allocator,
                       ILogger &log);

    /* Must outlive the controller. Optional; pass null to disable. */
    void setProgressSink(IProgressSink *sink);

    Status configure(const RecorderConfig &config);
    const RecorderConfig &config() const { return config_; }

    /*
     * Capture one clip. `report` is filled in even on failure paths so the
     * caller can always print the real frame count and measured levels.
     */
    Status record(const char *label, CaptureReport &report);

    /*
     * Write the clip held by the last successful record() to storage. On any
     * failure the clip stays in RAM so the caller can retry with SAVE after
     * fixing the card, which is the documented recovery path.
     */
    Status savePending();

    /* Throw away the pending clip and release its buffer. */
    void discardPending();

    bool hasPending() const { return hasPending_; }
    const char *pendingLabel() const { return pendingLabel_; }
    uint64_t pendingFrames() const { return pendingFrames_; }
    const AudioStats &pendingStats() const { return pendingStats_; }
    const CaptureReport &lastReport() const { return lastReport_; }

    /*
     * Lowest unused sequence number for `label`, found by listing the directory.
     * Returns IndexExhausted past 9999 for that label.
     */
    Status nextClipIndex(const char *label, uint32_t &index);

    /* Directory of stored clips, e.g. "/" . */
    Status listRecordings(const char *dir, DirEntry *out, size_t capacity, size_t &count);

    /* Explicit mount control for the STATUS command. */
    Status mountStorage();
    void unmountStorage();
    uint64_t storageFreeBytes() const { return storage_.freeBytes(); }
    uint64_t storageTotalBytes() const { return storage_.totalBytes(); }
    bool isStorageMounted() const { return storage_.isMounted(); }

    /* Bytes a clip of the configured format occupies on disk, header included. */
    uint64_t clipFileBytes(uint64_t frames) const { return wavFileBytes(config_.channels, frames); }
    uint64_t clipFrameBytes(uint64_t frames) const { return wavDataBytes(config_.channels, frames); }

private:
    Status validateConfig() const;
    Status prepareStreaming(const char *label);
    Status finishStreaming(const char *path, uint64_t framesWritten);
    void abortStreaming();
    Status captureLoop(CaptureReport &report);
    Status writeClipTo(const char *path, const int16_t *samples, uint64_t frames);
    void releasePending();

    IAudioSource &audio_;
    IStorageBackend &storage_;
    IIndicator &indicator_;
    IClock &clock_;
    IAllocator &allocator_;
    ILogger &log_;
    IProgressSink *progress_;

    RecorderConfig config_;
    CaptureReport lastReport_;
    SampleConverter converter_;

    int16_t *pendingAudio_;
    uint64_t pendingFrames_;
    AudioStats pendingStats_;
    bool hasPending_;
    char pendingLabel_[kMaxLabelLength + 1];

    /* Streaming capture state, used only by CaptureStrategy::Streaming. */
    IFileWriter *activeWriter_;
    char activePath_[kMaxClipPathLength];
    uint64_t streamBytesWritten_;

    /*
     * Scratch for one DMA-sized block of source words plus the PCM16 the
     * converter produces from it. The PCM scratch is twice the word count
     * because a packed 16-bit source yields two samples per word. Members
     * rather than stack arrays: this is ~1.8 KB inside a call chain that also
     * runs on a task with a bounded stack.
     */
    static const size_t kScratchWords = 256;
    int32_t wordScratch_[kScratchWords];
    int16_t pcmScratch_[kScratchWords * 2];
    uint8_t writeScratch_[1024];

    /* Directory listing buffer, also a member for the same reason. */
    static const size_t kListScratchEntries = 48;
    DirEntry listScratch_[kListScratchEntries];
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_RECORDER_CONTROLLER_H