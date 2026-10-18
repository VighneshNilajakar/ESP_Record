#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_GPIO_INDICATOR_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_GPIO_INDICATOR_H

#include <Arduino.h>

#include "../IIndicator.h"

#if defined(ESP32)

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * Single-GPIO status LED.
 *
 * `activeLevel` is configurable because the ESP32-CAM flash LED on GPIO4 is
 * active high, while several dev boards have an active-low LED. Getting this
 * wrong inverts every state, so it is an explicit parameter rather than an
 * assumption.
 *
 * allOff() drives the pin to the inactive level, which is also the
 * low-power state, and is called by the controller on every exit path.
 *
 * A single pin can only show two states, so idle and error look identical and
 * success looks the same as recording. That is a property of one-wire LEDs, not
 * a bug; if the states must be distinguishable, drive separate LEDs or a
 * NeoPixel from a custom IIndicator.
 */
class GpioIndicator : public IIndicator {
public:
    GpioIndicator() : pin_(-1), activeLow_(false), configured_(false) {}

    Status begin() { return begin(2, true, false); }

    Status begin(int8_t pin, bool activeHigh, bool enablePullUp) {
        pin_ = pin;
        activeLow_ = !activeHigh;
        configured_ = pin_ >= 0;
        if (!configured_) return Status::InvalidConfig;

        pinMode(pin_, OUTPUT);
        digitalWrite(pin_, inactiveLevel());
        (void)enablePullUp;  /* a push-pull output does not use a pull-up */
        return Status::Ok;
    }

    void setIdle() { write(false); }
    void setRecording() { write(true); }
    void setSuccess() { write(true); }
    void setError() { write(false); }
    void allOff() { write(false); }

    const char *name() const { return configured_ ? "gpio" : "none"; }

private:
    uint8_t inactiveLevel() const { return activeLow_ ? HIGH : LOW; }

    void write(bool on) {
        if (!configured_) return;
        digitalWrite(pin_, on ? (activeLow_ ? LOW : HIGH) : inactiveLevel());
    }

    int8_t pin_;
    bool activeLow_;
    bool configured_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_GPIO_INDICATOR_H