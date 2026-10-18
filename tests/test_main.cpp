#include <stdio.h>

#include "test_console.h"
#include "test_controller.h"
#include "test_core.h"
#include "TestHarness.h"

/*
 * Host test entry point.
 *
 * Everything under src/ except the ESP adapters is portable by design, so this
 * binary builds with nothing but a C++17 compiler and a nine-line Arduino.h
 * stub. That is the regression gate: the adapters are the part that changes
 * most often per chip, and keeping them out of the host build is what lets the
 * rest be verified on every commit.
 */

int reportTestResults(const char *suiteName) {
    if (adrtest::failures() == 0) {
        printf("PASS %s\n", suiteName);
        return 0;
    }
    printf("FAIL %s (%d of %d checks failed)\n", suiteName, adrtest::failures(),
           adrtest::checks());
    for (size_t i = 0; i < adrtest::failuresList().size(); ++i) {
        printf("  %s\n", adrtest::failuresList()[i].c_str());
    }
    return 1;
}

int main() {
    /* Unbuffered so a crash still shows how far the run got. */
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("AudioDatasetRecorder host tests\n");
    printf("-------------------------------\n");

    printf("[core]\n");
    runCoreTests();

    printf("[controller]\n");
    runControllerTests();

    printf("[console]\n");
    runConsoleTests();

    printf("-------------------------------\n");
    if (adrtest::checks() == 0) {
        printf("FAIL no checks ran\n");
        return 1;
    }

    if (adrtest::failures() == 0) {
        printf("All %d checks passed.\n", adrtest::checks());
        return 0;
    }

    printf("%d of %d checks FAILED.\n", adrtest::failures(), adrtest::checks());
    for (size_t i = 0; i < adrtest::failuresList().size(); ++i) {
        printf("  %s\n", adrtest::failuresList()[i].c_str());
    }
    return 1;
}