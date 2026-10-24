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
 * setRecording() holds the LED on for the whole capture. setSuccess() pulses
 * it three times and leaves it off -- the controller calls it only after the
 * card has been unmounted, so toggling GPIO4 (SD D1 on the ESP32-CAM) cannot
 * disturb the bus. allOff() and setError() leave it off.
 *
 * Every write re-asserts pinMode(): SD_MMC reconfigures GPIO4 while the card is
 * mounted, and a plain digitalWrite() on the pin the SD driver left behind is
 * why a "success" LED could stay lit after SAVE.
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

    /* Momentary: three pulses, then off. Blocking for ~450 ms, which is fine
     * for a console command that has just finished a multi-second SD write. */
    void setSuccess() {
        if (!configured_) return;
        for (uint8_t i = 0; i < 3; ++i) {
            write(true);
            delay(80);
            write(false);
            delay(70);
        }
    }

    void setError() { write(false); }
    void allOff() { write(false); }

    const char *name() const { return configured_ ? "gpio" : "none"; }

private:
    uint8_t inactiveLevel() const { return activeLow_ ? HIGH : LOW; }

    void write(bool on) {
        if (!configured_) return;
        pinMode(pin_, OUTPUT);
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