#ifndef AUDIO_DATASET_RECORDER_LABELS_H
#define AUDIO_DATASET_RECORDER_LABELS_H

#include <stdint.h>
#include <stddef.h>

namespace AudioDatasetRecorder {

/* Labels are used to build filenames, so the character set is an allow-list. */
static const size_t kMaxLabelLength = 24;

/* Zero-padded sequence, e.g. /noise.007.wav. Three digits up to 999, four up
 * to this maximum. Files written by the 0.1.x sketch used %03lu and produced a
 * fourth digit past 999, so four-digit names must keep working. */
static const size_t kMaxClipIndex = 9999;
static const char kClipExtension[] = ".wav";

/* Enough for "/" + 24 label chars + '.' + 4 digits + ".wav" + terminator. */
static const size_t kMaxClipPathLength = 1 + kMaxLabelLength + 1 + 4 + 4 + 1;

/*
 * A label is valid only if it is non-empty, at most kMaxLabelLength
 * characters, and every character is an ASCII letter, digit, underscore or
 * hyphen. Everything else -- '.', '/', '\', spaces, control bytes, non-ASCII --
 * is rejected. Because '.' cannot appear, ".." can never be produced, so path
 * traversal is impossible by construction rather than by filtering.
 */
bool isValidLabel(const char *label);

/*
 * Best-effort cleanup for interactive input: lowercases, replaces every
 * disallowed character with '_', and truncates. The result is still rejected
 * by isValidLabel if it ends up empty, which is the authority. Returns the
 * number of characters written, or 0 if nothing fit.
 */
size_t sanitizeLabel(const char *input, char *out, size_t outCapacity);

/*
 * Build "<dir>/<label>.<NNN>.wav". `dir` may be empty or "/" for the card
 * root. Returns the number of characters written, or 0 if the result would not
 * fit or the label is invalid.
 */
size_t buildClipPath(char *out, size_t outCapacity, const char *dir, const char *label, uint32_t index);

/*
 * Inverse of buildClipPath for a bare directory entry name. Returns true only
 * for exactly "<label>.<NNN>.wav" with a valid label and a three-digit index in
 * 1..kMaxClipIndex.
 *
 * The label is copied into `label` rather than pointed into `name`, because the
 * caller's comparison must work on a NUL-terminated string. `labelCapacity`
 * must be at least kMaxLabelLength + 1; a smaller buffer is treated as a parse
 * failure instead of being overrun.
 */
bool parseClipName(const char *name, char *label, size_t labelCapacity, uint32_t &index);

/* True when the label is one of the built-in suggestions. Informational only. */
bool isDefaultLabel(const char *label);

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_LABELS_H