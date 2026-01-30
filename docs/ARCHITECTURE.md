# Architecture

## How it works

Two modes: collect training data, then monitor posture.

```
  COLLECT MODE                        MONITOR MODE
  Browser ↔ ESP32 HTTP server         ESP32 Camera
  Capture labeled images              → TFLite inference
  (good posture / bad posture)        → MQTT → Home Assistant
                                      → Escalation → LED
        ↓
  Training data (good/ bad/)
        ↓
  train_model.py → model.h
        ↓
  Flash firmware with trained model
```

### Collect mode

HTTP server on the ESP32. Open the web UI, see a live camera feed, press G or B to label the current frame as good/bad posture. Need ~200+ images per class.

### Monitor mode

Runs TFLite Micro inference on each camera frame. Model takes 96x96 grayscale input, outputs good/bad confidence. No calibration step needed — the model already knows what to look for.

## Escalation

The longer you slouch, the more annoying it gets. Fix your posture and it resets.

## MQTT topics

| Topic | What |
|-------|------|
| `posture-pilot/status` | `good` or `slouching` |
| `posture-pilot/level` | Escalation level (0-4) |
| `posture-pilot/streak` | Hours of good posture |

## OTA

ArduinoOTA for wireless updates. Hostname: `posture-pilot.local`.
