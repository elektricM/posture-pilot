# Model Training Guide

This guide covers training your PosturePilot model, from data collection to deployment.

## Quick Overview

```
Data Collection → Training → Conversion → Deployment
  (web UI)      (train_model.py)  (TFLite)  (flash firmware)
```

## Step 1: Data Collection

### Best Practices

**Lighting**: Consistent ambient lighting is crucial. Avoid:
- Bright windows behind you
- Direct overhead lights causing shadows
- Dramatic lighting changes during collection

**Variety**: Collect diverse examples:
- Different clothing (hoodies, t-shirts, bare skin)
- Various slouch positions (forward lean, side tilt, hunched)
- Good posture variations (upright, slight recline)
- Different times of day (if lighting changes)

**Camera Position**: 
- 50-80cm from your face
- Eye level when sitting
- Centered on your torso
- Angled slightly downward (5-15°)

**How Much Data**:
- **Minimum**: 200 images per class (400 total)
- **Recommended**: 300-500 per class (600-1000 total)
- **For Transfer Learning**: 150+ per class is usually enough

### Collection Workflow

1. Flash firmware in `MODE_COLLECT`
2. Open web UI at `http://<device-ip>/`
3. Position camera, sit with good posture
4. Press `G` repeatedly while maintaining position (~100 frames)
5. Make small adjustments (slight turns, shifts) and collect more
6. Now slouch in various ways
7. Press `B` for each bad posture variation (~200 frames)
8. Alternate between good and bad to avoid overfitting to time-based patterns

**Pro tip**: Use the keyboard shortcuts (G/B) for rapid collection. Hold the key down while making micro-adjustments to your position.

### Organizing Data

```bash
scripts/data/
  ├── good/
  │   ├── good_1.jpg
  │   ├── good_2.jpg
  │   └── ...
  └── bad/
      ├── bad_1.jpg
      ├── bad_2.jpg
      └── ...
```

The browser auto-downloads images. Move them to the correct folders before training.

## Step 2: Training

### Basic Training

```bash
cd scripts
pip install -r requirements.txt
python train_model.py --data ./data --output ../src/model.h
```

This trains a small CNN from scratch. Takes 5-10 minutes on a laptop.

### Advanced Options

**More Epochs** (better convergence, risk of overfitting):
```bash
python train_model.py --data ./data --epochs 50
```

**Transfer Learning** (better with less data):
```bash
python train_model.py --data ./data --transfer
```

Transfer learning uses a pretrained backbone (MobileNetV2) and fine-tunes it. Useful if:
- You have <200 images per class
- Accuracy is low with the basic model
- You want faster convergence

### Evaluating Results

The script prints validation accuracy at the end:

```
Validation accuracy: 92.45%
```

**What's good?**
- 90%+ — Excellent, ready to deploy
- 80-90% — Good, will work but may have occasional false positives
- 70-80% — Marginal, collect more data or try transfer learning
- <70% — Poor, need more/better data or check camera setup

**Common issues:**
- **Overfitting** (high train, low val accuracy): More data, stronger augmentation
- **Low accuracy overall**: More data, better variety, check lighting
- **One class accurate, other not**: Imbalanced data, collect more of the poor class

### Model Output

Training produces two files:
- `../src/model.h` — C header for firmware
- `../src/model.tflite` — Standalone TFLite file (for inspection)

## Step 3: Inspect (Optional)

### View Model Details

```bash
# Install TFLite tools
pip install tensorflow

# Inspect model
python -c "
import tensorflow as tf
interpreter = tf.lite.Interpreter(model_path='../src/model.tflite')
interpreter.allocate_tensors()

print('Input:', interpreter.get_input_details())
print('Output:', interpreter.get_output_details())
print('Size:', len(open('../src/model.tflite', 'rb').read()), 'bytes')
"
```

### Visualize Training History

Add this to `train_model.py` after training:

