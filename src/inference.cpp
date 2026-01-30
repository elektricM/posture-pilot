#include "inference.h"
#include "config.h"
#include "model.h"

#include <TFLiteMicro_ArduinoESP32S3.h>

bool inferenceSetup() {
    // Set up model using the library's helper
    // It handles arena allocation, interpreter creation, etc.
    TFLMinterpreter = TFLMsetupModel(posture_model, TFLMgetResolver);

    if (!TFLMinterpreter) {
        Serial.println("Failed to set up TFLite model");
        return false;
    }

    Serial.println("TFLite model loaded");
    return true;
}

// Bilinear resize grayscale image into float buffer
static void preprocessFrame(const uint8_t* src, int srcW, int srcH,
                            float* dst, int dstW, int dstH) {
    float xScale = (float)srcW / dstW;
    float yScale = (float)srcH / dstH;

    for (int y = 0; y < dstH; y++) {
        float srcY = y * yScale;
        int y0 = (int)srcY;
        int y1 = min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < dstW; x++) {
            float srcX = x * xScale;
            int x0 = (int)srcX;
            int x1 = min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            float val = src[y0 * srcW + x0] * (1 - xFrac) * (1 - yFrac)
                      + src[y0 * srcW + x1] * xFrac * (1 - yFrac)
                      + src[y1 * srcW + x0] * (1 - xFrac) * yFrac
                      + src[y1 * srcW + x1] * xFrac * yFrac;

            dst[y * dstW + x] = val / 255.0f;
        }
    }
}

InferenceResult runInference(camera_fb_t* fb) {
    InferenceResult result = {0.0f, false, 0};

    if (!TFLMinterpreter || !fb || !fb->buf) {
        return result;
    }

    unsigned long start = millis();

    // Preprocess: resize frame to model input and normalize
    preprocessFrame(fb->buf, fb->width, fb->height,
                    TFLMinput->data.f, MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT);

    // Run inference
    if (!TFLMpredict()) {
        Serial.println("Inference failed");
        return result;
    }

    // Output order depends on alphabetical class sorting in training:
    // image_dataset_from_directory sorts: bad=0, good=1
    float bad_conf = TFLMoutput->data.f[0];
    float good_conf = TFLMoutput->data.f[1];

    result.confidence = bad_conf;
    result.isBadPosture = bad_conf > SLOUCH_THRESHOLD;
    result.inferenceTimeMs = millis() - start;

    #if DEBUG_MODE
    Serial.printf("Inference: good=%.2f bad=%.2f (%lums)\n",
                  good_conf, bad_conf, result.inferenceTimeMs);
    #endif

    return result;
}
