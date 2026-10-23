#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_SD_MMC_STORAGE_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_SD_MMC_STORAGE_H

#include "FsStorageBase.h"

#if defined(ESP32)

#include <SD_MMC.h>

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * Storage over the on-board microSD socket driven by SD_MMC.
 *
 * This is the AI-Thinker ESP32-CAM reference: GPIO14/15 are shared between the
 * I2S microphone and the SD_MMC bus, so the card must not be mounted while the
 * audio peripheral owns those pins. Use it with CaptureStrategy::FullBuffer,
 * which releases I2S before mounting.
 *
 * Not available on every ESP chip. Chips without an SDMMC controller, and any
 * board whose exposed pins cannot form a usable 1-bit bus, must use SpiSdStorage
 * instead.
 */
class SdMmcStorage : public FsStorageBase {
public:
    SdMmcStorage() : mountpoint_("/sdcard"), oneBit_(true), frequencyKhz_(0) {}

    /*
     * mountpoint must outlive this object.
     *
     * frequencyKhz is the SDMMC clock in kHz; 0 keeps the board default
     * (BOARD_MAX_SDMMC_FREQ, 20 MHz on classic ESP32). Lowering it reduces
     * peak current and can rescue a marginal 3.3 V rail or a long cable, at
     * the cost of write throughput. This is a genuine trade-off, not a way to
     * paper over a brownout: if the rail collapses even at 4 MHz, the supply
     * needs bulk capacitance.
     */
    void configure(const char *mountpoint, bool oneBit, int frequencyKhz = 0) {
        mountpoint_ = mountpoint;
        oneBit_ = oneBit;
        frequencyKhz_ = frequencyKhz;
    }

    Status begin() {
        if (mounted_) return Status::Ok;
        mounted_ = frequencyKhz_ > 0
                       ? SD_MMC.begin(mountpoint_, oneBit_, false, frequencyKhz_)
                       : SD_MMC.begin(mountpoint_, oneBit_);
        if (!mounted_) return Status::StorageBeginFailed;
        /* A successful begin() with no card means the bus is unusable, not
         * that the card is empty. */
        if (SD_MMC.cardType() == CARD_NONE) {
            SD_MMC.end();
            mounted_ = false;
            return Status::StorageBeginFailed;
        }
        return Status::Ok;
    }

    void end() {
        if (!mounted_) return;
        SD_MMC.end();
        mounted_ = false;
    }

    const char *name() const { return oneBit_ ? "sd_mmc-1bit" : "sd_mmc-4bit"; }

protected:
    FS &backend() const { return SD_MMC; }
    uint64_t backendTotalBytes() const { return SD_MMC.totalBytes(); }
    uint64_t backendUsedBytes() const { return SD_MMC.usedBytes(); }

private:
    const char *mountpoint_;
    bool oneBit_;
    int frequencyKhz_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_SD_MMC_STORAGE_H