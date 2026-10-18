#ifndef AUDIO_DATASET_RECORDER_I_AUDIO_SOURCE_H
#define AUDIO_DATASET_RECORDER_I_AUDIO_SOURCE_H

#include <stdint.h>
#include <stddef.h>

#include "PcmConvert.h"
#include "RecorderTypes.h"

namespace AudioDatasetRecorder {

/*
 * A microphone, however it is wired.
 *
 * The adapter owns the chip-specific parts only: pin muxing, peripheral
 * setup, DMA configuration and slot selection. It must NOT silently convert or
 * rescale samples -- see outputFormat() -- and it must NOT hide a microphone's
 * data alignment behind a magic constant. Everything the core needs to convert
 * faithfully is reported through outputFormat() and layout().
 *
 * All three methods are required to work without allocating; the controller
 * may call begin()/end() repeatedly across a session.
 */
class IAudioSource {
public:
    virtual ~IAudioSource() {}

    /*
     * Start the peripheral. `format` carries the sample rate and channel count
     * the application asked for; `slot` is the microphone's L/R selection.
     * Returns Ok or a specific Status. Must be safe to call again after end().
     */
    virtual Status begin(const AudioFormat &format, SampleSlot slot) = 0;

    /*
     * Read up to `capacityWords` raw words into `dst`, blocking at most
     * `timeoutMs`. Always sets `wordsRead` (possibly 0).
     *
     * Returning Ok with wordsRead == 0 means "nothing yet, ask again".
     * Returning AudioTimeout means the peripheral produced nothing in the whole
     * budget and the caller should treat it as a stall.
     */
    virtual Status readWords(int32_t *dst, size_t capacityWords, size_t &wordsRead, uint32_t timeoutMs) = 0;

    /* Release the peripheral. Must be idempotent and must never throw. */
    virtual void end() = 0;

    virtual bool isRunning() const = 0;

    /* What readWords() produces. Decides whether the core applies rightShift. */
    virtual SourceOutput outputFormat() const = 0;

    /* How readWords() lays out the stream: output kind, words per frame, and
     * samples packed per word. */
    virtual StreamLayout layout() const = 0;

    /*
     * Human-readable adapter name for logs, e.g. "esp32-legacy-i2s". Also the
     * value the support matrix is keyed on.
     */
    virtual const char *name() const = 0;
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_I_AUDIO_SOURCE_H