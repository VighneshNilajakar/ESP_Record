#include "Services.h"

#include <stdlib.h>

namespace AudioDatasetRecorder {

void *SystemAllocator::allocate(size_t bytes) {
    if (bytes == 0) return nullptr;
    return malloc(bytes);
}

void SystemAllocator::release(void *pointer) {
    if (pointer != nullptr) free(pointer);
}

size_t SystemAllocator::largestFreeBlock() const {
    /* Deliberately conservative. The core treats this as an estimate and still
     * checks the allocate() result; reporting a guess here never causes an
     * out-of-bounds write, it only affects whether we try and fail early. */
    return 32768;
}

}  // namespace AudioDatasetRecorder