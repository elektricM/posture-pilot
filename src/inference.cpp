#include "inference.h"
#include "config.h"
#include "model.h"

#include <MicroTFLite.h>

// Tensor arena — allocated statically
static byte tensorArena[TENSOR_ARENA_SIZE];

bool inferenceSetup() {
    // Check if model data is valid (not just the placeholder 0x00)
    if (posture_model_len <= 1) {
        Serial.println("No trained model found - flash a model or use COLLECT mode");
        return false;
    }

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

/**
 * Preprocess camera frame and load into model input tensor.
 * 
 * Performs bilinear interpolation to resize from camera resolution
 * (e.g., QVGA 320x240) down to model input size (96x96).
 * 
 * Bilinear interpolation smooths edges and reduces aliasing compared
 * to nearest-neighbor, improving model accuracy.
 * 
 * @param src Source grayscale image buffer
 * @param srcW Source image width
 * @param srcH Source image height
 * @param dstW Destination width (model input width)
 * @param dstH Destination height (model input height)
 */
static void preprocessAndLoad(const uint8_t* src, int srcW, int srcH,
                               int dstW, int dstH) {
    float xScale = (float)srcW / dstW;
    float yScale = (float)srcH / dstH;

    int idx = 0;
    for (int y = 0; y < dstH; y++) {
        // Map destination Y to source Y (float for subpixel accuracy)
        float srcY = y * yScale;
        int y0 = (int)srcY;
        int y1 = min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;  // Fractional part for interpolation

        for (int x = 0; x < dstW; x++) {
            // Map destination X to source X
            float srcX = x * xScale;
            int x0 = (int)srcX;
            int x1 = min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            // Bilinear interpolation: weighted average of 4 surrounding pixels
            // (x0,y0)  (x1,y0)
            //    +------+
            //    |  *   |  <- interpolated point (srcX, srcY)
            //    +------+
            // (x0,y1)  (x1,y1)
            float val = src[y0 * srcW + x0] * (1 - xFrac) * (1 - yFrac)
                      + src[y0 * srcW + x1] * xFrac * (1 - yFrac)
                      + src[y1 * srcW + x0] * (1 - xFrac) * yFrac
                      + src[y1 * srcW + x1] * xFrac * yFrac;

            // Normalize pixel value to [0,1] range expected by model
            // ModelSetInput handles INT8 quantization internally
            ModelSetInput(val / 255.0f, idx);
            idx++;
        }
    }
}

/**
 * Run TFLite inference on a camera frame.
 * 
 * Pipeline:
 *   1. Preprocess: resize camera frame to model input size (96x96) using
 *      bilinear interpolation and normalize to [0,1]
 *   2. Run TFLite inference (forward pass through CNN)
 *   3. Read output probabilities (INT8 quantized, automatically dequantized)
 *   4. Determine if slouching based on threshold
 * 
 * @param fb Camera frame buffer (must be grayscale format)
 * @return InferenceResult containing confidence and classification
 */
InferenceResult runInference(camera_fb_t* fb) {
    InferenceResult result = {0.0f, false, 0};

    if (!fb || !fb->buf) {
        return result;
    }

    unsigned long start = millis();

    // Preprocess: resize + normalize + load into input tensor
    preprocessAndLoad(fb->buf, fb->width, fb->height,
                      MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT);

    // Run inference (forward pass through the CNN)
    if (!ModelRunInference()) {
        Serial.println("Inference failed");
        return result;
    }

    // Read output - ModelGetOutput handles INT8 dequantization
    // Class order is alphabetical (training script sorts by folder name): bad=0, good=1
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
