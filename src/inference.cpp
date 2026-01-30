#include "inference.h"
#include "config.h"
#include "model.h"

#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>

// Tensor arena in PSRAM for larger models
static uint8_t* tensor_arena = nullptr;

static const tflite::Model* model = nullptr;
static tflite::MicroInterpreter* interpreter = nullptr;
static TfLiteTensor* input_tensor = nullptr;
static TfLiteTensor* output_tensor = nullptr;

bool inferenceSetup() {
    // Allocate tensor arena in PSRAM
    tensor_arena = (uint8_t*)ps_malloc(TENSOR_ARENA_SIZE);
    if (!tensor_arena) {
        Serial.println("Failed to allocate tensor arena in PSRAM");
        return false;
    }

    // Load model
    model = tflite::GetModel(posture_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("Model schema version %lu != supported %d\n",
                      model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    // Register ops used by the model
    // Using MicroMutableOpResolver to only include needed ops (saves memory)
    static tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddReshape();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddRelu();
    resolver.AddDepthwiseConv2D();
    resolver.AddAveragePool2D();
    resolver.AddQuantize();
    resolver.AddDequantize();

    // Build interpreter
    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, TENSOR_ARENA_SIZE);
    interpreter = &static_interpreter;

    // Allocate tensors
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("AllocateTensors() failed");
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    Serial.printf("Model loaded. Input: [%d, %d, %d, %d] Output: [%d, %d]\n",
                  input_tensor->dims->data[0], input_tensor->dims->data[1],
                  input_tensor->dims->data[2], input_tensor->dims->data[3],
                  output_tensor->dims->data[0], output_tensor->dims->data[1]);
    Serial.printf("Tensor arena: %d bytes used of %d\n",
                  interpreter->arena_used_bytes(), TENSOR_ARENA_SIZE);

    return true;
}

// Bilinear resize grayscale image into model input tensor
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

            // Bilinear interpolation
            float val = src[y0 * srcW + x0] * (1 - xFrac) * (1 - yFrac)
                      + src[y0 * srcW + x1] * xFrac * (1 - yFrac)
                      + src[y1 * srcW + x0] * (1 - xFrac) * yFrac
                      + src[y1 * srcW + x1] * xFrac * yFrac;

            // Normalize to [0, 1]
            dst[y * dstW + x] = val / 255.0f;
        }
    }
}

InferenceResult runInference(camera_fb_t* fb) {
    InferenceResult result = {0.0f, false, 0};

    if (!interpreter || !fb || !fb->buf) {
        return result;
    }

    unsigned long start = millis();

    // Preprocess: resize frame to model input and normalize
    float* input_data = input_tensor->data.f;
    preprocessFrame(fb->buf, fb->width, fb->height,
                    input_data, MODEL_INPUT_WIDTH, MODEL_INPUT_HEIGHT);

    // Run inference
    if (interpreter->Invoke() != kTfLiteOk) {
        Serial.println("Invoke() failed");
        return result;
    }

    // Output order depends on alphabetical class sorting in training:
    // image_dataset_from_directory sorts: bad=0, good=1
    float bad_conf = output_tensor->data.f[0];
    float good_conf = output_tensor->data.f[1];

    result.confidence = bad_conf;
    result.isBadPosture = bad_conf > SLOUCH_THRESHOLD;
    result.inferenceTimeMs = millis() - start;

    #if DEBUG_MODE
    Serial.printf("Inference: good=%.2f bad=%.2f (%lums)\n",
                  good_conf, bad_conf, result.inferenceTimeMs);
    #endif

    return result;
}
