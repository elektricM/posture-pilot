#!/usr/bin/env python3
"""
PosturePilot Model Training

Trains a posture classifier (good vs bad) and exports to TFLite + C header
for the ESP32 firmware.

Usage:
    python train_model.py --data ./data --output ../src/model.h

Data structure:
    data/
      good/    <- images of good posture
      bad/     <- images of bad posture
"""

import argparse
import os
import sys
import numpy as np
from pathlib import Path

os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"

import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

IMG_WIDTH = 96
IMG_HEIGHT = 96
BATCH_SIZE = 32
EPOCHS_DEFAULT = 30


def load_dataset(data_dir: str, validation_split: float = 0.2):
    """Load images from good/ and bad/ subdirectories."""
    data_path = Path(data_dir)

    if not (data_path / "good").exists() or not (data_path / "bad").exists():
        print(f"Error: Expected {data_path}/good/ and {data_path}/bad/ directories")
        sys.exit(1)

    train_ds = keras.utils.image_dataset_from_directory(
        data_path,
        validation_split=validation_split,
        subset="training",
        seed=42,
        image_size=(IMG_HEIGHT, IMG_WIDTH),
        batch_size=BATCH_SIZE,
        color_mode="grayscale",
        label_mode="categorical",
    )

    val_ds = keras.utils.image_dataset_from_directory(
        data_path,
        validation_split=validation_split,
        subset="validation",
        seed=42,
        image_size=(IMG_HEIGHT, IMG_WIDTH),
        batch_size=BATCH_SIZE,
        color_mode="grayscale",
        label_mode="categorical",
    )

    class_names = train_ds.class_names
    print(f"Classes: {class_names}")
    print(f"Training batches: {len(train_ds)}")
    print(f"Validation batches: {len(val_ds)}")

    # Normalize to [0, 1]
    norm = layers.Rescaling(1.0 / 255)
    train_ds = train_ds.map(lambda x, y: (norm(x), y))
    val_ds = val_ds.map(lambda x, y: (norm(x), y))

    train_ds = train_ds.cache().prefetch(buffer_size=tf.data.AUTOTUNE)
    val_ds = val_ds.cache().prefetch(buffer_size=tf.data.AUTOTUNE)

    return train_ds, val_ds, class_names


def build_model():
    """
    CNN for 96x96 binary classification on ESP32.

    Architecture based on what works in practice for ESP32 TFLite:
    - 5x5 first kernel to capture spatial features early
    - 3 conv blocks with increasing filters (32 -> 64 -> 64)
    - Dropout between blocks to prevent overfitting on small datasets
    - GlobalAveragePooling instead of Flatten (way fewer params)
    - INT8 quantization friendly (no fancy ops)
    """
    model = keras.Sequential([
        layers.Input(shape=(IMG_HEIGHT, IMG_WIDTH, 1)),

        # Data augmentation (only during training)
        layers.RandomFlip("horizontal"),
        layers.RandomRotation(0.05),
        layers.RandomBrightness(0.1),

        # Block 1: 5x5 conv to capture larger spatial patterns
        layers.Conv2D(32, (5, 5), padding="same", activation="relu"),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.2),

        # Block 2
        layers.Conv2D(64, (3, 3), padding="same", activation="relu"),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.2),

        # Block 3
        layers.Conv2D(64, (3, 3), padding="same", activation="relu"),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.2),

        # Classifier
        layers.GlobalAveragePooling2D(),
        layers.Dense(128, activation="relu"),
        layers.Dropout(0.3),
        layers.Dense(2, activation="softmax"),
    ])

    model.compile(
        optimizer="adam",
        loss="categorical_crossentropy",
        metrics=["accuracy"],
    )

    return model


def convert_to_tflite(model, train_ds, output_path: str):
    """Convert to fully quantized INT8 TFLite model.

    INT8 is much faster on ESP32 than float - no FPU needed,
    and the model is ~4x smaller.
    """
    # Representative dataset for INT8 calibration
    def representative_data():
        for images, _ in train_ds.take(50):
            for i in range(min(5, len(images))):
                yield [tf.expand_dims(images[i], 0)]

    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    # Full INT8 quantization
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_data
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    tflite_model = converter.convert()

    tflite_path = output_path.replace(".h", ".tflite")
    with open(tflite_path, "wb") as f:
        f.write(tflite_model)

    print(f"TFLite model: {tflite_path} ({len(tflite_model)} bytes)")
    return tflite_model


def convert_to_header(tflite_model: bytes, output_path: str):
    """Convert TFLite model to C header for firmware embedding."""
    hex_lines = []
    for i in range(0, len(tflite_model), 12):
        chunk = tflite_model[i:i + 12]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        hex_lines.append(f"    {hex_str},")

    from datetime import datetime
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    header = f"""#ifndef MODEL_H
#define MODEL_H

// ============================================
// PosturePilot TFLite Model
// ============================================
//
// Generated by scripts/train_model.py
// Timestamp: {timestamp}
// Model size: {len(tflite_model)} bytes ({len(tflite_model) / 1024:.1f} KB)
// Quantization: INT8 (full integer)
//
// Input:  96x96x1 grayscale, int8 (quantized)
// Output: 2 x int8 [bad_confidence, good_confidence]
//
// Class order (alphabetical): bad=0, good=1
//
// This model was trained on your custom posture data.
// To retrain: cd scripts && python train_model.py --data ./data
//
// ============================================

alignas(16) const unsigned char posture_model[] = {{
{chr(10).join(hex_lines)}
}};

const unsigned int posture_model_len = {len(tflite_model)};

#endif // MODEL_H
"""

    with open(output_path, "w") as f:
        f.write(header)

    print(f"C header: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Train PosturePilot posture classifier")
    parser.add_argument("--data", type=str, default="./data",
                        help="Path to training data (good/ and bad/ subdirs)")
    parser.add_argument("--output", type=str, default="../src/model.h",
                        help="Output path for C header")
    parser.add_argument("--epochs", type=int, default=EPOCHS_DEFAULT)
    args = parser.parse_args()

    print(f"PosturePilot Model Training")
    print(f"  Data:   {args.data}")
    print(f"  Output: {args.output}")
    print(f"  Epochs: {args.epochs}")
    print(f"  Input:  {IMG_WIDTH}x{IMG_HEIGHT} grayscale")
    print(f"  Quant:  INT8 (full integer)")
    print()

    train_ds, val_ds, class_names = load_dataset(args.data)

    # Verify class order
    print(f"Class mapping: {dict(enumerate(class_names))}")
    if class_names[0] != "bad" or class_names[1] != "good":
        print("WARNING: Unexpected class order! Firmware assumes bad=0, good=1")
        print("         Rename your folders so alphabetical order is: bad, good")

    model = build_model()
    model.summary()

    callbacks = [
        keras.callbacks.EarlyStopping(patience=5, restore_best_weights=True),
        keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=3),
    ]

    history = model.fit(
        train_ds,
        validation_data=val_ds,
        epochs=args.epochs,
        callbacks=callbacks,
    )

    val_loss, val_acc = model.evaluate(val_ds)
    print(f"\nValidation accuracy: {val_acc:.2%}")

    if val_acc < 0.7:
        print("Warning: accuracy is low. Collect more data or check image quality.")

    # INT8 quantization needs the training data for calibration
    tflite_model = convert_to_tflite(model, train_ds, args.output)
    convert_to_header(tflite_model, args.output)

    print(f"\nDone! Flash the firmware to update the model.")


if __name__ == "__main__":
    main()
