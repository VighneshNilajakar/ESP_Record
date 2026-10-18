#include "FsStorageBase.h"

#if defined(ESP32)

#include <new>
#include <string.h>

namespace AudioDatasetRecorder {
namespace adapters {

FsFileWriter::FsFileWriter(File *file) : file_(file), open_(file != nullptr) {}

FsFileWriter::~FsFileWriter() {
    if (file_ == nullptr) return;
    if (open_) file_->close();
    delete file_;
}

Status FsFileWriter::write(const uint8_t *data, size_t length, size_t &written) {
    written = 0;
    if (!open_ || file_ == nullptr) return Status::StorageWriteFailed;
    if (data == nullptr && length > 0) return Status::InvalidConfig;
    if (length == 0) return Status::Ok;

    /* Ask for an exact size and compare, so a short write is reported instead
     * of silently producing a WAV whose payload is shorter than its header
     * promises. */
    const size_t accepted = file_->write(data, length);
    written = accepted;
    if (accepted != length) return Status::StorageWriteFailed;
    return Status::Ok;
}

Status FsFileWriter::flush() {
    if (!open_ || file_ == nullptr) return Status::StorageCloseFailed;
    file_->flush();
    return Status::Ok;
}

Status FsFileWriter::close() {
    if (file_ == nullptr) return Status::Ok;
    if (open_) {
        file_->close();
        open_ = false;
    }
    return Status::Ok;
}

Status FsStorageBase::requireMounted() const {
    if (!mounted_) return Status::NotMounted;
    return Status::Ok;
}

Status FsStorageBase::exists(const char *path, bool &out) {
    out = false;
    if (path == nullptr) return Status::InvalidConfig;
    const Status guard = requireMounted();
    if (guard != Status::Ok) return guard;

    FS &fs = backend();

    File probe = fs.open(path, FILE_READ);
    if (probe) {
        probe.close();
        out = true;
        return Status::Ok;
    }

    /* Not readable: it may still exist as a directory. */
    File asDirectory = fs.open(path);
    if (asDirectory) {
        const bool isDir = asDirectory.isDirectory();
        asDirectory.close();
        if (isDir) out = true;
    }
    return Status::Ok;
}

Status FsStorageBase::fileSize(const char *path, uint64_t &out) {
    out = 0;
    if (path == nullptr) return Status::InvalidConfig;
    const Status guard = requireMounted();
    if (guard != Status::Ok) return guard;

    File file = backend().open(path, FILE_READ);
    if (!file) return Status::StorageOpenFailed;
    out = static_cast<uint64_t>(file.size());
    file.close();
    return Status::Ok;
}

Status FsStorageBase::remove(const char *path) {
    if (path == nullptr) return Status::InvalidConfig;
    const Status guard = requireMounted();
    if (guard != Status::Ok) return guard;
    return backend().remove(path) ? Status::Ok : Status::StorageOpenFailed;
}

Status FsStorageBase::listDir(const char *dir, DirEntry *out, size_t capacity, size_t &count) {
    count = 0;
    if (out == nullptr) return Status::InvalidConfig;
    const Status guard = requireMounted();
    if (guard != Status::Ok) return guard;

    File handle = backend().open(dir == nullptr ? "/" : dir);
    if (!handle) return Status::StorageOpenFailed;
    if (!handle.isDirectory()) {
        handle.close();
        return Status::InvalidConfig;
    }

    for (File entry = handle.openNextFile(); entry; entry = handle.openNextFile()) {
        if (count >= capacity) break;

        DirEntry &slot = out[count];
        slot.isDirectory = entry.isDirectory();
        slot.size = static_cast<uint64_t>(entry.size());

        const char *name = entry.name();
        if (name == nullptr) {
            slot.name[0] = '\0';
            slot.nameTruncated = true;
        } else {
            const size_t length = strlen(name);
            slot.nameTruncated = length >= DirEntry::kNameCapacity;
            const size_t copy = length < DirEntry::kNameCapacity ? length : DirEntry::kNameCapacity - 1;
            memcpy(slot.name, name, copy);
            slot.name[copy] = '\0';
        }

        ++count;
    }

    handle.close();
    return Status::Ok;
}

Status FsStorageBase::createWriter(const char *path, const char *partialPath, IFileWriter *&out) {
    (void)partialPath;  /* the controller owns partial-file removal */
    out = nullptr;
    if (path == nullptr) return Status::InvalidConfig;
    const Status guard = requireMounted();
    if (guard != Status::Ok) return guard;

    File *file = new (std::nothrow) File(backend().open(path, FILE_WRITE));
    if (file == nullptr) return Status::Internal;
    if (!*file) {
        delete file;
        return Status::StorageOpenFailed;
    }

    FsFileWriter *writer = new (std::nothrow) FsFileWriter(file);
    if (writer == nullptr) {
        file->close();
        delete file;
        return Status::Internal;
    }

    out = writer;
    return Status::Ok;
}

uint64_t FsStorageBase::totalBytes() const {
    if (!mounted_) return 0;
    return static_cast<uint64_t>(backend().totalBytes());
}

uint64_t FsStorageBase::freeBytes() const {
    if (!mounted_) return 0;
    return static_cast<uint64_t>(backend().freeBytes());
}

}  // namespace adapters
}  // namespace AudioDatasetRecorder

#endif  // ESP32