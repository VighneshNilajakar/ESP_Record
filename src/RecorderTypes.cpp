#include "RecorderTypes.h"

namespace AudioDatasetRecorder {

const char *toString(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::InvalidConfig: return "INVALID_CONFIG";
        case Status::InvalidLabel: return "INVALID_LABEL";
        case Status::Busy: return "BUSY";
        case Status::NotMounted: return "NOT_MOUNTED";
        case Status::NotSupported: return "NOT_SUPPORTED";
        case Status::AudioBeginFailed: return "AUDIO_BEGIN_FAILED";
        case Status::AudioReadFailed: return "AUDIO_READ_FAILED";
        case Status::AudioTimeout: return "AUDIO_TIMEOUT";
        case Status::OutOfMemory: return "OUT_OF_MEMORY";
        case Status::StorageBeginFailed: return "STORAGE_BEGIN_FAILED";
        case Status::StorageOpenFailed: return "STORAGE_OPEN_FAILED";
        case Status::StorageWriteFailed: return "STORAGE_WRITE_FAILED";
        case Status::StorageCloseFailed: return "STORAGE_CLOSE_FAILED";
        case Status::StorageSizeMismatch: return "STORAGE_SIZE_MISMATCH";
        case Status::InsufficientSpace: return "INSUFFICIENT_SPACE";
        case Status::PendingClipExists: return "PENDING_CLIP_EXISTS";
        case Status::NoPendingClip: return "NO_PENDING_CLIP";
        case Status::IndexExhausted: return "INDEX_EXHAUSTED";
        case Status::FileExists: return "FILE_EXISTS";
        case Status::Internal: return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

}  // namespace AudioDatasetRecorder