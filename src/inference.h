#ifndef INFERENCE_H
#define INFERENCE_H

#include <Arduino.h>
#include "esp_camera.h"

struct InferenceResult {
    float confidence;    // 0.0 = good posture, 1.0 = bad posture
    bool isBadPosture;   // confidence > SLOUCH_THRESHOLD
    unsigned long inferenceTimeMs;
};

// Initialize TFLite interpreter and load model
bool inferenceSetup();

// Run inference on a camera frame
// Handles preprocessing (resize, normalize) internally
InferenceResult runInference(camera_fb_t* fb);

#endif // INFERENCE_H
