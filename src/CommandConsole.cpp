#include "CommandConsole.h"

#include <stdio.h>
#include <string.h>

namespace {

char toUpperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

bool isSpaceChar(char c) { return c == ' ' || c == '\t'; }

/*
 * Normalize one raw line: drop CR and LF, trim leading and trailing spaces, and
 * report whether anything usable is left. Keeping this in one place is what lets
 * the command matcher see the same string regardless of how the user padded it.
 */
bool cleanLine(const char *line, char *out, size_t outCapacity) {
    size_t written = 0;
    size_t start = 0;

    while (line[start] != '\0' && isSpaceChar(line[start])) ++start;

    for (size_t i = start; line[i] != '\0'; ++i) {
        const char c = line[i];
        if (c == '\r' || c == '\n') continue;
        /* The ESP32 ROM prints its boot log at 74880 baud, so a monitor at
         * 115200 receives it as bytes outside printable ASCII. Treating that
         * as a command produced a large "unknown command" banner on every
         * reset; drop the whole line instead. Labels are ASCII by definition. */
        const unsigned char byte = static_cast<unsigned char>(c);
        if ((byte < 0x20 && c != '\t') || byte > 0x7e) return false;
        if (written + 1 < outCapacity) out[written++] = c;
    }
    while (written > 0 && isSpaceChar(out[written - 1])) --written;
    out[written] = '\0';
    return written > 0;
}

}  // namespace

