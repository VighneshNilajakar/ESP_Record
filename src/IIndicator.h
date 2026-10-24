#ifndef AUDIO_DATASET_RECORDER_I_INDICATOR_H
#define AUDIO_DATASET_RECORDER_I_INDICATOR_H

#include "RecorderTypes.h"

namespace AudioDatasetRecorder {

/*
 * A visual status indicator (usually a GPIO LED).
 *
 * The core guarantees that allOff() is called on every exit path from a
 * recording, including failures, so a recorder can never leave an LED stuck on
 * and draining the board. setSuccess() is a momentary indication: implementations
 * must be inactive when it returns. A single pin cannot show idle and error
 * separately, so setIdle() and setError() both end inactive.
 */
class IIndicator {
public:
    virtual ~IIndicator() {}

    virtual Status begin() = 0;

    virtual void setIdle() = 0;
    virtual void setRecording() = 0;
    /* May blink or pulse; must end inactive. */
    virtual void setSuccess() = 0;
    virtual void setError() = 0;
    virtual void allOff() = 0;

    virtual const char *name() const = 0;
};

/* Does nothing. Useful for boards with no spare LED, and in host tests. */
class NullIndicator : public IIndicator {
public:
    Status begin() { return Status::Ok; }
    void setIdle() {}
    void setRecording() {}
    void setSuccess() {}
    void setError() {}
    void allOff() {}
    const char *name() const { return "none"; }
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_I_INDICATOR_H