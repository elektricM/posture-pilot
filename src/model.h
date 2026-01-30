#ifndef MODEL_H
#define MODEL_H

// Placeholder model data.
// Replace this with the output of scripts/train_model.py
// which converts a trained TFLite model to a C byte array.
//
// The model expects:
//   Input:  96x96x1 grayscale image, float32 normalized [0, 1]
//   Output: 2 floats [bad_confidence, good_confidence] (alphabetical class order)

alignas(16) const unsigned char posture_model[] = {
    // TFLite flatbuffer placeholder
    // Run scripts/train_model.py to generate real model data
    0x00
};

const unsigned int posture_model_len = sizeof(posture_model);

#endif // MODEL_H
