#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_ARDUINO_ENVIRONMENT_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_ARDUINO_ENVIRONMENT_H

#include <Arduino.h>

#include "../CommandConsole.h"
#include "../Services.h"

#if defined(ESP32)
#include <esp_heap_caps.h>

namespace AudioDatasetRecorder {
namespace adapters {

/* millis() behind the IClock interface. */
class ArduinoMillisClock : public IClock {
public:
    uint32_t millis() const { return ::millis(); }
};

/* printf-style logging to any Stream, normally Serial. */
class ArduinoLogger : public ILogger {
public:
    explicit ArduinoLogger(Stream &stream) : stream_(stream) {}
    void log(const char *format, ...);

private:
    Stream &stream_;
};

/*
 * Prefers PSRAM for large buffers and falls back to internal RAM.
 *
 * The fallback matters: a board without PSRAM should still work with a shorter
 * clip rather than refusing to record at all. largestFreeBlock() reports the
 * larger of the two so the controller can fail early with a useful message.
 */
class PsramFirstAllocator : public IAllocator {
public:
    PsramFirstAllocator();

    void *allocate(size_t bytes);
    void release(void *pointer);
    size_t largestFreeBlock() const;
    const char *name() const { return psramFound() ? "psram-or-heap" : "heap"; }

    size_t largestPsramBlock() const;
    size_t largestHeapBlock() const;

private:
    bool usePsram_;
};

/* ICommandTransport over any Stream, normally Serial. */
class StreamTransport : public ICommandTransport {
public:
    explicit StreamTransport(Stream &stream) : stream_(stream) {}
    size_t read(char *buffer, size_t capacity, uint32_t timeoutMs);
    size_t write(const char *text, size_t length);

private:
    Stream &stream_;
};

/*
 * Logs a once-per-second capture heartbeat to a Stream. Attach with
 * RecorderController::setProgressSink.
 */
class SerialProgressSink : public IProgressSink {
public:
    explicit SerialProgressSink(Stream &stream) : stream_(stream) {}
    void onCaptureProgress(uint32_t elapsedMs, uint64_t framesCaptured, uint64_t framesExpected);

private:
    Stream &stream_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_ARDUINO_ENVIRONMENT_H