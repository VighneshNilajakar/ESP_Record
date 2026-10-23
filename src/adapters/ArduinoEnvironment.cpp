#include "ArduinoEnvironment.h"

#if defined(ESP32)

#include <stdarg.h>
#include <stdio.h>

namespace AudioDatasetRecorder {
namespace adapters {
namespace {

/* 160 bytes is enough for any line the library emits while still keeping the
 * formatter's stack cost bounded inside an Arduino task. */
const size_t kLogBufferBytes = 160;

}  // namespace

void ArduinoLogger::log(const char *format, ...) {
    if (format == nullptr) return;
    char buffer[kLogBufferBytes];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (written <= 0) return;

    const size_t length = static_cast<size_t>(written) < sizeof(buffer)
                              ? static_cast<size_t>(written)
                              : sizeof(buffer) - 1;

    /*
     * Terminate the record here. The core formats one line per call and the
     * messages carry no line ending of their own, so without this every banner
     * and STATUS line runs into the next one. CRLF rather than LF because a
     * bare line feed does not return the carriage on a plain terminal.
     */
    stream_.print(buffer);
    if (length == 0 || buffer[length - 1] != '\n') stream_.print("\r\n");
}

PsramFirstAllocator::PsramFirstAllocator() : usePsram_(psramFound()) {}

void *PsramFirstAllocator::allocate(size_t bytes) {
    if (bytes == 0) return nullptr;
    if (usePsram_) {
        void *pointer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (pointer != nullptr) return pointer;
        /* Fall through: a partially-satisfiable board should still record. */
    }
    return malloc(bytes);
}

void PsramFirstAllocator::release(void *pointer) {
    if (pointer == nullptr) return;
    free(pointer);
}

size_t PsramFirstAllocator::largestPsramBlock() const {
    if (!usePsram_) return 0;
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

size_t PsramFirstAllocator::largestHeapBlock() const {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

size_t PsramFirstAllocator::largestFreeBlock() const {
    const size_t psram = largestPsramBlock();
    const size_t heap = largestHeapBlock();
    return psram > heap ? psram : heap;
}

size_t StreamTransport::read(char *buffer, size_t capacity, uint32_t timeoutMs) {
    if (buffer == nullptr || capacity == 0) return 0;
    const uint32_t deadline = ::millis() + timeoutMs;
    size_t total = 0;
    while (total < capacity) {
        while (stream_.available() > 0 && total < capacity) {
            const int value = stream_.read();
            if (value < 0) break;
            buffer[total++] = static_cast<char>(static_cast<uint8_t>(value));
        }
        if (total > 0) return total;
        if (static_cast<int32_t>(::millis() - deadline) >= 0) return 0;
        delay(1);
    }
    return total;
}

size_t StreamTransport::write(const char *text, size_t length) {
    if (text == nullptr) return 0;
    return stream_.write(reinterpret_cast<const uint8_t *>(text), length);
}

void SerialProgressSink::onCaptureProgress(uint32_t elapsedMs, uint64_t framesCaptured,
                                           uint64_t framesExpected) {
    const uint32_t percent =
        framesExpected == 0 ? 100U
                            : static_cast<uint32_t>((framesCaptured * 100ULL) / framesExpected);
    stream_.printf("[timer] %u s, %u%%\n", elapsedMs / 1000U, percent > 100U ? 100U : percent);
}

}  // namespace adapters
}  // namespace AudioDatasetRecorder

#endif  // ESP32