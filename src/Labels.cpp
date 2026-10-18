#include "Labels.h"

#include <string.h>

namespace AudioDatasetRecorder {
namespace {

bool isAllowedLabelChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

char toLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

const char *const kDefaultLabels[] = {"hey_hotpin", "noise", "unknown"};

}  // namespace

bool isValidLabel(const char *label) {
    if (label == nullptr) return false;
    const size_t length = strlen(label);
    if (length == 0 || length > kMaxLabelLength) return false;
    for (size_t i = 0; i < length; ++i) {
        if (!isAllowedLabelChar(label[i])) return false;
    }
    return true;
}

size_t sanitizeLabel(const char *input, char *out, size_t outCapacity) {
    if (input == nullptr || out == nullptr || outCapacity == 0) return 0;

    size_t written = 0;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (written >= kMaxLabelLength) break;
        if (written + 1 >= outCapacity) break;
        const char c = input[i];
        out[written++] = isAllowedLabelChar(c) ? toLowerAscii(c) : '_';
    }
    out[written] = '\0';
    return written;
}

size_t buildClipPath(char *out, size_t outCapacity, const char *dir, const char *label, uint32_t index) {
    if (out == nullptr || outCapacity == 0) return 0;
    if (!isValidLabel(label)) return 0;
    if (index == 0 || index > kMaxClipIndex) return 0;

    if (dir == nullptr) dir = "/";

    const size_t labelLength = strlen(label);
    size_t dirLength = strlen(dir);
    /* Collapse trailing slashes, and treat a bare "/" as already terminated so
     * the root produces "/noise.001.wav" and not "//noise.001.wav". */
    while (dirLength > 1 && dir[dirLength - 1] == '/') --dirLength;
    const bool dirIsRoot = (dirLength == 1 && dir[0] == '/');

    /* Three digits minimum, four once past 999, so the sequence never wraps or
     * collides with an existing name. */
    const size_t digits = (index >= 1000u) ? 4u : 3u;

    const size_t needed = dirLength + (dirIsRoot ? 0u : 1u) + labelLength + 1 + digits +
                          strlen(kClipExtension) + 1;
    if (needed > outCapacity || needed > kMaxClipPathLength + 16) return 0;

    size_t pos = 0;
    for (size_t i = 0; i < dirLength; ++i) out[pos++] = dir[i];
    if (!dirIsRoot) out[pos++] = '/';
    for (size_t i = 0; i < labelLength; ++i) out[pos++] = label[i];
    out[pos++] = '.';

    for (size_t remaining = digits; remaining > 0; --remaining) {
        uint32_t divisor = 1;
        for (size_t step = 1; step < remaining; ++step) divisor *= 10;
        out[pos++] = static_cast<char>('0' + ((index / divisor) % 10));
    }

    for (size_t i = 0; kClipExtension[i] != '\0'; ++i) out[pos++] = kClipExtension[i];
    out[pos] = '\0';
    return pos;
}

bool parseClipName(const char *name, char *label, size_t labelCapacity, uint32_t &index) {
    if (label != nullptr) label[0] = '\0';
    index = 0;
    if (name == nullptr || label == nullptr) return false;
    if (labelCapacity < kMaxLabelLength + 1) return false;

    const size_t nameLength = strlen(name);
    const size_t extensionLength = strlen(kClipExtension);
    /* Shortest legal name is "<one char>.<three digits>.wav" = extensionLength + 5. */
    if (nameLength < extensionLength + 5) return false;
    if (memcmp(name + nameLength - extensionLength, kClipExtension, extensionLength) != 0) return false;

    /* Count the digit run immediately before ".wav". Three digits up to 999,
     * four up to 9999; anything longer cannot be a name this library writes. */
    size_t digits = 0;
    while (digits < 4) {
        const char c = name[nameLength - extensionLength - 1 - digits];
        if (c < '0' || c > '9') break;
        ++digits;
    }
    if (digits < 3) return false;

    const size_t digitStart = nameLength - extensionLength - digits;
    /* The digits must be preceded by exactly one '.' that separates them from
     * the label; labels never contain '.', so this is unambiguous. */
    if (digitStart == 0 || name[digitStart - 1] != '.') return false;

    /* A four-digit index must not have a leading zero: "0001" is not a name
     * this library writes and would silently collide with "001". */
    if (digits == 4 && name[digitStart] == '0') return false;

    uint32_t parsed = 0;
    for (size_t i = 0; i < digits; ++i) {
        parsed = parsed * 10U + static_cast<uint32_t>(name[digitStart + i] - '0');
    }
    if (parsed == 0 || parsed > kMaxClipIndex) return false;

    /* Labels never contain '.', so everything before the separator is the label. */
    const size_t labelLength = digitStart - 1;
    if (labelLength == 0 || labelLength > kMaxLabelLength) return false;

    memcpy(label, name, labelLength);
    label[labelLength] = '\0';
    if (!isValidLabel(label)) {
        label[0] = '\0';
        return false;
    }

    index = parsed;
    return true;
}

bool isDefaultLabel(const char *label) {
    if (!isValidLabel(label)) return false;
    for (size_t i = 0; i < sizeof(kDefaultLabels) / sizeof(kDefaultLabels[0]); ++i) {
        if (strcmp(label, kDefaultLabels[i]) == 0) return true;
    }
    return false;
}

}  // namespace AudioDatasetRecorder