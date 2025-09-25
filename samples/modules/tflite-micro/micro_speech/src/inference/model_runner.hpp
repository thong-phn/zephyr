#ifndef MICRO_SPEECH_OPENAMP_MODEL_RUNNER_H_
#define MICRO_SPEECH_OPENAMP_MODEL_RUNNER_H_

#include <cstddef>
#include <cstdint>
#include "micro_model_settings.h"

using Features = int8_t[kFeatureCount][kFeatureSize];

extern Features g_features;

extern "C" {
void model_runner_init(void);
int micro_speech_process_audio(const int16_t *audio_data, size_t audio_data_size);
}

#endif /* MICRO_SPEECH_OPENAMP_MODEL_RUNNER_H_ */