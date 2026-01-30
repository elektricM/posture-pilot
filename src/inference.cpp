#include "inference.h"
#include "config.h"
#include "model.h"

#include <MicroTFLite.h>

// Tensor arena — allocated statically
static byte tensorArena[TENSOR_ARENA_SIZE];

bool inferenceSetup() {
    bool ok = ModelInit(posture_model, tensorArena, TENSOR_ARENA_SIZE);

    if (!ok) {
        Serial.println("Failed to initialize TFLite model");
        return false;
    }

    Serial.println("TFLite model loaded");
    ModelPrintInputTensorDimensions();
    ModelPrintOutputTensorDimensions();
    ModelPrintTensorQuantizationParams();

    return true;
}

// Bilinear resize grayscale frame to model input size
static void preprocessAndLoad(const uint8_t* src, int srcW, int srcH,
                               int dstW, int dstH) {
    float xScale = (float)srcW / dstW;
    float yScale = (float)srcH / dstH;

    int idx = 0;
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

            // Bilinear interpolation
            float val = src[y0 * srcW + x0] * (1 - xFrac) * (1 - yFrac)
                      + src[y0 * srcW + x1] * xFrac * (1 - yFrac)
                      + src[y1 * srcW + x0] * (1 - xFrac) * yFrac
                      + src[y1 * srcW + x1] * xFrac * yFrac;

            // Normalize to [0,1] and set input
            // ModelSetInput handles quantization internally
            ModelSetInput(val / 255.0f, idx);
            idx++;
        }
    }
}

InferenceResult runInference(camera_fb_t* fb) {
    InferenceResult result = {0.0f, false, 0};

    if (!fb || !fb->buf) {
        return result;
    }

    unsigned long start = millis();

    // Preprocess: resize + normalize + load into input tensor
    preprocessAndLoad(fb->buf, fb->width, fb->height,
                      MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT);

    // Run inference
    if (!ModelRunInference()) {
        Serial.println("Inference failed");
        return result;
    }

    // Read output - ModelGetOutput handles dequantization
    // Class order (alphabetical): bad=0, good=1
    float bad_conf  = ModelGetOutput(0);
    float good_conf = ModelGetOutput(1);

    result.confidence = bad_conf;
    result.isBadPosture = bad_conf > SLOUCH_THRESHOLD;
    result.inferenceTimeMs = millis() - start;

    #if DEBUG_MODE
    Serial.printf("Inference: good=%.2f bad=%.2f (%lums)\n",
                  good_conf, bad_conf, result.inferenceTimeMs);
    #endif

    return result;
}
