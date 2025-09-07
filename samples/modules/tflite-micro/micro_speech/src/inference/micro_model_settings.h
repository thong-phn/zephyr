#ifndef MICRO_SPEECH_OPENAMP_MODEL_SETTINGS_H_
#define MICRO_SPEECH_OPENAMP_MODEL_SETTINGS_H_

// The following values are derived from values used during model training.
// If you change the way you preprocess the input, update all these constants.
constexpr int kAudioSampleFrequency = 16000;
constexpr int kFeatureSize = 40;
constexpr int kFeatureCount = 49;
constexpr int kFeatureElementCount = (kFeatureSize * kFeatureCount);
constexpr int kFeatureStrideMs = 20;
constexpr int kFeatureDurationMs = 30;

// Variables for the model's output categories.
constexpr int kCategoryCount = 4;
constexpr const char* kCategoryLabels[kCategoryCount] = {
    "silence",
    "unknown",
    "yes",
    "no",
};

#endif  // MICRO_SPEECH_OPENAMP_MODEL_SETTINGS_H_
