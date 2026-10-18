#ifndef AUDIO_DATASET_RECORDER_TEST_FAKES_H
#define AUDIO_DATASET_RECORDER_TEST_FAKES_H

#include <stdarg.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "CommandConsole.h"
#include "IAudioSource.h"
#include "IIndicator.h"
#include "IStorageBackend.h"
#include "RecorderController.h"
#include "Services.h"

/* ------------------------------------------------------------------ clock */

class FakeClock : public AudioDatasetRecorder::IClock {
public:
    FakeClock() : now_(0) {}
    uint32_t millis() const { return now_; }
    void advance(uint32_t ms) { now_ += ms; }
    void set(uint32_t ms) { now_ = ms; }

private:
    uint32_t now_;
};

/* ----------------------------------------------------------------- logger */

class FakeLogger : public AudioDatasetRecorder::ILogger {
public:
    void log(const char *format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        lines.push_back(buffer);
    }

    bool contains(const std::string &needle) const {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find(needle) != std::string::npos) return true;
        }
        return false;
    }

    std::string joined() const {
        std::string result;
        for (size_t i = 0; i < lines.size(); ++i) {
            result += lines[i];
            result += "\n";
        }
        return result;
    }

    void clear() { lines.clear(); }

    std::vector<std::string> lines;
};

/* -------------------------------------------------------------- indicator */

class FakeIndicator : public AudioDatasetRecorder::IIndicator {
public:
    FakeIndicator() : recordingCalls(0), allOffCalls(0), successCalls(0), errorCalls(0) {}

    AudioDatasetRecorder::Status begin() { return AudioDatasetRecorder::Status::Ok; }
    void setIdle() {}
    void setRecording() { ++recordingCalls; }
    void setSuccess() { ++successCalls; }
    void setError() { ++errorCalls; }
    void allOff() { ++allOffCalls; }
    const char *name() const { return "fake"; }

    int recordingCalls;
    int allOffCalls;
    int successCalls;
    int errorCalls;
};

/* -------------------------------------------------------------- allocator */

class FakeAllocator : public AudioDatasetRecorder::IAllocator {
public:
    FakeAllocator() : limit_(1024u * 1024u), failNext_(false), outstanding_(0), allocCalls_(0) {}

    void setLimit(size_t bytes) { limit_ = bytes; }
    void setFailNext(bool value) { failNext_ = value; }

    void *allocate(size_t bytes) {
        ++allocCalls_;
        if (failNext_ || bytes > limit_) return nullptr;
        void *pointer = ::operator new(bytes);
        live_[pointer] = bytes;
        outstanding_ += bytes;
        return pointer;
    }

    void release(void *pointer) {
        if (pointer == nullptr) return;
        std::map<void *, size_t>::iterator it = live_.find(pointer);
        if (it != live_.end()) {
            outstanding_ -= it->second;
            live_.erase(it);
        }
        ::operator delete(pointer);
    }

    size_t largestFreeBlock() const { return limit_; }
    const char *name() const { return "fake"; }

    size_t outstanding_;
    size_t limit_;
    bool failNext_;
    int allocCalls_;

private:
    std::map<void *, size_t> live_;
};

/* ----------------------------------------------------------- audio source */

using AudioDatasetRecorder::AudioFormat;
using AudioDatasetRecorder::SampleSlot;
using AudioDatasetRecorder::Status;
using AudioDatasetRecorder::StreamLayout;

/*
 * Scriptable IAudioSource.
 *
 * Generates a deterministic saw wave by default so statistics are predictable,
 * and can stall, fail or truncate on demand. `wordsRemaining` bounds how much
 * audio it will ever deliver, which is how truncation and watchdog tests are
 * driven.
 */
class FakeAudioSource : public AudioDatasetRecorder::IAudioSource {
public:
    FakeAudioSource()
        : layout_(),
          running_(false),
          beginCalls_(0),
          endCalls_(0),
          readCalls_(0),
          wordsRemaining_(SIZE_MAX),
          failBegin_(false),
          failAfterReads_(SIZE_MAX),
          msPerRead_(1),
          readStatus_(Status::Ok),
          amplitude_(1 << 20),
          advanceClock_(nullptr) {}

    /* Layout the source advertises. */
    void setLayout(const StreamLayout &layout) { layout_ = layout; }
    void setWordsRemaining(size_t value) { wordsRemaining_ = value; }
    void setFailBegin(bool value) { failBegin_ = value; }
    void setFailAfterReads(size_t value) { failAfterReads_ = value; }
    void setReadStatus(Status value) { readStatus_ = value; }
    void setAmplitude(int32_t value) { amplitude_ = value; }

