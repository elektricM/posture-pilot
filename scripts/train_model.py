#!/usr/bin/env python3
"""
PosturePilot Model Training Script

Trains a binary classifier (good posture vs bad posture) and exports
to TFLite format + C header for embedding in ESP32 firmware.

Usage:
    python train_model.py --data ./data --output ../src/model.h

Data structure:
    data/
      good/    <- images of good posture
      bad/     <- images of bad posture

The script supports two approaches:
    1. Simple CNN (default) - small, fast, trains from scratch
    2. Transfer learning (--transfer) - MobileNetV2 backbone, more accurate
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
TRANSFER_EPOCHS_DEFAULT = 15


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
    print(f"Training samples: {len(train_ds) * BATCH_SIZE}")
    print(f"Validation samples: {len(val_ds) * BATCH_SIZE}")

    # Normalize pixel values to [0, 1]
    normalization = layers.Rescaling(1.0 / 255)
    train_ds = train_ds.map(lambda x, y: (normalization(x), y))
    val_ds = val_ds.map(lambda x, y: (normalization(x), y))

    # Performance optimization
    train_ds = train_ds.cache().prefetch(buffer_size=tf.data.AUTOTUNE)
    val_ds = val_ds.cache().prefetch(buffer_size=tf.data.AUTOTUNE)

    return train_ds, val_ds, class_names


def build_simple_cnn():
    """Build a small CNN suitable for ESP32 inference."""
    model = keras.Sequential([
        layers.Input(shape=(IMG_HEIGHT, IMG_WIDTH, 1)),

        # Data augmentation (training only)
        layers.RandomFlip("horizontal"),
        layers.RandomRotation(0.05),
        layers.RandomZoom(0.1),

        # Conv block 1
        layers.Conv2D(16, 3, padding="same", activation="relu"),
        layers.MaxPooling2D(),

        # Conv block 2
        layers.Conv2D(32, 3, padding="same", activation="relu"),
        layers.MaxPooling2D(),

        # Conv block 3
        layers.Conv2D(64, 3, padding="same", activation="relu"),
        layers.MaxPooling2D(),

        # Classifier
        layers.GlobalAveragePooling2D(),
        layers.Dropout(0.3),
        layers.Dense(32, activation="relu"),
        layers.Dense(2, activation="softmax"),
    ])

    model.compile(
        optimizer="adam",
        loss="categorical_crossentropy",
        metrics=["accuracy"],
    )

    return model


def build_transfer_model():
    """Build a MobileNetV2-based model with transfer learning."""
    # MobileNetV2 expects 3 channels, so we repeat grayscale
    inputs = layers.Input(shape=(IMG_HEIGHT, IMG_WIDTH, 1))

    # Data augmentation
    x = layers.RandomFlip("horizontal")(inputs)
    x = layers.RandomRotation(0.05)(x)
    x = layers.RandomZoom(0.1)(x)

    # Convert grayscale to 3-channel for MobileNet
    x = layers.Concatenate()([x, x, x])

    # MobileNetV2 backbone (frozen)
    base_model = keras.applications.MobileNetV2(
        input_shape=(IMG_HEIGHT, IMG_WIDTH, 3),
        include_top=False,
        weights="imagenet",
    )
    base_model.trainable = False

    x = base_model(x, training=False)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dropout(0.3)(x)
    x = layers.Dense(64, activation="relu")(x)
    outputs = layers.Dense(2, activation="softmax")(x)

    model = keras.Model(inputs, outputs)

    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=1e-3),
        loss="categorical_crossentropy",
        metrics=["accuracy"],
    )

    return model


def convert_to_tflite(model, output_path: str):
    """Convert Keras model to TFLite format."""
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    # Quantize for smaller model and faster inference on ESP32
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.target_spec.supported_types = [tf.float16]

    tflite_model = converter.convert()

    tflite_path = output_path.replace(".h", ".tflite")
    with open(tflite_path, "wb") as f:
        f.write(tflite_model)

    print(f"TFLite model saved: {tflite_path} ({len(tflite_model)} bytes)")
    return tflite_model


def convert_to_header(tflite_model: bytes, output_path: str):
    """Convert TFLite model bytes to C header file."""
    hex_lines = []
    for i in range(0, len(tflite_model), 12):
        chunk = tflite_model[i:i + 12]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        hex_lines.append(f"    {hex_str},")

    header = f"""#ifndef MODEL_H
#define MODEL_H

// Auto-generated by scripts/train_model.py
// Model size: {len(tflite_model)} bytes
//
// Input:  {IMG_WIDTH}x{IMG_HEIGHT}x1 grayscale, float32 normalized [0, 1]
// Output: 2 floats [good_confidence, bad_confidence]

alignas(16) const unsigned char posture_model[] = {{
{chr(10).join(hex_lines)}
}};

const unsigned int posture_model_len = {len(tflite_model)};

#endif // MODEL_H
"""

    with open(output_path, "w") as f:
        f.write(header)

    print(f"C header saved: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Train PosturePilot posture classifier")
    parser.add_argument("--data", type=str, default="./data",
                        help="Path to training data (good/ and bad/ subdirectories)")
    parser.add_argument("--output", type=str, default="../src/model.h",
                        help="Output path for C header file")
    parser.add_argument("--transfer", action="store_true",
                        help="Use MobileNetV2 transfer learning instead of simple CNN")
    parser.add_argument("--epochs", type=int, default=None,
                        help="Number of training epochs")
    args = parser.parse_args()

    # Set default epochs based on model type
    if args.epochs is None:
        args.epochs = TRANSFER_EPOCHS_DEFAULT if args.transfer else EPOCHS_DEFAULT

    print("PosturePilot Model Training")
    print(f"  Data:     {args.data}")
    print(f"  Output:   {args.output}")
    print(f"  Model:    {'MobileNetV2 transfer' if args.transfer else 'Simple CNN'}")
    print(f"  Epochs:   {args.epochs}")
    print(f"  Input:    {IMG_WIDTH}x{IMG_HEIGHT} grayscale")
    print()

    # Load data
    train_ds, val_ds, class_names = load_dataset(args.data)

    # Verify class order: should be [bad, good] alphabetically
    # Output index 0 = bad, index 1 = good
    # BUT our firmware expects: output[0] = good, output[1] = bad
    # image_dataset_from_directory sorts alphabetically: bad=0, good=1
    print(f"Class mapping: {dict(enumerate(class_names))}")
    if class_names[0] == "bad" and class_names[1] == "good":
        print("Note: Model outputs [bad, good]. Firmware will read index 1 as bad_confidence.")
        print("      Adjust inference.cpp if class order differs.")

    # Build model
    if args.transfer:
        model = build_transfer_model()
    else:
        model = build_simple_cnn()

    model.summary()

    # Train
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

    # Evaluate
    val_loss, val_acc = model.evaluate(val_ds)
    print(f"\nValidation accuracy: {val_acc:.2%}")
    print(f"Validation loss: {val_loss:.4f}")

    if val_acc < 0.7:
        print("\nWarning: Low accuracy. Consider collecting more data.")

    # Convert to TFLite
    tflite_model = convert_to_tflite(model, args.output)

    # Convert to C header
    convert_to_header(tflite_model, args.output)

    print("\nDone! Flash the firmware to update the model on your ESP32.")
    print(f"Model file: {args.output}")


if __name__ == "__main__":
    main()
