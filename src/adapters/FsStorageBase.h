#ifndef AUDIO_DATASET_RECORDER_ADAPTERS_FS_STORAGE_H
#define AUDIO_DATASET_RECORDER_ADAPTERS_FS_STORAGE_H

#include "../IStorageBackend.h"

#if defined(ESP32)

#include <Arduino.h>
#include <FS.h>

namespace AudioDatasetRecorder {
namespace adapters {

/*
 * Writer for one open file. Owns its File handle and releases it in the
 * destructor, so a caller that forgets to close() still cannot leak the handle
 * or leave the file open. The controller always deletes the writer.
 */
class FsFileWriter : public IFileWriter {
public:
    explicit FsFileWriter(File *file);
    ~FsFileWriter();

    Status write(const uint8_t *data, size_t length, size_t &written);
    Status flush();
    Status close();

private:
    FsFileWriter(const FsFileWriter &);
    FsFileWriter &operator=(const FsFileWriter &);

    File *file_;
    bool open_;
};

/*
 * Shared implementation over Arduino's filesystem API.
 *
 * Both SD_MMC and the SPI SD library expose an fs::FS-compatible object, so
 * every operation except mounting is written once here. Only mount/unmount
 * differ, and those live in the subclasses because the signatures genuinely
 * differ between the two buses and between core 2.x and 3.x.
 *
 * Paths are expected to be rooted, e.g. "/noise.001.wav".
 */
class FsStorageBase : public IStorageBackend {
public:
    Status exists(const char *path, bool &out);
    Status fileSize(const char *path, uint64_t &out);
    Status remove(const char *path);
    Status listDir(const char *dir, DirEntry *out, size_t capacity, size_t &count);
    Status createWriter(const char *path, const char *partialPath, IFileWriter *&out);

    uint64_t totalBytes() const;
    uint64_t freeBytes() const;

    bool isMounted() const { return mounted_; }

protected:
    explicit FsStorageBase() : mounted_(false) {}

    /* Returns a mutable reference even from a const object: the underlying
     * filesystem operations (open, exists, remove) are non-const, and a storage
     * backend that could not mutate through a const handle would be useless. */
    virtual FS &backend() const = 0;

    /*
     * Capacity is asked of the concrete bus rather than of fs::FS.
     *
     * Core 2.x does not declare totalBytes()/usedBytes() on fs::FS at all; they
     * exist only on SDMMCFS and SDFS. Core 3.x does declare them on FS. Routing
     * through the subclass is the only form that compiles against both.
     */
    virtual uint64_t backendTotalBytes() const = 0;
    virtual uint64_t backendUsedBytes() const = 0;

    /* Guard used by every operation so a caller that skipped begin() gets a
     * clear error instead of a filesystem-level fault. */
    Status requireMounted() const;

    bool mounted_;
};

}  // namespace adapters
}  // namespace AudioDatasetRecorder
#endif  // ESP32

#endif  // AUDIO_DATASET_RECORDER_ADAPTERS_FS_STORAGE_H