    /*
     * Advance an injected clock on every read. That is how a stalled capture is
     * reproduced: the source stops producing words while wall time keeps
     * moving, and the controller's watchdog must notice.
     */
    void advanceClockOnRead(FakeClock *clock, uint32_t ms) {
        advanceClock_ = clock;
        msPerRead_ = ms;
    }

    Status begin(const AudioFormat &format, SampleSlot slot) {
        ++beginCalls_;
        (void)format;
        (void)slot;
        if (failBegin_) return Status::AudioBeginFailed;
        running_ = true;
        return Status::Ok;
    }

    Status readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead, uint32_t timeoutMs) {
        (void)timeoutMs;
        ++readCalls_;
        if (advanceClock_ != nullptr) advanceClock_->advance(msPerRead_);
        wordsRead = 0;
        if (!running_) return Status::AudioReadFailed;
        if (readCalls_ > failAfterReads_) return Status::AudioReadFailed;

        size_t produced = 0;
        /* The budget covers words already emitted in this call, not just the
         * running total, or the source would over-produce by up to one block. */
        while (produced < capacityWords && (emitted_ + produced) < wordsRemaining_) {
            dst[produced] = generateWord(emitted_ + produced);
            ++produced;
        }
        emitted_ += produced;
        wordsRead = produced;
        return readStatus_;
    }

    void end() {
        ++endCalls_;
        running_ = false;
    }

    bool isRunning() const { return running_; }
    AudioDatasetRecorder::SourceOutput outputFormat() const { return layout_.output; }
    StreamLayout layout() const { return layout_; }
    const char *name() const { return "fake-audio"; }

    int beginCalls() const { return beginCalls_; }
    int endCalls() const { return endCalls_; }
    int readCalls() const { return static_cast<int>(readCalls_); }
    size_t emittedWords() const { return emitted_; }
    void resetEmitted() { emitted_ = 0; }

private:
    /* Deterministic, asymmetric so a wrongly selected slot is detectable. */
    int32_t generateWord(size_t index) const {
        const int32_t phase = static_cast<int32_t>(index % 64);
        return static_cast<int32_t>((static_cast<int64_t>(amplitude_) * phase) / 32) - amplitude_;
    }

    StreamLayout layout_;
    bool running_;
    int beginCalls_;
    int endCalls_;
    size_t readCalls_;
    size_t emitted_ = 0;
    size_t wordsRemaining_;
    bool failBegin_;
    size_t failAfterReads_;
    uint32_t msPerRead_;
    Status readStatus_;
    int32_t amplitude_;
    FakeClock *advanceClock_;
};

/* -------------------------------------------------------- storage backend */

class FakeFileWriter : public AudioDatasetRecorder::IFileWriter {
public:
    explicit FakeFileWriter(std::vector<uint8_t> &sink) : sink_(sink), open_(true) {}

    Status write(const uint8_t *data, size_t length, size_t &written) {
        written = 0;
        if (!open_) return Status::StorageWriteFailed;
        if (failWrite_ || (shortWriteAt_ != SIZE_MAX && sink_.size() >= shortWriteAt_)) {
            /* Report a short write without appending anything, which is the
             * worst case the controller must still detect. */
            written = length > 1 ? length - 1 : 0;
            return Status::StorageWriteFailed;
        }
        for (size_t i = 0; i < length; ++i) sink_.push_back(data[i]);
        written = length;
        return Status::Ok;
    }

    Status flush() { return open_ ? Status::Ok : Status::StorageCloseFailed; }
    Status close() {
        open_ = false;
        return Status::Ok;
    }

    void setFailWrite(bool value) { failWrite_ = value; }
    void setShortWriteAt(size_t bytes) { shortWriteAt_ = bytes; }

private:
    std::vector<uint8_t> &sink_;
    bool open_;
    bool failWrite_ = false;
    size_t shortWriteAt_ = SIZE_MAX;
};

/*
 * In-memory storage. Files are stored by their full path so the controller's
 * size verification and partial-file removal can be observed directly.
 */
