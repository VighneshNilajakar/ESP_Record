#ifndef AUDIO_DATASET_RECORDER_I_STORAGE_BACKEND_H
#define AUDIO_DATASET_RECORDER_I_STORAGE_BACKEND_H

#include <stdint.h>
#include <stddef.h>

#include "RecorderTypes.h"

namespace AudioDatasetRecorder {

/* One entry of a directory listing. Fixed-size so it works without heap. */
struct DirEntry {
    static const size_t kNameCapacity = 64;

    char name[kNameCapacity];
    uint64_t size;
    bool isDirectory;
    bool nameTruncated;

    DirEntry() : size(0), isDirectory(false), nameTruncated(false) { name[0] = '\0'; }
};

/*
 * An open output file. write() must report exactly how many bytes it accepted
 * so the core can detect short writes instead of silently producing a WAV whose
 * header and payload disagree.
 */
class IFileWriter {
public:
    virtual ~IFileWriter() {}

    virtual Status write(const uint8_t *data, size_t length, size_t &written) = 0;
    virtual Status flush() = 0;
    virtual Status close() = 0;
};

class IFileWriterFactory {
public:
    virtual ~IFileWriterFactory() {}

    /*
     * Create a writer for `path`, truncating any existing file. Returns
     * StorageOpenFailed if the file cannot be created and leaves `out` null.
     * `partialPath` is the path of a scratch file to remove if the write later
     * fails verification, so a truncated WAV is never left looking complete.
     */
    virtual Status createWriter(const char *path, const char *partialPath, IFileWriter *&out) = 0;
};

class IStorageBackend : public IFileWriterFactory {
public:
    virtual ~IStorageBackend() {}

    /* Mount. Should be idempotent: a second call while mounted returns Ok. */
    virtual Status begin() = 0;

    /* Unmount. Must be safe when not mounted. */
    virtual void end() = 0;

    virtual bool isMounted() const = 0;

    virtual Status exists(const char *path, bool &out) = 0;

    virtual Status fileSize(const char *path, uint64_t &out) = 0;

    virtual Status remove(const char *path) = 0;

    /* List a directory. `count` is set to the number of entries stored, capped
     * at `capacity`; the caller can re-list with a larger buffer. */
    virtual Status listDir(const char *dir, DirEntry *out, size_t capacity, size_t &count) = 0;

    virtual uint64_t totalBytes() const = 0;
    virtual uint64_t freeBytes() const = 0;

    virtual const char *name() const = 0;
};

}  // namespace AudioDatasetRecorder

#endif  // AUDIO_DATASET_RECORDER_I_STORAGE_BACKEND_H