#include "test_console.h"

#include <string>

#include "AudioDatasetRecorder.h"
#include "TestFakes.h"
#include "TestHarness.h"

using namespace AudioDatasetRecorder;

namespace {

RecorderConfig consoleConfig() {
    RecorderConfig config;
    config.sampleRateHz = 1000;
    config.channels = 1;
    config.durationSeconds = 1;
    config.rightShift = 0;
    config.minFreeBytesMargin = 0;
    return config;
}

struct Fixture {
    Fixture() : controller(audio, storage, indicator, clock, allocator, log),
                console(controller, transport, log) {
        audio.setLayout(StreamLayout());
        controller.configure(consoleConfig());
    }

    FakeAudioSource audio;
    FakeStorage storage;
    FakeIndicator indicator;
    FakeClock clock;
    FakeAllocator allocator;
    FakeLogger log;
    ScriptedTransport transport;
    RecorderController controller;
    CommandConsole console;
};

/* Feed one line through the chunked reader, exercising CR/LF handling. */
void type(Fixture &f, const char *text, int chunks = 1) {
    const std::string line(text);
    const size_t step = line.length() / static_cast<size_t>(chunks) + 1;
    for (size_t offset = 0; offset < line.length(); offset += step) {
        f.transport.feed(line.substr(offset, step));
        f.console.poll();
    }
}

void testHelpAndUnknownCommands() {
    Fixture f;
    f.console.printBanner();
    f.console.showHelp();
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "RECORD <label>");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "DISCARD");

    f.log.clear();
    type(f, "FROBNICATE\n");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "unknown command");
}

void testEmptyLinesAreIgnored() {
    Fixture f;
    type(f, "\n");
    type(f, "   \n");
    type(f, "\r\n");
    ADR_CHECK(!f.log.contains("unknown command"));
    ADR_CHECK(!f.controller.hasPending());
}

void testCrLfIsAccepted() {
    Fixture f;
    type(f, "RECORD noise\r\n");
    ADR_CHECK(f.controller.hasPending());
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "held in RAM");
}

void testCommandIsCaseInsensitive() {
    Fixture f;
    type(f, "record noise\n");
    ADR_CHECK(f.controller.hasPending());
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "'noise'");
}

void testWhitespaceIsCollapsed() {
    Fixture f;
    type(f, "   RECORD    hey_hotpin   \n");
    ADR_CHECK(f.controller.hasPending());
    /* Interior runs of whitespace collapse to one, so a spaced label is
     * reported as a single token rather than silently rewritten. */
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "SAVE");
}

void testRecordWithoutLabel() {
    Fixture f;
    type(f, "RECORD\n");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "RECORD needs a label");
    ADR_CHECK(!f.controller.hasPending());

    f.log.clear();
    type(f, "RECORD    \n");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "RECORD needs a label");
}

void testInvalidLabelSuggestsASafeAlternative() {
    Fixture f;
    type(f, "RECORD hey hotpin\n");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "invalid label");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "did you mean: RECORD hey_hotpin");
    ADR_CHECK(!f.controller.hasPending());
}

void testSaveWithoutPendingIsReportedNotFatal() {
    Fixture f;
    type(f, "SAVE\n");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "NO_PENDING_CLIP");
}

void testSaveAndDiscard() {
    Fixture f;
    type(f, "RECORD noise\n");
    ADR_CHECK(f.controller.hasPending());
    type(f, "SAVE\n");
    ADR_CHECK(!f.controller.hasPending());
    ADR_CHECK(f.storage.has("/noise.001.wav"));

    type(f, "RECORD noise\n");
    ADR_CHECK(f.controller.hasPending());
    type(f, "DISCARD\n");
    ADR_CHECK(!f.controller.hasPending());
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "DISCARD -> done");
    ADR_CHECK(!f.storage.has("/noise.002.wav"));
}

