#include "AudioDatasetRecorder.h"

/*
 * This translation unit exists so that "AudioDatasetRecorder.h" is a real
 * library source file with an object code artefact to link. Every declaration
 * it pulls in has its definition in the correspondingly named .cpp file:
 *
 *   AudioStats         -> AudioStats.cpp
 *   PcmConvert         -> PcmConvert.cpp
 *   WavWriter          -> WavWriter.cpp
 *   Labels             -> Labels.cpp
 *   RecorderTypes      -> RecorderTypes.cpp
 *   Services           -> Services.cpp
 *   RecorderController -> RecorderController.cpp
 *
 * The interfaces are header-only by design: they have no state beyond their
 * virtual destructors, so there is nothing for a .cpp file to emit.
 */