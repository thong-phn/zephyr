#include "model_runner.hpp"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(model_runner, LOG_LEVEL_DBG);

#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#include "audio_preprocessor_int8_model.hpp"
#include "micro_speech_quantized_model.hpp"

#include "transport/rpmsg_transport.h" // For sending results back

#include <algorithm>
#include <stdio.h>
#include <string.h>

/* Features type for audio processing */
Features g_features;

namespace {

/* Arena size for model operations */
constexpr size_t kArenaSize = 28584;  /* xtensa p6 */
alignas(16) uint8_t g_arena[kArenaSize];

/* Audio sample constants */
constexpr int kAudioSampleDurationCount =
    kFeatureDurationMs * kAudioSampleFrequency / 1000;
constexpr int kAudioSampleStrideCount =
    kFeatureStrideMs * kAudioSampleFrequency / 1000;

/* Operation resolver types */
using MicroSpeechOpResolver = tflite::MicroMutableOpResolver<4>;
using AudioPreprocessorOpResolver = tflite::MicroMutableOpResolver<18>;

/**
 * @brief Register operations for micro speech model
 */
TfLiteStatus register_micro_speech_ops(MicroSpeechOpResolver& op_resolver) {
    TF_LITE_ENSURE_STATUS(op_resolver.AddReshape());
    TF_LITE_ENSURE_STATUS(op_resolver.AddFullyConnected());
    TF_LITE_ENSURE_STATUS(op_resolver.AddDepthwiseConv2D());
    TF_LITE_ENSURE_STATUS(op_resolver.AddSoftmax());
    return kTfLiteOk;
}

/**
 * @brief Register operations for audio preprocessor model
 */
TfLiteStatus register_ops(AudioPreprocessorOpResolver& op_resolver) {
    TF_LITE_ENSURE_STATUS(op_resolver.AddReshape());
    TF_LITE_ENSURE_STATUS(op_resolver.AddCast());
    TF_LITE_ENSURE_STATUS(op_resolver.AddStridedSlice());
    TF_LITE_ENSURE_STATUS(op_resolver.AddConcatenation());
    TF_LITE_ENSURE_STATUS(op_resolver.AddMul());
    TF_LITE_ENSURE_STATUS(op_resolver.AddAdd());
    TF_LITE_ENSURE_STATUS(op_resolver.AddDiv());
    TF_LITE_ENSURE_STATUS(op_resolver.AddMinimum());
    TF_LITE_ENSURE_STATUS(op_resolver.AddMaximum());
    TF_LITE_ENSURE_STATUS(op_resolver.AddWindow());
    TF_LITE_ENSURE_STATUS(op_resolver.AddFftAutoScale());
    TF_LITE_ENSURE_STATUS(op_resolver.AddRfft());
    TF_LITE_ENSURE_STATUS(op_resolver.AddEnergy());
    TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBank());
    TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBankSquareRoot());
    TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBankSpectralSubtraction());
    TF_LITE_ENSURE_STATUS(op_resolver.AddPCAN());
    TF_LITE_ENSURE_STATUS(op_resolver.AddFilterBankLog());
    return kTfLiteOk;
}

/**
 * @brief Generate a single feature from audio data
 */
TfLiteStatus generate_single_feature(const int16_t* audio_data,
                                    const int audio_data_size,
                                    int8_t* feature_output,
                                    tflite::MicroInterpreter* interpreter) {
    TfLiteTensor* input = interpreter->input(0);
    if (!input) {
        MicroPrintf("Failed to get input tensor");
        return kTfLiteError;
    }

    /* Check input shape is compatible with our audio sample size */
    if (audio_data_size != kAudioSampleDurationCount) {
        MicroPrintf("Audio data size mismatch: %d vs %d", audio_data_size, kAudioSampleDurationCount);
        return kTfLiteError;
    }

    TfLiteTensor* output = interpreter->output(0);
    if (!output) {
        MicroPrintf("Failed to get output tensor");
        return kTfLiteError;
    }

    /* Copy audio data to input tensor */
    std::copy_n(audio_data, audio_data_size, tflite::GetTensorData<int16_t>(input));

    /* Run inference */
    if (interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("Invoke failed");
        return kTfLiteError;
    }

    /* Copy output to feature buffer */
    std::copy_n(tflite::GetTensorData<int8_t>(output), kFeatureSize, feature_output);

    return kTfLiteOk;
}

/**
 * @brief Generate features from audio data
 */