void testListAndStatus() {
    Fixture f;
    f.storage.files["/noise.001.wav"] = std::vector<uint8_t>(100, 0);
    f.storage.files["/hey_hotpin.001.wav"] = std::vector<uint8_t>(200, 0);
    f.storage.files["/notes.txt"] = std::vector<uint8_t>(10, 0);

    type(f, "LIST\n");
    const std::string listed = f.log.joined();
    ADR_CHECK_STR_CONTAINS(listed, "noise.001.wav");
    ADR_CHECK_STR_CONTAINS(listed, "hey_hotpin.001.wav");
    /* A file that is not a clip must not be counted as one. */
    ADR_CHECK_STR_CONTAINS(listed, "2 clip(s)");
    ADR_CHECK(!f.storage.isMounted());

    f.log.clear();
    type(f, "STATUS\n");
    const std::string status = f.log.joined();
    ADR_CHECK_STR_CONTAINS(status, "1000 Hz");
    ADR_CHECK_STR_CONTAINS(status, "full-buffer");
    ADR_CHECK_STR_CONTAINS(status, "pending       none");
}

void testStatusReportsPendingClip() {
    Fixture f;
    type(f, "RECORD hey_hotpin\n");
    f.log.clear();
    type(f, "STATUS\n");
    const std::string status = f.log.joined();
    ADR_CHECK_STR_CONTAINS(status, "awaiting SAVE");
    ADR_CHECK_STR_CONTAINS(status, "hey_hotpin");
}

void testListReportsMountFailure() {
    Fixture f;
    f.storage.beginFails_ = true;
    type(f, "LIST\n");
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "storage mount failed");
}

void testLongLineIsRejectedNotTruncated() {
    Fixture f;
    /* A label far longer than the line buffer must be refused outright. Running
     * it truncated would record an unrelated 24-character label, which is worse
     * than an error message. */
    std::string overlong = "RECORD ";
    for (int i = 0; i < 300; ++i) overlong += "a";
    overlong += "\n";
    type(f, overlong.c_str(), 40);
    ADR_CHECK(!f.controller.hasPending());
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "input too long");

    /* The console must recover: the next command still works. */
    f.log.clear();
    type(f, "RECORD noise\n");
    ADR_CHECK(f.controller.hasPending());
    ADR_CHECK_STR_CONTAINS(f.log.joined(), "'noise'");
}

void testBootNoiseIsIgnored() {
    Fixture f;
    /* Bytes the ROM boot log leaves in the UART buffer at the wrong baud must
     * not be reported as an unknown command. */
    const char noise[] = {static_cast<char>(0x55), static_cast<char>(0x55),
                          static_cast<char>(0xff), static_cast<char>(0xfe),
                          static_cast<char>(0x80), static_cast<char>(0x0a),
                          static_cast<char>(0x00)};
    type(f, noise);
    ADR_CHECK(f.log.lines.empty());
    ADR_CHECK(!f.controller.hasPending());
}

void testPartialLineIsNotDispatched() {
    Fixture f;
    /* Without a newline the console must not act on half a command. */
    type(f, "RECORD noise");
    ADR_CHECK(!f.controller.hasPending());
    type(f, "\n");
    ADR_CHECK(f.controller.hasPending());
}

}  // namespace

void runConsoleTests() {
    testHelpAndUnknownCommands();
    testEmptyLinesAreIgnored();
    testCrLfIsAccepted();
    testCommandIsCaseInsensitive();
    testWhitespaceIsCollapsed();
    testRecordWithoutLabel();
    testInvalidLabelSuggestsASafeAlternative();
    testSaveWithoutPendingIsReportedNotFatal();
    testSaveAndDiscard();
    testListAndStatus();
    testStatusReportsPendingClip();
    testListReportsMountFailure();
    testLongLineIsRejectedNotTruncated();
    testBootNoiseIsIgnored();
    testPartialLineIsNotDispatched();
}
