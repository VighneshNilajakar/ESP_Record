#ifndef AUDIO_DATASET_RECORDER_TEST_HARNESS_H
#define AUDIO_DATASET_RECORDER_TEST_HARNESS_H

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

/*
 * Tiny assertion harness. Deliberately not a third-party framework: the whole
 * point of the host build is that it needs nothing beyond a C++17 compiler.
 */
namespace adrtest {

inline int &failures() {
    static int value = 0;
    return value;
}

inline int &checks() {
    static int value = 0;
    return value;
}

inline std::vector<std::string> &failuresList() {
    static std::vector<std::string> value;
    return value;
}

inline void recordFailure(const char *file, int line, const std::string &text) {
    ++failures();
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s:%d: %s", file, line, text.c_str());
    failuresList().push_back(buffer);
}

template <typename A, typename B>
inline bool equalEnough(const A &a, const B &b) {
    return a == b;
}

}  // namespace adrtest

#define ADR_CHECK(condition)                                                       \
    do {                                                                           \
        ++adrtest::checks();                                                       \
        if (!(condition)) adrtest::recordFailure(__FILE__, __LINE__, #condition);  \
    } while (0)

#define ADR_CHECK_EQ(actual, expected)                                                        \
    do {                                                                                      \
        ++adrtest::checks();                                                                  \
        if (!(actual == expected)) {                                                          \
            char adrEqBuffer[512];                                                            \
            snprintf(adrEqBuffer, sizeof(adrEqBuffer),                                        \
                     "%s == %s  (actual %lld, expected %lld)", #actual, #expected,            \
                     static_cast<long long>(actual), static_cast<long long>(expected));       \
            adrtest::recordFailure(__FILE__, __LINE__, adrEqBuffer);                          \
        }                                                                                     \
    } while (0)

#define ADR_CHECK_NEAR(actual, expected, tolerance)                                           \
    do {                                                                                       \
        ++adrtest::checks();                                                                   \
        double adrDelta = static_cast<double>(actual) - static_cast<double>(expected);         \
        if (adrDelta < 0) adrDelta = -adrDelta;                                               \
        if (!(adrDelta <= static_cast<double>(tolerance))) {                                  \
            char adrNearBuffer[512];                                                          \
            snprintf(adrNearBuffer, sizeof(adrNearBuffer), "%s ~= %s  (actual %f, expected %f)", \
                     #actual, #expected, static_cast<double>(actual),                          \
                     static_cast<double>(expected));                                          \
            adrtest::recordFailure(__FILE__, __LINE__, adrNearBuffer);                        \
        }                                                                                      \
    } while (0)

#define ADR_CHECK_STR_CONTAINS(haystack, needle)                                              \
    do {                                                                                       \
        ++adrtest::checks();                                                                   \
        const std::string adrHay = (haystack);                                                 \
        const std::string adrNeed = (needle);                                                  \
        if (adrHay.find(adrNeed) == std::string::npos) {                                       \
            char adrStrBuffer[512];                                                            \
            snprintf(adrStrBuffer, sizeof(adrStrBuffer),                                       \
                     "expected to contain \"%s\", got \"%s\"", adrNeed.c_str(),                \
                     adrHay.c_str());                                                          \
            adrtest::recordFailure(__FILE__, __LINE__, adrStrBuffer);                          \
        }                                                                                      \
    } while (0)

#define ADR_SKIP_IF(cond) \
    if (cond) return

/* Run one test case, announcing it so a crash names its own test. */
#define ADR_RUN(fn)                          \
    do {                                     \
        printf("  - %s\n", #fn);             \
        fn();                                \
    } while (0)

int reportTestResults(const char *suiteName);

#endif  // AUDIO_DATASET_RECORDER_TEST_HARNESS_H