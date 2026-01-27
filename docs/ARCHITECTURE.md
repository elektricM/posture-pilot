# Architecture

## Overview

```
ESP32 Camera → Face Tracking → MQTT → Home Assistant → Notifications
```

## Detection

Tracks face position in frame. When you slouch:
- Face drops lower (head tilts forward)
- Face appears larger (leaning toward camera)

No ML models - just comparing current face position to calibrated baseline.

## Calibration

On startup, sit properly for 5 seconds. Device averages face position across 30 frames to establish "good posture" baseline.

## Escalation

Longer you ignore it, more aggressive it gets. Resets when you fix posture.

## MQTT

Publishes to:
- `posture-pilot/status` - good/slouching
- `posture-pilot/level` - escalation 0-4
- `posture-pilot/streak` - hours of good posture

Subscribes to:
- `posture-pilot/calibrate` - send 1 to recalibrate
