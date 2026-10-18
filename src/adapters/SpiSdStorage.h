#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_SPI_SD_STORAGE_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_SPI_SD_STORAGE_H

#include "FsStorageBase.h"

#if defined(ESP32)

#include <SD.h>

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * Storage over a microSD card on a dedicated SPI bus.
 *
 * This is the portable choice: SPI pins can be routed away from the I2S pins,
 * so it removes the conflict that forces the ESP32-CAM into
 * CaptureStrategy::FullBuffer. It works on ESP32 boards and on the smaller
 * chips that have SPI but no usable SDMMC host.
 *
 * Pass -1 for any pin to let the core pick its default. SD.begin() is
 * overloaded on the CS-only form, which uses the board's default VSPI pins.
 */
class SpiSdStorage : public FsStorageBase {
public:
    SpiSdStorage()
        : csPin_(-1), sckPin_(-1), misoPin_(-1), mosiPin_(-1), mounted_(false) {}

    void configure(int8_t cs, int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1) {
        csPin_ = cs;
        sckPin_ = sck;
        misoPin_ = miso;
        mosiPin_ = mosi;
    }

    Status begin() {
        if (mounted_) return Status::Ok;
        if (csPin_ < 0) return Status::InvalidConfig;

        mounted_ = (sckPin_ < 0 || misoPin_ < 0 || mosiPin_ < 0)
                       ? SD.begin(csPin_)
                       : SD.begin(csPin_, sckPin_, misoPin_, mosiPin_);
        if (!mounted_) return Status::StorageBeginFailed;
        if (SD.cardType() == CARD_NONE) {
            SD.end();
            mounted_ = false;
            return Status::StorageBeginFailed;
        }
        return Status::Ok;
    }

    void end() {
        if (!mounted_) return;
        SD.end();
        mounted_ = false;
    }

    const char *name() const { return "spi_sd"; }

protected:
    FS &backend() { return SD; }

private:
    int8_t csPin_;
    int8_t sckPin_;
    int8_t misoPin_;
    int8_t mosiPin_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_SPI_SD_STORAGE_H