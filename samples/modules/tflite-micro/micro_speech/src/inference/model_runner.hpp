#ifndef MICRO_SPEECH_OPENAMP_MODEL_RUNNER_H_
#define MICRO_SPEECH_OPENAMP_MODEL_RUNNER_H_

#include <cstddef>
#include <cstdint>
#include "micro_model_settings.h"

// Define the Features type to be accessible by other files
using Features = int8_t[kFeatureCount][kFeatureSize];

// Declare g_features as an external variable
extern Features g_features;

// The public API for running audio processing
extern "C" {
int micro_speech_process_audio(const int16_t *audio_data, size_t audio_data_size);
}

#endif /* MICRO_SPEECH_OPENAMP_MODEL_RUNNER_H_ */