namespace AudioDatasetRecorder {

CommandConsole::CommandConsole(RecorderController &recorder, ICommandTransport &transport, ILogger &log)
    : recorder_(recorder), transport_(transport), log_(log), lineOverflowed_(false) {
    lineBuffer_[0] = '\0';
}

bool CommandConsole::matches(const char *line, const char *keyword) const {
    const size_t length = strlen(keyword);
    for (size_t i = 0; i < length; ++i) {
        if (toUpperAscii(line[i]) != keyword[i]) return false;
    }
    const char next = line[length];
    return next == '\0' || isSpaceChar(next);
}

CommandConsole::ParseResult CommandConsole::parse(const char *line, char *label, size_t labelCapacity) const {
    if (line == nullptr || labelCapacity == 0) return ParseResult::Empty;
    label[0] = '\0';

    const char *cursor = line;
    while (isSpaceChar(*cursor)) ++cursor;
    if (*cursor == '\0') return ParseResult::Empty;

    const bool isRecord = matches(line, "RECORD");
    const bool isSave = matches(line, "SAVE");
    const bool isDiscard = matches(line, "DISCARD");
    const bool isList = matches(line, "LIST");
    const bool isStatus = matches(line, "STATUS");
    const bool isHelp = matches(line, "HELP") || matches(line, "?");

    if (!(isRecord || isSave || isDiscard || isList || isStatus || isHelp)) {
        return ParseResult::Unknown;
    }
    if (!isRecord) return ParseResult::Command;

    /* Everything after the command word is the label: trimmed, with runs of
     * whitespace collapsed so "RECORD   hey   hotpin" still reads sensibly
     * rather than being silently turned into a different label. */
    const char *end = cursor;
    while (*end != '\0' && !isSpaceChar(*end)) ++end;
    while (isSpaceChar(*end)) ++end;

    size_t written = 0;
    bool lastWasSpace = false;
    for (const char *p = end; *p != '\0' && written + 1 < labelCapacity; ++p) {
        const char c = *p;
        if (isSpaceChar(c)) {
            if (lastWasSpace || written == 0) continue;
            lastWasSpace = true;
        } else {
            lastWasSpace = false;
        }
        label[written++] = c;
    }
    while (written > 0 && isSpaceChar(label[written - 1])) --written;
    label[written] = '\0';

    return written > 0 ? ParseResult::Command : ParseResult::Invalid;
}

void CommandConsole::poll() {
    char chunk[32];
    const size_t received = transport_.read(chunk, sizeof(chunk), 5);
    if (received == 0) return;

    /*
     * Track the length explicitly instead of calling strlen() per character.
     * A C string only describes its own prefix: the first character written
     * lands at index 0, and a strlen() there would run past the terminator into
     * whatever bytes follow in the buffer, returning a length that has nothing
     * to do with this line. That silently scrambles every line after the first
     * character and eventually truncates commands mid-word.
     */
    size_t length = strlen(lineBuffer_);

    for (size_t i = 0; i < received; ++i) {
        const char c = chunk[i];
        if (c == '\n') {
            lineBuffer_[length] = '\0';
            if (lineOverflowed_) {
                log_.log("[console] input too long, command ignored. Maximum line length is %u "
                         "characters.",
                         static_cast<unsigned>(sizeof(lineBuffer_) - 1));
            } else {
                handleLine(lineBuffer_);
            }
            length = 0;
            lineOverflowed_ = false;
            continue;
        }
        if (c == '\r') continue;
        if (length + 1 < sizeof(lineBuffer_)) {
            lineBuffer_[length++] = c;
        } else {
            /* Remember the overflow and keep consuming until the line ends. */
            lineOverflowed_ = true;
        }
    }

    lineBuffer_[length] = '\0';
}

void CommandConsole::handleLine(const char *line) {
    char trimmed[sizeof(lineBuffer_)];
    if (!cleanLine(line, trimmed, sizeof(trimmed))) return;

    char label[kMaxLabelLength + 1];
    const ParseResult parsed = parse(trimmed, label, sizeof(label));

    if (parsed == ParseResult::Empty) return;

    if (parsed == ParseResult::Unknown) {
        log_.log("[console] unknown command: '%s'. Type HELP for the list.", trimmed);
        return;
    }

    if (parsed == ParseResult::Invalid) {
        log_.log("[console] RECORD needs a label, for example: RECORD hey_hotpin");
        return;
    }

    if (matches(trimmed, "RECORD")) {
        if (!isValidLabel(label)) {
            char suggestion[kMaxLabelLength + 1];
            sanitizeLabel(label, suggestion, sizeof(suggestion));
            log_.log("[console] invalid label. Labels are 1-%u characters of a-z A-Z 0-9 _ -.",
                     static_cast<unsigned>(kMaxLabelLength));
            if (isValidLabel(suggestion)) {
                log_.log("[console] did you mean: RECORD %s", suggestion);
            }
            return;
        }

        CaptureReport report;
        const Status status = recorder_.record(label, report);
        log_.log("[console] RECORD '%s' -> %s (%llu/%llu frames, %u ms%s)", label, toString(status),
                 static_cast<unsigned long long>(report.framesCaptured),
                 static_cast<unsigned long long>(report.framesExpected), report.elapsedMs,
                 report.truncated ? ", TRUNCATED" : "");
        log_.log("[console] peak %u, rms %.1f, dc %.1f, clipped %u", report.audio.peakMagnitude,
                 report.audio.rms(), report.audio.mean(), report.audio.clippedSamples);
        if (status == Status::Ok && recorder_.hasPending()) {
            log_.log("[console] clip held in RAM, send SAVE to write it");
        }
        return;
    }

    if (matches(trimmed, "SAVE")) {
        const Status status = recorder_.savePending();
        log_.log("[console] SAVE -> %s", toString(status));
        return;
    }

    if (matches(trimmed, "DISCARD")) {
        recorder_.discardPending();
        log_.log("[console] DISCARD -> done");
        return;
    }

    if (matches(trimmed, "LIST")) {
        listRecordings();
        return;
    }

    if (matches(trimmed, "STATUS")) {
        showStatus();
        return;
    }

    showHelp();
}

void CommandConsole::printBanner() const {
    log_.log("");
    log_.log("------------------------------------------------------------");
    log_.log(" AudioDatasetRecorder ready: %u s clips",
             static_cast<unsigned>(recorder_.config().durationSeconds));
    log_.log(" Type HELP to show the command menu.");
    log_.log("------------------------------------------------------------");
    log_.log("");
}

void CommandConsole::showHelp() const {
    log_.log("");
    log_.log("============================================================");
    log_.log(" COMMAND MENU");
    log_.log("============================================================");
    log_.log(" Capture");
    log_.log("   RECORD <label>   record %u s into RAM (no card needed)",
             static_cast<unsigned>(recorder_.config().durationSeconds));
    log_.log("   DISCARD          drop the pending clip without writing it");
    log_.log("");
    log_.log(" Files");
    log_.log("   SAVE             write the pending clip to the SD card");
    log_.log("   LIST             list WAV files on the card");
    log_.log("");
    log_.log(" Info");
    log_.log("   STATUS           configuration, card and last capture");
    log_.log("   HELP             this menu");
    log_.log("------------------------------------------------------------");
    log_.log(" Labels   1-%u characters: A-Z a-z 0-9 _ -",
             static_cast<unsigned>(kMaxLabelLength));
    log_.log(" Files    <label>.<3 digits>.wav, numbered from 001 per label");
    log_.log(" Example  RECORD hey_hotpin");
    log_.log("============================================================");
    log_.log("");
}

void CommandConsole::showStatus() const {
    const RecorderConfig &config = recorder_.config();
    const uint64_t frames = recorder_.clipFileBytes(
        static_cast<uint64_t>(config.sampleRateHz) * config.durationSeconds);

    log_.log("");
    log_.log("============================================================");
    log_.log(" STATUS");
    log_.log("============================================================");
    log_.log(" Capture");
    log_.log("   format        %u Hz, %u channel(s), PCM16",
             static_cast<unsigned>(config.sampleRateHz),
             static_cast<unsigned>(config.channels));
    log_.log("   duration      %u s", static_cast<unsigned>(config.durationSeconds));
    log_.log("   clip size     %llu bytes including the 44-byte WAV header",
             static_cast<unsigned long long>(frames));
    log_.log("   right shift   %u (smaller = more gain)",
             static_cast<unsigned>(config.rightShift));
    log_.log("   strategy      %s",
             config.strategy == CaptureStrategy::Streaming ? "streaming" : "full-buffer");
    log_.log("");
    log_.log(" Storage");
    if (recorder_.isStorageMounted()) {
        log_.log("   card          mounted, %llu of %llu bytes free",
                 static_cast<unsigned long long>(recorder_.storageFreeBytes()),
                 static_cast<unsigned long long>(recorder_.storageTotalBytes()));
    } else {
        log_.log("   card          not mounted (mounted only for SAVE and LIST)");
    }

    if (recorder_.hasPending()) {
        log_.log("   pending       '%s', %llu frames awaiting SAVE", recorder_.pendingLabel(),
                 static_cast<unsigned long long>(recorder_.pendingFrames()));
    } else {
        log_.log("   pending       none");
    }
    log_.log("");
    log_.log(" Last capture");

    const CaptureReport &last = recorder_.lastReport();
    if (last.framesCaptured > 0) {
        log_.log("   %llu frames in %u ms",
                 static_cast<unsigned long long>(last.framesCaptured),
                 static_cast<unsigned>(last.elapsedMs));
        log_.log("   peak %u, rms %.1f, dc %.1f, clipped %u", last.audio.peakMagnitude,
                 last.audio.rms(), last.audio.mean(), last.audio.clippedSamples);
    } else {
        log_.log("   none");
    }
    log_.log("============================================================");
    log_.log("");
}

Status CommandConsole::listRecordings() const {
    static const size_t kCapacity = 48;
    DirEntry entries[kCapacity];
    size_t count = 0;

    const Status mount = recorder_.mountStorage();
    if (mount != Status::Ok) {
        log_.log("[console] LIST -> storage mount failed (%s)", toString(mount));
        return mount;
    }

    const Status status = recorder_.listRecordings("/", entries, kCapacity, count);
    if (status != Status::Ok) {
        log_.log("[console] LIST -> %s", toString(status));
        recorder_.unmountStorage();
        return status;
    }

    size_t clips = 0;
    char label[kMaxLabelLength + 1];
    for (size_t i = 0; i < count; ++i) {
        uint32_t index = 0;
        if (entries[i].isDirectory) continue;
        if (!parseClipName(entries[i].name, label, sizeof(label), index)) continue;
        ++clips;
        log_.log("  %-24s %8llu bytes", entries[i].name,
                 static_cast<unsigned long long>(entries[i].size));
    }

    log_.log("[console] LIST -> %u clip(s)%s", static_cast<unsigned>(clips),
             count >= kCapacity ? " (listing truncated, raise kCapacity)" : "");
    recorder_.unmountStorage();
    return Status::Ok;
}

}  // namespace AudioDatasetRecorder