#include "RecorderController.h"

#include <stdio.h>
#include <string.h>

namespace AudioDatasetRecorder {
namespace {

/* Undo a failed streaming attempt so a truncated WAV is never left looking complete. */
void dropPartialFile(IStorageBackend &storage, const char *path) {
    storage.remove(path);
}

/* Close and release a writer exactly once, on every path. */
void destroyWriter(IFileWriter *&writer) {
    if (writer == nullptr) return;
    writer->close();
    delete writer;
    writer = nullptr;
}

}  // namespace

RecorderController::RecorderController(IAudioSource &audio,
                                       IStorageBackend &storage,
                                       IIndicator &indicator,
                                       IClock &clock,
                                       IAllocator &allocator,
                                       ILogger &log)
    : audio_(audio),
      storage_(storage),
      indicator_(indicator),
      clock_(clock),
      allocator_(allocator),
      log_(log),
      progress_(nullptr),
      pendingAudio_(nullptr),
      pendingFrames_(0),
      hasPending_(false),
      activeWriter_(nullptr),
      streamBytesWritten_(0) {
    pendingLabel_[0] = '\0';
    activePath_[0] = '\0';
    wordScratch_[0] = 0;
    pcmScratch_[0] = 0;
}

void RecorderController::setProgressSink(IProgressSink *sink) { progress_ = sink; }

Status RecorderController::configure(const RecorderConfig &config) {
    if (hasPending_) return Status::PendingClipExists;
    config_ = config;
    return validateConfig();
}

Status RecorderController::validateConfig() const {
    if (config_.sampleRateHz == 0) return Status::InvalidConfig;
    if (config_.channels != 1 && config_.channels != 2) return Status::InvalidConfig;
    if (config_.durationSeconds == 0) return Status::InvalidConfig;
    if (config_.writeChunkBytes < 4) return Status::InvalidConfig;
    if (config_.minFreeBytesMargin > 64u * 1024u * 1024u) return Status::InvalidConfig;

    const uint64_t frames =
        static_cast<uint64_t>(config_.sampleRateHz) * static_cast<uint64_t>(config_.durationSeconds);
    if (!wavFitsClassicRiff(config_.channels, frames)) return Status::InvalidConfig;
    return Status::Ok;
}

void RecorderController::releasePending() {
    if (pendingAudio_ != nullptr) {
        allocator_.release(pendingAudio_);
        pendingAudio_ = nullptr;
    }
    pendingFrames_ = 0;
    pendingStats_.reset();
    hasPending_ = false;
    pendingLabel_[0] = '\0';
}

void RecorderController::discardPending() {
    if (!hasPending_) return;
    log_.log("[recorder] DISCARD '%s' (%llu frames, never written)",
             pendingLabel_, static_cast<unsigned long long>(pendingFrames_));
    releasePending();
}

Status RecorderController::record(const char *label, CaptureReport &report) {
    report = CaptureReport();
    report.framesExpected =
        static_cast<uint64_t>(config_.sampleRateHz) * static_cast<uint64_t>(config_.durationSeconds);

    if (hasPending_) {
        log_.log("[recorder] refused RECORD: clip '%s' still pending, send SAVE or DISCARD first",
                 pendingLabel_);
        report.status = Status::PendingClipExists;
        lastReport_ = report;
        return report.status;
    }

    if (!isValidLabel(label)) {
        log_.log("[recorder] refused RECORD: invalid label '%s'",
                 label == nullptr ? "(null)" : label);
        report.status = Status::InvalidLabel;
        lastReport_ = report;
        return report.status;
    }

    Status status = validateConfig();
    if (status != Status::Ok) {
        log_.log("[recorder] refused RECORD: invalid configuration (%s)", toString(status));
        report.status = status;
        lastReport_ = report;
        return status;
    }

    /* The converter applies the right shift only when the adapter says it
     * delivers raw I2S words. A source that already converted to PCM16 is never
     * shifted a second time. See SourceOutput. */
    status = converter_.configure(audio_.layout(), config_);
    if (status != Status::Ok) {
        log_.log("[recorder] refused RECORD: %s cannot supply %u channel(s) (%s)",
                 audio_.name(), config_.channels, toString(status));
        report.status = status;
        lastReport_ = report;
        return status;
    }

    if (config_.strategy == CaptureStrategy::Streaming) {
        status = prepareStreaming(label);
        if (status != Status::Ok) {
            report.status = status;
            lastReport_ = report;
            indicator_.setError();
            return status;
        }
    } else {
        const uint64_t dataBytes = wavDataBytes(config_.channels, report.framesExpected);
        if (dataBytes == 0) {
            report.status = Status::InvalidConfig;
            lastReport_ = report;
            return report.status;
        }
        if (allocator_.largestFreeBlock() < dataBytes + config_.minFreeBytesMargin) {
            log_.log("[recorder] refused RECORD: need %llu bytes plus a %u byte margin, largest "
                     "block in %s is %llu. Shorten ADR_RECORD_SECONDS or use a streaming profile.",
                     static_cast<unsigned long long>(dataBytes), config_.minFreeBytesMargin,
                     allocator_.name(),
                     static_cast<unsigned long long>(allocator_.largestFreeBlock()));
            report.status = Status::OutOfMemory;
            lastReport_ = report;
            return report.status;
        }
        pendingAudio_ = static_cast<int16_t *>(allocator_.allocate(static_cast<size_t>(dataBytes)));
        if (pendingAudio_ == nullptr) {
            log_.log("[recorder] refused RECORD: allocation of %llu bytes failed",
                     static_cast<unsigned long long>(dataBytes));
            report.status = Status::OutOfMemory;
            lastReport_ = report;
            return report.status;
        }
    }

    AudioFormat format;
    format.sampleRateHz = config_.sampleRateHz;
    format.channels = config_.channels;
    format.bitsPerSample = 16;

    indicator_.setRecording();

    status = audio_.begin(format, config_.slot);
    if (status != Status::Ok) {
        log_.log("[recorder] audio begin failed on %s: %s", audio_.name(), toString(status));
        indicator_.allOff();
        if (config_.strategy == CaptureStrategy::Streaming) {
            abortStreaming();
            storage_.end();
        } else {
            releasePending();
        }
        report.status = status;
        lastReport_ = report;
        return status;
    }

    status = captureLoop(report);

    /* Tear the peripheral down on every path so the pins are free for the
     * storage bus and the board is never left with an owned peripheral. */
    audio_.end();
    indicator_.allOff();

    if (config_.strategy == CaptureStrategy::Streaming) {
        if (status != Status::Ok || report.truncated) {
            /* A streaming file was opened with a header that promises the full
             * length, so a short capture cannot be salvaged in place. */
            log_.log("[recorder] streaming capture failed (%s); partial file deleted",
                     toString(status == Status::Ok ? Status::AudioTimeout : status));
            abortStreaming();
            storage_.end();
            if (status == Status::Ok) status = Status::AudioTimeout;
        } else {
            status = finishStreaming(activePath_, report.framesCaptured);
        }
        report.status = status;
        lastReport_ = report;
        return status;
    }

    if (status != Status::Ok) {
        releasePending();
        report.status = status;
        lastReport_ = report;
        return status;
    }

    pendingFrames_ = report.framesCaptured;
    pendingStats_ = report.audio;
    memcpy(pendingLabel_, label, strlen(label) + 1);
    hasPending_ = true;

    report.status = Status::Ok;
    lastReport_ = report;
    return Status::Ok;
}

Status RecorderController::prepareStreaming(const char *label) {
    Status status = storage_.begin();
    if (status != Status::Ok || !storage_.isMounted()) {
        log_.log("[recorder] streaming profile needs a mounted card (%s)",
                 toString(status == Status::Ok ? Status::StorageBeginFailed : status));
        return status == Status::Ok ? Status::StorageBeginFailed : status;
    }

    uint32_t index = 0;
    status = nextClipIndex(label, index);
    if (status != Status::Ok) return status;

    if (buildClipPath(activePath_, sizeof(activePath_), "/", label, index) == 0) {
        return Status::InvalidLabel;
    }

    const uint64_t needed =
        wavFileBytes(config_.channels,
                     static_cast<uint64_t>(config_.sampleRateHz) * config_.durationSeconds);
    const uint64_t available = storage_.freeBytes();
    if (available != 0 && available < needed) {
        log_.log("[recorder] not enough space for a streaming clip: need %llu, free %llu",
                 static_cast<unsigned long long>(needed), static_cast<unsigned long long>(available));
        return Status::InsufficientSpace;
    }

    uint8_t header[kWavHeaderBytes];
    const uint64_t frames =
        static_cast<uint64_t>(config_.sampleRateHz) * static_cast<uint64_t>(config_.durationSeconds);
    ByteArrayPrint sink(header, sizeof(header));
    if (!writePcm16WavHeader(sink, config_.sampleRateHz, config_.channels, frames)) {
        return Status::InvalidConfig;
    }

    status = storage_.createWriter(activePath_, activePath_, activeWriter_);
    if (status != Status::Ok || activeWriter_ == nullptr) {
        activeWriter_ = nullptr;
        return Status::StorageOpenFailed;
    }

    size_t headerWritten = 0;
    status = activeWriter_->write(header, sizeof(header), headerWritten);
    if (status != Status::Ok || headerWritten != sizeof(header)) {
        destroyWriter(activeWriter_);
        dropPartialFile(storage_, activePath_);
        return Status::StorageWriteFailed;
    }

    streamBytesWritten_ = sizeof(header);
    log_.log("[recorder] streaming to %s", activePath_);
    return Status::Ok;
}

void RecorderController::abortStreaming() {
    destroyWriter(activeWriter_);
    if (activePath_[0] != '\0') {
        dropPartialFile(storage_, activePath_);
        activePath_[0] = '\0';
    }
    streamBytesWritten_ = 0;
}

Status RecorderController::finishStreaming(const char *path, uint64_t framesWritten) {
    Status status = Status::Ok;
    if (activeWriter_ != nullptr) {
        status = activeWriter_->flush();
        if (status == Status::Ok) status = activeWriter_->close();
        destroyWriter(activeWriter_);
        if (status != Status::Ok) {
            log_.log("[recorder] failed to close %s: %s", path, toString(status));
            dropPartialFile(storage_, path);
            storage_.end();
            return Status::StorageCloseFailed;
        }
    }

    /* Verify while the card is still mounted so a bad file can be removed. */
    const uint64_t expected = wavFileBytes(config_.channels, framesWritten);
    uint64_t onDisk = 0;
    const Status sizeStatus = storage_.fileSize(path, onDisk);
    const bool sizeOk = (sizeStatus == Status::Ok && onDisk == expected &&
                         onDisk == streamBytesWritten_);

    if (!sizeOk) {
        log_.log("[recorder] streaming size check failed for %s: card reports %llu, wrote %llu, "
                 "expected %llu; file deleted",
                 path, static_cast<unsigned long long>(onDisk),
                 static_cast<unsigned long long>(streamBytesWritten_),
                 static_cast<unsigned long long>(expected));
        dropPartialFile(storage_, path);
        storage_.end();
        indicator_.setError();
        return Status::StorageSizeMismatch;
    }

    log_.log("[recorder] saved %s: %llu bytes", path, static_cast<unsigned long long>(onDisk));
    storage_.end();
    activePath_[0] = '\0';
    streamBytesWritten_ = 0;
    indicator_.setSuccess();
    return Status::Ok;
}

Status RecorderController::captureLoop(CaptureReport &report) {
    const uint64_t framesExpected = report.framesExpected;
    const uint64_t deadlineMs =
        static_cast<uint64_t>(config_.durationSeconds) * 1000ULL + config_.captureOverrunMs;
    const uint16_t wordsPerFrame = converter_.wordsPerFrame();

    const uint32_t startedMs = clock_.millis();
    uint32_t nextTickMs = 1000;
    Status readStatus = Status::Ok;

    while (report.framesCaptured < framesExpected) {
        const uint32_t nowMs = clock_.millis();
        if (static_cast<uint64_t>(nowMs - startedMs) > deadlineMs) {
            readStatus = Status::AudioTimeout;
            break;
        }

        if (nowMs - startedMs >= nextTickMs) {
            if (progress_ != nullptr) {
                progress_->onCaptureProgress(nowMs - startedMs, report.framesCaptured, framesExpected);
            }
            nextTickMs += 1000;
        }

        size_t wordsRead = 0;
        const Status sourceStatus =
            audio_.readWords(wordScratch_, kScratchWords, wordsRead, config_.readTimeoutMs);
        if (sourceStatus != Status::Ok) {
            readStatus = sourceStatus;
            break;
        }
        if (wordsRead == 0) continue;

        /* Never convert more frames than are still wanted, so the statistics
         * cannot be inflated by a final partial DMA block. */
        const uint64_t remainingFrames = framesExpected - report.framesCaptured;
        const uint64_t maxWords = remainingFrames * wordsPerFrame;
        if (static_cast<uint64_t>(wordsRead) > maxWords) {
            wordsRead = static_cast<size_t>(maxWords);
        }
        if (wordsRead < wordsPerFrame) continue;

        size_t produced = 0;
        if (!converter_.convertBlock(wordScratch_, wordsRead, pcmScratch_, kScratchWords, produced,
                                     report.audio)) {
            readStatus = Status::Internal;
            break;
        }

        const size_t producedFrames = converter_.framesFromWords(wordsRead);

        if (produced * sizeof(int16_t) > sizeof(writeScratch_)) {
            /* Cannot happen: produced is bounded by kScratchWords samples. */
            readStatus = Status::Internal;
            break;
        }
        memcpy(writeScratch_, pcmScratch_, produced * sizeof(int16_t));

        if (config_.strategy == CaptureStrategy::FullBuffer) {
            memcpy(pendingAudio_ + (report.framesCaptured * config_.channels), pcmScratch_,
                   produced * sizeof(int16_t));
        } else {
            size_t written = 0;
            const Status writeStatus =
                activeWriter_->write(writeScratch_, produced * sizeof(int16_t), written);
            if (writeStatus != Status::Ok || written != produced * sizeof(int16_t)) {
                log_.log("[recorder] short write during streaming: %llu of %llu bytes",
                         static_cast<unsigned long long>(written),
                         static_cast<unsigned long long>(produced * sizeof(int16_t)));
                readStatus = Status::StorageWriteFailed;
                break;
            }
            streamBytesWritten_ += written;
        }

        report.framesCaptured += producedFrames;
    }

    report.elapsedMs = clock_.millis() - startedMs;

    if (report.framesCaptured < framesExpected) {
        report.truncated = true;
        if (!config_.acceptTruncatedClip || readStatus == Status::StorageWriteFailed) {
            log_.log("[recorder] capture incomplete: %llu of %llu frames (%s); clip discarded",
                     static_cast<unsigned long long>(report.framesCaptured),
                     static_cast<unsigned long long>(framesExpected), toString(readStatus));
            report.status = readStatus == Status::Ok ? Status::AudioTimeout : readStatus;
            return report.status;
        }
        log_.log("[recorder] WARNING: capture incomplete: %llu of %llu frames; saving short clip",
                 static_cast<unsigned long long>(report.framesCaptured),
                 static_cast<unsigned long long>(framesExpected));
    }

    report.status = Status::Ok;
    return Status::Ok;
}

Status RecorderController::savePending() {
    if (!hasPending_) return Status::NoPendingClip;

    Status status = storage_.begin();
    if (status != Status::Ok || !storage_.isMounted()) {
        log_.log("[recorder] storage mount failed (%s); clip kept in RAM, send SAVE to retry",
                 toString(status == Status::Ok ? Status::StorageBeginFailed : status));
        indicator_.setError();
        return status == Status::Ok ? Status::StorageBeginFailed : status;
    }

    uint32_t index = 0;
    status = nextClipIndex(pendingLabel_, index);
    if (status != Status::Ok) {
        storage_.end();
        indicator_.setError();
        log_.log("[recorder] cannot choose a filename for '%s': %s", pendingLabel_, toString(status));
        return status;
    }

    char path[kMaxClipPathLength];
    if (buildClipPath(path, sizeof(path), "/", pendingLabel_, index) == 0) {
        storage_.end();
        indicator_.setError();
        log_.log("[recorder] refused to build a path for label '%s'", pendingLabel_);
        return Status::InvalidLabel;
    }

    const uint64_t expected = wavFileBytes(config_.channels, pendingFrames_);
    log_.log("[recorder] saving %s (%llu frames, %llu bytes)", path,
             static_cast<unsigned long long>(pendingFrames_),
             static_cast<unsigned long long>(expected));

    status = writeClipTo(path, pendingAudio_, pendingFrames_);

    uint64_t onDisk = 0;
    if (status == Status::Ok) {
        const Status sizeStatus = storage_.fileSize(path, onDisk);
        if (sizeStatus != Status::Ok || onDisk != expected) {
            log_.log("[recorder] size check failed for %s: card reports %llu, expected %llu", path,
                     static_cast<unsigned long long>(onDisk),
                     static_cast<unsigned long long>(expected));
            storage_.remove(path);
            status = Status::StorageSizeMismatch;
        }
    }

    storage_.end();

    if (status != Status::Ok) {
        indicator_.setError();
        log_.log("[recorder] save failed (%s); clip kept in RAM, send SAVE to retry", toString(status));
        return status;
    }

    log_.log("[recorder] saved %s: %llu bytes, peak %u, rms %.1f, clipped %u", path,
             static_cast<unsigned long long>(onDisk), pendingStats_.peakMagnitude,
             pendingStats_.rms(), pendingStats_.clippedSamples);

    releasePending();
    indicator_.setSuccess();
    return Status::Ok;
}

Status RecorderController::writeClipTo(const char *path, const int16_t *samples, uint64_t frames) {
    const uint64_t needed = wavFileBytes(config_.channels, frames);
    if (needed == 0) return Status::InvalidConfig;

    const uint64_t available = storage_.freeBytes();
    if (available != 0 && available < needed) {
        log_.log("[recorder] not enough space: need %llu, free %llu",
                 static_cast<unsigned long long>(needed), static_cast<unsigned long long>(available));
        return Status::InsufficientSpace;
    }

    IFileWriter *writer = nullptr;
    Status status = storage_.createWriter(path, path, writer);
    if (status != Status::Ok || writer == nullptr) return Status::StorageOpenFailed;

    /* Any failure below must delete the partial file, otherwise a WAV whose
     * header promises more audio than it contains is left on the card. */
    uint8_t header[kWavHeaderBytes];
    ByteArrayPrint sink(header, sizeof(header));
    if (!writePcm16WavHeader(sink, config_.sampleRateHz, config_.channels, frames)) {
        destroyWriter(writer);
        dropPartialFile(storage_, path);
        return Status::StorageWriteFailed;
    }

    size_t written = 0;
    status = writer->write(header, sizeof(header), written);
    if (status != Status::Ok || written != sizeof(header)) {
        destroyWriter(writer);
        dropPartialFile(storage_, path);
        return Status::StorageWriteFailed;
    }

    const size_t perChunkSamples =
        config_.channels * (config_.writeChunkBytes / (config_.channels * sizeof(int16_t)));
    const size_t chunkSamples = perChunkSamples > 0 ? perChunkSamples : config_.channels;

    uint64_t remaining = frames * config_.channels;
    const int16_t *cursor = samples;
    while (remaining > 0) {
        const size_t batch = (remaining < static_cast<uint64_t>(chunkSamples))
                                 ? static_cast<size_t>(remaining)
                                 : chunkSamples;
        const size_t byteCount = batch * sizeof(int16_t);
        const uint8_t *source;

        if (byteCount <= sizeof(writeScratch_)) {
            memcpy(writeScratch_, cursor, byteCount);
            source = writeScratch_;
        } else {
            source = reinterpret_cast<const uint8_t *>(cursor);
        }

        status = writer->write(source, byteCount, written);
        if (status != Status::Ok || written != byteCount) {
            log_.log("[recorder] short write to %s: %llu of %llu bytes", path,
                     static_cast<unsigned long long>(written),
                     static_cast<unsigned long long>(byteCount));
            destroyWriter(writer);
            dropPartialFile(storage_, path);
            return Status::StorageWriteFailed;
        }

        cursor += batch;
        remaining -= batch;
    }

    status = writer->flush();
    if (status != Status::Ok) {
        destroyWriter(writer);
        dropPartialFile(storage_, path);
        return status;
    }

    status = writer->close();
    destroyWriter(writer);
    if (status != Status::Ok) {
        dropPartialFile(storage_, path);
        return Status::StorageCloseFailed;
    }

    return Status::Ok;
}

Status RecorderController::nextClipIndex(const char *label, uint32_t &index) {
    index = 0;
    if (!isValidLabel(label)) return Status::InvalidLabel;

    size_t count = 0;
    const Status status = storage_.listDir("/", listScratch_, kListScratchEntries, count);
    if (status != Status::Ok) return status;

    uint32_t highest = 0;
    char entryLabel[kMaxLabelLength + 1];
    for (size_t i = 0; i < count; ++i) {
        if (listScratch_[i].isDirectory) continue;
        uint32_t entryIndex = 0;
        if (!parseClipName(listScratch_[i].name, entryLabel, sizeof(entryLabel), entryIndex)) continue;
        if (strcmp(entryLabel, label) != 0) continue;
        if (entryIndex > highest) highest = entryIndex;
    }

    if (highest >= kMaxClipIndex) return Status::IndexExhausted;
    index = highest + 1;
    return Status::Ok;
}

Status RecorderController::listRecordings(const char *dir, DirEntry *out, size_t capacity, size_t &count) {
    if (dir == nullptr) dir = "/";
    return storage_.listDir(dir, out, capacity, count);
}

Status RecorderController::mountStorage() { return storage_.begin(); }
void RecorderController::unmountStorage() { storage_.end(); }

}  // namespace AudioDatasetRecorder