TfLiteStatus generate_features(const int16_t* audio_data,
                            const size_t audio_data_size,
                            Features* features_output) {
    /* Load the audio preprocessing model */
    const tflite::Model* model = tflite::GetModel(g_audio_preprocessor_int8_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("Audio preprocessor model version mismatch: %d vs %d",
            model->version(), TFLITE_SCHEMA_VERSION);
        return kTfLiteError;
    }

    /* Set up operations */
    AudioPreprocessorOpResolver op_resolver;
    if (register_ops(op_resolver) != kTfLiteOk) {
        MicroPrintf("Failed to register audio preprocessor ops");
        return kTfLiteError;
    }

    /* Create interpreter */
    tflite::MicroInterpreter interpreter(model, op_resolver, g_arena, kArenaSize);

    /* Allocate tensors */
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        MicroPrintf("Failed to allocate tensors for audio preprocessor");
        return kTfLiteError;
    }

    MicroPrintf("AudioPreprocessor model arena size = %u", interpreter.arena_used_bytes());

    /* Process audio in stride windows */
    size_t remaining_samples = audio_data_size;
    size_t feature_index = 0;
    const int16_t* current_audio = audio_data;

    while (remaining_samples >= kAudioSampleDurationCount && feature_index < kFeatureCount) {
        if (generate_single_feature(current_audio, kAudioSampleDurationCount,
                    (*features_output)[feature_index], &interpreter) != kTfLiteOk) {
            MicroPrintf("Failed to generate feature %d", feature_index);
            return kTfLiteError;
        }

        feature_index++;
        current_audio += kAudioSampleStrideCount;
        remaining_samples -= kAudioSampleStrideCount;
    }

    return kTfLiteOk;
}

/**
 * @brief Run speech recognition on generated features
 */
TfLiteStatus run_micro_speech_inference(const Features& features) {
    /* Load the speech recognition model */
    const tflite::Model* model = tflite::GetModel(g_micro_speech_quantized_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        MicroPrintf("MicroSpeech model version mismatch: %d vs %d",
            model->version(), TFLITE_SCHEMA_VERSION);
        return kTfLiteError;
    }

    /* Set up operations */
    MicroSpeechOpResolver op_resolver;
    if (register_micro_speech_ops(op_resolver) != kTfLiteOk) {
        MicroPrintf("Failed to register MicroSpeech ops");
        return kTfLiteError;
    }

    /* Create interpreter */
    tflite::MicroInterpreter interpreter(model, op_resolver, g_arena, kArenaSize);

    /* Allocate tensors */
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        MicroPrintf("Failed to allocate tensors for MicroSpeech");
        return kTfLiteError;
    }

    MicroPrintf("MicroSpeech model arena size = %u", interpreter.arena_used_bytes());

    /* Get input tensor */
    TfLiteTensor* input = interpreter.input(0);
    if (!input) {
        MicroPrintf("Failed to get input tensor");
        return kTfLiteError;
    }

    /* Check input shape is compatible with our feature data size */
    if (input->dims->data[input->dims->size - 1] != kFeatureElementCount) {
        MicroPrintf("Input tensor size mismatch");
        return kTfLiteError;
    }

    TfLiteTensor* output = interpreter.output(0);
    if (!output) {
        MicroPrintf("Failed to get output tensor");
        return kTfLiteError;
    }

    /* Check output shape is compatible with our number of categories */
    if (output->dims->data[output->dims->size - 1] != kCategoryCount) {
        MicroPrintf("Output tensor size mismatch");
        return kTfLiteError;
    }

    float output_scale = output->params.scale;
    int output_zero_point = output->params.zero_point;

    /* Copy feature data to input tensor */
    std::copy_n(&features[0][0], kFeatureElementCount, tflite::GetTensorData<int8_t>(input));

    /* Run inference */
    if (interpreter.Invoke() != kTfLiteOk) {
        MicroPrintf("Invoke failed");
        return kTfLiteError;
    }

    /* Dequantize and print results */
    float category_predictions[kCategoryCount];
    MicroPrintf("MicroSpeech category predictions:");

    for (int i = 0; i < kCategoryCount; i++) {
        category_predictions[i] = (tflite::GetTensorData<int8_t>(output)[i] - output_zero_point) * output_scale;
        MicroPrintf("  %.4f %s", (double)category_predictions[i], kCategoryLabels[i]);
    }

    /* Find the prediction with highest confidence */
    int prediction_index = std::distance(
        std::begin(category_predictions),
        std::max_element(std::begin(category_predictions), std::end(category_predictions))
    );

    // Send prediction result through rpmsg
    static int8_t count = 0;
    char prediction_buff[256]; count++;
    snprintf(prediction_buff, sizeof(prediction_buff), "[Z] run_micro_speech_inference: %s; No. of predictions: %d \n", kCategoryLabels[prediction_index], count);
    if (tty_ept.addr != RPMSG_ADDR_ANY) {
        rpmsg_send(&tty_ept, prediction_buff, strlen(prediction_buff));
    }
    
    MicroPrintf("Detected: %s", kCategoryLabels[prediction_index]);

    return kTfLiteOk;
}

} /* namespace */

/* C API implementation */
extern "C" int micro_speech_process_audio(const int16_t *audio_data,
                                        size_t audio_data_size) {
    // [DEBUG] Print
    char debug_buff[256];
    snprintf(debug_buff, sizeof(debug_buff), "[Z] micro_speech_process_audio: audio_data_size: %zu\n", audio_data_size);
    
    // Send through rpmsg if TTY endpoint is available
    if (tty_ept.addr != RPMSG_ADDR_ANY) {
        rpmsg_send(&tty_ept, debug_buff, strlen(debug_buff));
    }
    /* Generate features */
    if (generate_features(audio_data, audio_data_size, &g_features) != kTfLiteOk) {
        MicroPrintf("Failed to generate features");
        return -1;
    }
    /* Run inference */
    if (run_micro_speech_inference(g_features) != kTfLiteOk) {
        MicroPrintf("Inference failed");
        return -2;
    }
    return 0;
}