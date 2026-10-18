#ifndef AUDIO_DATASET_RECORDER_PCM_CONVERT_H
#define AUDIO_DATASET_RECORDER_PCM_CONVERT_H

#include <stdint.h>
#include <stddef.h>

#include "RecorderTypes.h"

namespace AudioDatasetRecorder {

/*
 * Signed 32-bit I2S slot word -> saturating PCM16.
 *
 * `rightShift` describes the alignment between the raw word and PCM16 and
 * depends on the microphone and the I2S API in use. A smaller shift means more
 * digital gain. Values above 31 are clamped.
 *
 * Saturation (never wraparound) is deliberate: a wrapped sample is an audible
 * click, a saturated one is merely loud.
 */
int16_t i2sWordToPcm16(int32_t word, uint8_t rightShift, bool *wasClipped = nullptr);

/* Pure digital attenuation. 0 is a no-op; 15 or more yields silence. */
int16_t applyAttenuation(int16_t sample, uint8_t shift);

/*
 * How an IAudioSource lays out the words it hands back. Adapters must report
 * this truthfully rather than pre-selecting a channel behind the caller's
 * back.
 *
 * SourceOutput::RawI2sWords  wordsPerFrame is 1 (the driver already reduced the
 *                           frame to the requested slot) or 2 (interleaved
 *                           left, right, left, right). samplesPerWord is 1 and
 *                           the core applies `rightShift`.
 *
 * SourceOutput::Pcm16        wordsPerFrame is 1 and samplesPerWord is 1 or 2
 *                           (two when two consecutive 16-bit samples are packed
 *                           little-endian into one 32-bit read). The core applies
 *                           no right shift at all. A PCM16 source is mono by
 *                           construction -- the driver already chose the slot --
 *                           so `channels` must be 1.
 */
struct StreamLayout {
    SourceOutput output;
    uint16_t wordsPerFrame;
    uint8_t samplesPerWord;

    StreamLayout() : output(SourceOutput::RawI2sWords), wordsPerFrame(1), samplesPerWord(1) {}
};

/* Backwards-compatible name for code written against the 0.2.0 previews. */
typedef StreamLayout RawStreamLayout;

/*
 * Turns what an IAudioSource produces into the interleaved PCM16 frames that
 * get stored.
 *
 * Raw word channel selection:
 *   wordsPerFrame == 1 : word 0 is used for every slot setting, because the
 *                        driver already selected the slot.
 *   wordsPerFrame == 2 : Left  -> word 0
 *                        Right -> word 1
 *                        Both  -> mean of word 0 and word 1
 *
 * PCM16 channel selection:
 *   All samplesPerWord halves are emitted in order, because the driver already
 *   reduced the stream to the selected slot.
 */
class SampleConverter {
public:
    SampleConverter();

    Status configure(const StreamLayout &layout, const RecorderConfig &config);

    /* Number of PCM16 samples produced from `wordCount` source words. */
    size_t samplesForWords(size_t wordCount) const;

    /* Number of sample-frames (one sample per channel) the source words carry. */
    size_t framesFromWords(size_t wordCount) const;

    /*
     * Convert one source block. Returns false and fills `outSamples` with 0 when
     * the arguments are inconsistent (bad pointer, too few words for a whole
     * frame, or an output buffer that cannot hold the result).
     */
    bool convertBlock(const int32_t *words,
                      size_t wordCount,
                      int16_t *out,
                      size_t outCapacitySamples,
                      size_t &outSamples,
                      AudioStats &stats) const;

    uint16_t wordsPerFrame() const { return wordsPerFrame_; }
    uint8_t samplesPerWord() const { return samplesPerWord_; }
    SourceOutput output() const { return output_; }
    uint8_t rightShift() const { return rightShift_; }
    uint8_t attenuationShift() const { return attenuationShift_; }
    bool configured() const { return configured_; }

private:
    int16_t scale(int32_t raw, bool &clipped) const;

    SourceOutput output_;
    uint16_t wordsPerFrame_;
    uint8_t samplesPerWord_;
    uint16_t channels_;
    SampleSlot slot_;
    uint8_t rightShift_;
    uint8_t attenuationShift_;
    bool configured_;
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_PCM_CONVERT_H