```python
import matplotlib.pyplot as plt

plt.figure(figsize=(12, 4))
plt.subplot(1, 2, 1)
plt.plot(history.history['accuracy'], label='Train')
plt.plot(history.history['val_accuracy'], label='Val')
plt.title('Accuracy')
plt.legend()

plt.subplot(1, 2, 2)
plt.plot(history.history['loss'], label='Train')
plt.plot(history.history['val_loss'], label='Val')
plt.title('Loss')
plt.legend()

plt.savefig('training_history.png')
```

## Step 4: Deploy

1. Set `DEFAULT_MODE = MODE_MONITOR` in `config.h`
2. Flash firmware: `pio run -t upload`
3. Watch serial output for inference results:
   ```
   Inference: good=0.92 bad=0.08 (189ms)
   ```

### Field Testing

Sit in your normal position and watch the serial output:
- **Good posture**: Should show `good > 0.7`
- **Slouching**: Should show `bad > 0.5`

If it's backwards (detecting good as bad), you may have swapped the folder names. Keras loads alphabetically: `bad=0`, `good=1`.

### Tuning Threshold

In `config.h`:
```cpp
#define SLOUCH_THRESHOLD 0.5f
```

- **Too sensitive** (false positives): Increase threshold (0.6, 0.7)
- **Too lenient** (ignores slouching): Decrease threshold (0.4, 0.3)

## Advanced: Model Architecture

The default model in `train_model.py`:

```
Input (96x96x1 grayscale)
    ↓
Conv2D(32, 5×5) + ReLU + MaxPool(2×2) + Dropout(0.2)
    ↓
Conv2D(64, 3×3) + ReLU + MaxPool(2×2) + Dropout(0.2)
    ↓
Conv2D(64, 3×3) + ReLU + MaxPool(2×2) + Dropout(0.2)
    ↓
GlobalAveragePooling2D
    ↓
Dense(128) + ReLU + Dropout(0.3)
    ↓
Dense(2, softmax)
```

**Why this architecture?**
- First 5×5 conv captures larger spatial features (head position, shoulder angle)
- 3×3 convs refine local features
- GlobalAvgPooling instead of Flatten → fewer parameters
- Dropout prevents overfitting on small datasets
- INT8-friendly ops (no fancy activations)

### Modify the Architecture

To experiment:

```python
# In train_model.py, replace build_model():

def build_model():
    model = keras.Sequential([
        layers.Input(shape=(IMG_HEIGHT, IMG_WIDTH, 1)),
        
        # Add your layers here
        layers.Conv2D(16, (3, 3), activation="relu"),
        layers.MaxPooling2D((2, 2)),
        
        # ...
        
        layers.GlobalAveragePooling2D(),
        layers.Dense(2, activation="softmax"),
    ])
    return model
```

**Constraints for ESP32:**
- Keep model <200KB (tensor arena + code fits in flash)
- Prefer simple ops (Conv2D, Dense, pooling, ReLU)
- Avoid: LSTM, complex activations, large dense layers
- Test on hardware — some ops are slow on ESP32

## Troubleshooting

### "Not enough training data"
Collect more images. Aim for 300+ per class.

### "Model too large for ESP32"
Reduce layers or filters. Increase `TENSOR_ARENA_SIZE` in `config.h` if needed (max ~100KB).

### "Inference too slow"
Reduce input size (64×64), fewer layers, or lower frame rate in `config.h`.

### "Accuracy plateaus early"
- Try transfer learning (`--transfer`)
- Collect more diverse data
- Increase data augmentation in `train_model.py`

### "Good validation, bad in practice"
- Lighting mismatch between training and deployment
- Camera angle changed
- Sitting position different than training data
- Collect data in actual deployment environment

## Tips

- **Label consistently**: Don't switch definitions mid-collection
- **Test quickly**: Train, flash, iterate — don't spend hours on perfect data before testing
- **Start simple**: Get 200/200, train, test. Then refine.
- **Version your models**: Save `.tflite` files with dates/versions
- **Document changes**: Note what data/settings produced which accuracy

## Next Steps

Once your model works:
- Fine-tune escalation timers in `config.h`
- Set up Home Assistant automations
- Add physical alerts (buzzer, relay)
- Train a new model for different lighting conditions
