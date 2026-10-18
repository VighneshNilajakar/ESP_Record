#ifndef AUDIO_DATASET_RECORDER_AUDIO_STATS_H
#define AUDIO_DATASET_RECORDER_AUDIO_STATS_H

#include <stdint.h>

/*
 * Running audio measurements for one recording.
 *
 * All accumulators are 64-bit so that long clips cannot wrap: a 16-bit sample
 * squares to at most 2^30, so `sumSquares` only overflows after roughly
 * 1.7e10 samples (~3 hours at 16 kHz).
 */
namespace AudioDatasetRecorder {

struct AudioStats {
    uint64_t sampleCount;
    uint64_t sumSquares;
    int64_t sum;
    uint32_t clippedSamples;
    uint16_t peakMagnitude;

    AudioStats();
    void reset();

    /* `clipped` should be true when the caller saturated this sample on purpose. */
    void addSample(int16_t sample, bool clipped = false);

    double rms() const;
    double normalizedRms() const;

    /* Mean sample value. A large mean usually means I2S misalignment or DC bias. */
    double mean() const;
    double normalizedMean() const;
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_AUDIO_STATS_H