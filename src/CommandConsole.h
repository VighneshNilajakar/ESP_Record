#ifndef AUDIO_DATASET_RECORDER_COMMAND_CONSOLE_H
#define AUDIO_DATASET_RECORDER_COMMAND_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

#include "RecorderController.h"
#include "Services.h"

namespace AudioDatasetRecorder {

/*
 * Line-oriented byte transport for the console. Implemented over Arduino
 * Stream by adapters/ArduinoTransport.h, and over a scripted buffer by the
 * host tests.
 */
class ICommandTransport {
public:
    virtual ~ICommandTransport() {}

    /*
     * Read whatever is available into `buffer`, waiting at most `timeoutMs` for
     * the first byte. Returns the number of bytes copied. A return of 0 means
     * nothing arrived, which is normal between lines.
     */
    virtual size_t read(char *buffer, size_t capacity, uint32_t timeoutMs) = 0;

    virtual size_t write(const char *text, size_t length) = 0;
};

/*
 * Serial command interface for RecorderController.
 *
 *   RECORD <label>   capture and (streaming profiles) or leave (full-buffer
 *                    profiles) a clip for SAVE
 *   SAVE             write the pending clip to storage
 *   DISCARD          drop the pending clip
 *   LIST             list recorded WAV files on the card
 *   STATUS           report configuration, memory, storage and indicator state
 *   HELP             show this list
 *
 * Parsing is deliberately defensive: LF and CRLF both terminate a line,
 * surrounding and repeated whitespace is collapsed, the command word is
 * matched case-insensitively, and a malformed label is refused with a
 * sanitized suggestion rather than silently turned into a different label.
 */
class CommandConsole {
public:
    CommandConsole(RecorderController &recorder, ICommandTransport &transport, ILogger &log);

    /* Read and dispatch at most one command. Cheap to call from loop(). */
    void poll();

    void printBanner() const;
    void showHelp() const;
    void showStatus() const;
    Status listRecordings() const;

    /*
     * Exposed for tests and for callers driving the console without a loop.
     * An over-long line is rejected, never truncated into a shorter command:
     * silently turning a mistyped 300-character label into a valid 24-character
     * one would record the wrong file under a name the user never asked for.
     */
    void handleLine(const char *line);

private:
    enum class ParseResult { Empty, Command, Unknown, Invalid };

    ParseResult parse(const char *line, char *label, size_t labelCapacity) const;
    bool matches(const char *line, const char *keyword) const;

    RecorderController &recorder_;
    ICommandTransport &transport_;
    ILogger &log_;

    char lineBuffer_[96];
    /* Set when input exceeded the buffer, so the command can be rejected
     * instead of silently executed as a truncated label. */
    bool lineOverflowed_;
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_COMMAND_CONSOLE_H