class FakeStorage : public AudioDatasetRecorder::IStorageBackend {
public:
    FakeStorage()
        : mounted_(false),
          beginFails_(false),
          mountCount_(0),
          unmountCount_(0),
          freeBytes_(8u * 1024u * 1024u),
          totalBytes_(16u * 1024u * 1024u),
          createFails_(false),
          reportSizeDelta_(0) {}

    Status begin() {
        ++mountCount_;
        if (beginFails_) return Status::StorageBeginFailed;
        mounted_ = true;
        return Status::Ok;
    }

    void end() {
        if (mounted_) ++unmountCount_;
        mounted_ = false;
    }

    bool isMounted() const { return mounted_; }

    Status exists(const char *path, bool &out) {
        out = files.find(path) != files.end();
        return Status::Ok;
    }

    Status fileSize(const char *path, uint64_t &out) {
        std::map<std::string, std::vector<uint8_t> >::iterator it = files.find(path);
        if (it == files.end()) return Status::StorageOpenFailed;
        out = static_cast<uint64_t>(it->second.size()) + reportSizeDelta_;
        return Status::Ok;
    }

    Status remove(const char *path) {
        if (files.erase(path) == 0) return Status::StorageOpenFailed;
        return Status::Ok;
    }

    Status listDir(const char *dir, AudioDatasetRecorder::DirEntry *out, size_t capacity,
                   size_t &count) {
        count = 0;
        (void)dir;
        for (std::map<std::string, std::vector<uint8_t> >::const_iterator it = files.begin();
             it != files.end() && count < capacity; ++it) {
            const std::string &name = it->first;
            /* Only the basename, matching a real root listing. */
            const size_t slash = name.find_last_of('/');
            const std::string base =
                slash == std::string::npos ? name : name.substr(slash + 1);
            if (base.length() >= AudioDatasetRecorder::DirEntry::kNameCapacity) continue;
            memcpy(out[count].name, base.c_str(), base.length() + 1);
            out[count].size = it->second.size();
            out[count].isDirectory = false;
            out[count].nameTruncated = false;
            ++count;
        }
        return Status::Ok;
    }

    Status createWriter(const char *path, const char *partialPath, AudioDatasetRecorder::IFileWriter *&out) {
        (void)partialPath;
        out = nullptr;
        if (createFails_) return Status::StorageOpenFailed;
        files[path] = std::vector<uint8_t>();
        /* Ownership passes to the caller: the controller deletes every writer it
         * is handed, exactly as it must on a real backend. The fake therefore
         * must not keep a pointer of its own. */
        out = new FakeFileWriter(files[path]);
        return Status::Ok;
    }

    uint64_t totalBytes() const { return totalBytes_; }
    uint64_t freeBytes() const { return mounted_ ? freeBytes_ : 0; }
    const char *name() const { return "fake-storage"; }

    bool has(const std::string &path) const { return files.find(path) != files.end(); }
    size_t fileCount() const { return files.size(); }

    std::map<std::string, std::vector<uint8_t> > files;
    bool mounted_;
    bool beginFails_;
    int mountCount_;
    int unmountCount_;
    uint64_t freeBytes_;
    uint64_t totalBytes_;
    bool createFails_;
    /* Make the card report a size that disagrees with reality. */
    uint64_t reportSizeDelta_;
};

/* -------------------------------------------------------------- transport */

/* Feeds a fixed script into the console one chunk at a time. */
class ScriptedTransport : public AudioDatasetRecorder::ICommandTransport {
public:
    size_t read(char *buffer, size_t capacity, uint32_t timeoutMs) {
        (void)timeoutMs;
        if (script_.empty()) return 0;
        size_t produced = 0;
        while (produced < capacity && !script_.empty()) {
            buffer[produced++] = script_[0];
            script_.erase(script_.begin());
        }
        return produced;
    }

    size_t write(const char *text, size_t length) {
        output.append(text, length);
        return length;
    }

    void feed(const std::string &text) { script_ += text; }
    const std::string &transcript() const { return output; }

private:
    std::string script_;
    std::string output;
};

/* --------------------------------------------------------------- progress */

class RecordingProgressSink : public AudioDatasetRecorder::IProgressSink {
public:
    void onCaptureProgress(uint32_t elapsedMs, uint64_t framesCaptured, uint64_t framesExpected) {
        (void)elapsedMs;
        (void)framesExpected;
        ++calls;
        lastFrames = framesCaptured;
    }

    int calls = 0;
    uint64_t lastFrames = 0;
};

#endif  // AUDIO_DATASET_RECORDER_TEST_FAKES_H