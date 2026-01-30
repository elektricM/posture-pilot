#include "inference.h"
#include "config.h"
#include "model.h"

#include <TFLiteMicro_ArduinoESP32S3.h>

// Register ops used by our CNN
// Conv2D, MaxPool2D, Reshape, FullyConnected, Softmax, Mean (GlobalAvgPool),
// Quantize + Dequantize for INT8
static tflite::MicroMutableOpResolver<10> getResolver() {
    tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddReshape();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddRelu();
    resolver.AddMean();        // GlobalAveragePooling2D
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddDropout();
    return resolver;
}

bool inferenceSetup() {
    TFLMinterpreter = TFLMsetupModel<10, TENSOR_ARENA_SIZE>(
        posture_model, getResolver, true);

    if (!TFLMinterpreter) {
        Serial.println("Failed to set up TFLite model");
        return false;
    }

    // Log input/output tensor info
    Serial.printf("Input:  type=%d dims=[%d,%d,%d,%d]\n",
        TFLMinput->type,
        TFLMinput->dims->data[0], TFLMinput->dims->data[1],
        TFLMinput->dims->data[2], TFLMinput->dims->data[3]);
    Serial.printf("Output: type=%d dims=[%d,%d]\n",
        TFLMoutput->type,
        TFLMoutput->dims->data[0], TFLMoutput->dims->data[1]);

    return true;
}

// Resize grayscale frame to model input size using bilinear interpolation
// Writes INT8 quantized values directly to the input tensor
static void preprocessFrame(const uint8_t* src, int srcW, int srcH,
                            int8_t* dst, int dstW, int dstH,
                            float scale, int32_t zero_point) {
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

            // Bilinear interpolation
            float val = src[y0 * srcW + x0] * (1 - xFrac) * (1 - yFrac)
                      + src[y0 * srcW + x1] * xFrac * (1 - yFrac)
                      + src[y1 * srcW + x0] * (1 - xFrac) * yFrac
                      + src[y1 * srcW + x1] * xFrac * yFrac;

            // Normalize to [0,1] then quantize to int8
            float normalized = val / 255.0f;
            int32_t quantized = (int32_t)roundf(normalized / scale + zero_point);
            // Clamp to int8 range
            quantized = max((int32_t)-128, min((int32_t)127, quantized));
            dst[y * dstW + x] = (int8_t)quantized;
        }
    }
}

// Float version for non-quantized models
static void preprocessFrameFloat(const uint8_t* src, int srcW, int srcH,
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

    // Preprocess based on model input type (INT8 or float)
    if (TFLMinput->type == kTfLiteInt8) {
        float input_scale = TFLMinput->params.scale;
        int32_t input_zp = TFLMinput->params.zero_point;
        preprocessFrame(fb->buf, fb->width, fb->height,
                        TFLMinput->data.int8, MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT,
                        input_scale, input_zp);
    } else {
        preprocessFrameFloat(fb->buf, fb->width, fb->height,
                             TFLMinput->data.f, MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT);
    }

    if (!TFLMpredict()) {
        Serial.println("Inference failed");
        return result;
    }

    // Read output — handles both INT8 and float models
    float bad_conf, good_conf;

    if (TFLMoutput->type == kTfLiteInt8) {
        float output_scale = TFLMoutput->params.scale;
        int32_t output_zp = TFLMoutput->params.zero_point;
        bad_conf  = (TFLMoutput->data.int8[0] - output_zp) * output_scale;
        good_conf = (TFLMoutput->data.int8[1] - output_zp) * output_scale;
    } else {
        // Alphabetical class order: bad=0, good=1
        bad_conf  = TFLMoutput->data.f[0];
        good_conf = TFLMoutput->data.f[1];
    }

    result.confidence = bad_conf;
    result.isBadPosture = bad_conf > SLOUCH_THRESHOLD;
    result.inferenceTimeMs = millis() - start;

    #if DEBUG_MODE
    Serial.printf("Inference: good=%.2f bad=%.2f (%lums)\n",
                  good_conf, bad_conf, result.inferenceTimeMs);
    #endif

    return result;
}
