#ifndef AUDIO_DATASET_RECORDER_SERVICES_H
#define AUDIO_DATASET_RECORDER_SERVICES_H

#include <stdint.h>
#include <stddef.h>

#include "RecorderTypes.h"

namespace AudioDatasetRecorder {

/*
 * Monotonic milliseconds. Injected rather than calling millis() directly so
 * capture-timeout logic is testable on a host and so a board profile can supply
 * its own time base.
 *
 * Implementations MUST be monotonic and MUST actually advance on their own.
 * The capture watchdog is the only thing that ends a stalled recording, so a
 * clock that never moves turns a silent microphone into an infinite loop.
 */
class IClock {
public:
    virtual ~IClock() {}
    virtual uint32_t millis() const = 0;
};

/*
 * Backing memory for captured audio.
 *
 * Injected because the right answer is board-dependent: the ESP32-CAM reference
 * needs PSRAM for a 60 s clip, while a board without PSRAM must either shorten
 * the clip or stream. The controller queries largestFreeBlock() before
 * allocating so it can fail cleanly instead of half-allocating.
 */
class IAllocator {
public:
    virtual ~IAllocator() {}
    virtual void *allocate(size_t bytes) = 0;
    virtual void release(void *pointer) = 0;
    virtual size_t largestFreeBlock() const = 0;
    virtual const char *name() const = 0;
};

/*
 * printf-style logging. Kept behind an interface so the core never depends on
 * Serial and so tests can capture output.
 *
 * Implementations must accept the same format strings as printf and must not
 * crash on a null pointer argument. One call is one complete line: the
 * implementation is responsible for the line terminator, and the format
 * strings deliberately carry none.
 */
class ILogger {
public:
    virtual ~ILogger() {}
    virtual void log(const char *format, ...) = 0;
};

class NullLogger : public ILogger {
public:
    void log(const char *, ...) {}
};

/*
 * Linear-memory allocator over a caller-supplied buffer. Used by the streaming
 * capture strategy and by host tests.
 */
class BumpAllocator : public IAllocator {
public:
    BumpAllocator() : buffer_(nullptr), capacity_(0), used_(0), largestFree_(0) {}

    void configure(uint8_t *buffer, size_t capacity) {
        buffer_ = buffer;
        capacity_ = capacity;
        used_ = 0;
        largestFree_ = capacity;
    }

    void *allocate(size_t bytes) {
        if (buffer_ == nullptr || bytes > (capacity_ - used_)) return nullptr;
        void *result = buffer_ + used_;
        used_ += bytes;
        largestFree_ = capacity_ - used_;
        return result;
    }

    void release(void *) { /* no-op: bump allocators do not reclaim in order to stay O(1) */ }

    size_t largestFreeBlock() const { return largestFree_ > 0 ? largestFree_ : 0; }
    const char *name() const { return "bump"; }

private:
    uint8_t *buffer_;
    size_t capacity_;
    size_t used_;
    size_t largestFree_;
};

/* Heap allocator with no board-specific preference. Present so the core can be
 * exercised on a host without a real allocator implementation. */
class SystemAllocator : public IAllocator {
public:
    void *allocate(size_t bytes);
    void release(void *pointer);
    size_t largestFreeBlock() const;
    const char *name() const { return "system"; }
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_SERVICES_H