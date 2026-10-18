#include "AudioStats.h"

#include <math.h>

namespace AudioDatasetRecorder {

AudioStats::AudioStats() { reset(); }

void AudioStats::reset() {
    sampleCount = 0;
    sumSquares = 0;
    sum = 0;
    clippedSamples = 0;
    peakMagnitude = 0;
}

void AudioStats::addSample(int16_t sample, bool clipped) {
    const int32_t widened = static_cast<int32_t>(sample);
    const uint32_t magnitude = static_cast<uint32_t>(widened < 0 ? -widened : widened);
    if (magnitude > peakMagnitude) {
        peakMagnitude = static_cast<uint16_t>(magnitude > 32768U ? 32768U : magnitude);
    }
    const int64_t wide = static_cast<int64_t>(sample);
    sumSquares += static_cast<uint64_t>(wide * wide);
    sum += wide;
    ++sampleCount;
    if (clipped) ++clippedSamples;
}

double AudioStats::rms() const {
    if (sampleCount == 0) return 0.0;
    return sqrt(static_cast<double>(sumSquares) / static_cast<double>(sampleCount));
}

double AudioStats::normalizedRms() const {
    return rms() / 32768.0;
}

double AudioStats::mean() const {
    if (sampleCount == 0) return 0.0;
    return static_cast<double>(sum) / static_cast<double>(sampleCount);
}

double AudioStats::normalizedMean() const {
    return mean() / 32768.0;
}

}  // namespace AudioDatasetRecorder