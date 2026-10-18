#ifndef AUDIO_DATASET_RECORDER_RECORDER_TYPES_H
#define AUDIO_DATASET_RECORDER_RECORDER_TYPES_H

#include <stdint.h>
#include <stddef.h>

#include "AudioStats.h"

/*
 * Portable vocabulary shared by the recorder core, the board adapters and the
 * serial command console. Nothing in this header includes <Arduino.h> or any
 * Espressif header, so it compiles on a host as well as on a target.
 */
namespace AudioDatasetRecorder {

/*
 * Every fallible operation returns one of these. Adapters must map their
 * driver-specific failures onto a member of this list instead of exposing
 * driver error codes to the core.
 */
enum class Status : uint8_t {
    Ok = 0,
    InvalidConfig,
    InvalidLabel,
    Busy,
    NotMounted,
    NotSupported,
    AudioBeginFailed,
    AudioReadFailed,
    AudioTimeout,
    OutOfMemory,
    StorageBeginFailed,
    StorageOpenFailed,
    StorageWriteFailed,
    StorageCloseFailed,
    StorageSizeMismatch,
    InsufficientSpace,
    PendingClipExists,
    NoPendingClip,
    IndexExhausted,
    FileExists,
    Internal,
};

const char *toString(Status status);

/* Which I2S slot the microphone is wired to. Mirrors the L/R pin on INMP441. */
enum class SampleSlot : uint8_t {
    Left = 0,
    Right = 1,
    Both = 2,
};

/*
 * How the clip is held between capture and storage.
 *
 * FullBuffer  Allocate the whole clip up front (PSRAM preferred), then mount
 *             storage and write it. Required when the microphone and the
 *             storage bus share pins, because storage cannot be mounted while
 *             the audio peripheral owns those pins.
 * Streaming   Keep only a small ring of frames in RAM and write to storage as
 *             audio arrives. Needs a board where the audio pins and the
 *             storage bus do not conflict.
 */
enum class CaptureStrategy : uint8_t {
    FullBuffer = 0,
    Streaming = 1,
};

/*
 * Describes what an IAudioSource hands back to the recorder core.
 *
 * This is deliberately explicit. Arduino-ESP32 3.x can convert 32-bit I2S
 * words to PCM16 inside the driver (I2S_RX_TRANSFORM_32_TO_16). If a source
 * already did that conversion, applying the configured right shift again
 * would halve or quarter the signal. The core branches on this flag instead of
 * guessing.
 */
enum class SourceOutput : uint8_t {
    RawI2sWords = 0,  // signed 32-bit slot words; core applies `rightShift`
    Pcm16 = 1,        // already converted; core must not shift again
};

struct AudioFormat {
    uint32_t sampleRateHz;
    uint16_t channels;
    uint16_t bitsPerSample;

    AudioFormat() : sampleRateHz(16000), channels(1), bitsPerSample(16) {}
};

struct RecorderConfig {
    uint32_t sampleRateHz;
    uint16_t channels;          // 1 or 2
    uint32_t durationSeconds;
    SampleSlot slot;

    /*
     * Alignment between the microphone's raw I2S word and PCM16. Only used
     * when the audio source reports SourceOutput::RawI2sWords. Smaller shift
     * means more digital gain. 14 matches the working AI-Thinker ESP32-CAM +
     * INMP441 setup; it is a tuning parameter, not an INMP441 constant.
     */
    uint8_t rightShift;

    /*
     * Extra digital attenuation applied after the right shift. 0 = none.
     * Positive values reduce amplitude; values >= 15 force silence. Gain is
     * scaling only, never denoising.
     */
    uint8_t attenuationShift;

    CaptureStrategy strategy;

    /* Bytes kept free after allocating a full clip, to leave room for the stack. */
    uint32_t minFreeBytesMargin;

    /* Per-read blocking budget handed to the audio source. */
    uint32_t readTimeoutMs;

    /* Extra grace beyond durationSeconds before a capture is declared stuck. */
    uint32_t captureOverrunMs;

    /*
     * Keep a clip that came up short instead of discarding it. When true the
     * WAV header is written with the frame count actually captured, so the file
     * is self-consistent, and the reported duration is shorter than requested.
     * When false (the default, matching the ESP32-CAM reference behaviour) a
     * short capture is discarded and never written.
     */
    bool acceptTruncatedClip;

    /* Payload write chunk size. Smaller uses less peak RAM, larger is faster. */
    uint32_t writeChunkBytes;

    RecorderConfig()
        : sampleRateHz(16000),
          channels(1),
          durationSeconds(60),
          slot(SampleSlot::Left),
          rightShift(14),
          attenuationShift(0),
          strategy(CaptureStrategy::FullBuffer),
          minFreeBytesMargin(64u * 1024u),
          readTimeoutMs(1000),
          captureOverrunMs(8000),
          acceptTruncatedClip(false),
          writeChunkBytes(4096) {}
};

/* Result of one capture attempt. */
struct CaptureReport {
    Status status;
    uint64_t framesCaptured;   // frames per channel actually read
    uint64_t framesExpected;
    uint32_t elapsedMs;        // wall clock measured by the injected clock
    bool truncated;            // fewer frames than requested
    AudioStats audio;

    CaptureReport()
        : status(Status::Ok),
          framesCaptured(0),
          framesExpected(0),
          elapsedMs(0),
          truncated(false) {}
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_RECORDER_TYPES_H