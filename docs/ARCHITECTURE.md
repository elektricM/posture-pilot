# Architecture

Technical overview of how PosturePilot works.

## System Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        XIAO ESP32S3 Sense                       │
│  ┌─────────┐    ┌──────────────┐    ┌────────────────────────┐ │
│  │ Camera  │───▶│ Face Tracker │───▶│ Escalation Controller │ │
│  │ OV2640  │    │  (5 FPS)     │    │                        │ │
│  └─────────┘    └──────────────┘    └───────────┬────────────┘ │
│                                                  │              │
│                                                  ▼              │
│                                          ┌─────────────┐       │
│                                          │ MQTT Client │       │
│                                          └──────┬──────┘       │
└─────────────────────────────────────────────────┼───────────────┘
                                                  │
                                    WiFi          │
                                                  ▼
┌─────────────────────────────────────────────────────────────────┐
│                       Home Assistant                             │
│  ┌─────────────┐    ┌─────────────┐    ┌───────────────────┐   │
│  │ MQTT Broker │───▶│  Sensors    │───▶│   Automations     │   │
│  │ (Mosquitto) │    │             │    │  - Notifications  │   │
│  └─────────────┘    └─────────────┘    │  - Lights         │   │
│                                         │  - Airhorn        │   │
│                                         └───────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Face Position Tracking

### The Algorithm

Instead of complex pose estimation, we track where your face appears in the camera frame.

**Good Posture:**
- Face appears in upper portion of frame
- Face has consistent apparent size

**Slouching:**
- Face drops lower in frame (head tilts down/forward)
- Face appears larger (leaning closer to camera)

### Implementation

```cpp
// Simplified detection flow
void processFrame() {
    // 1. Capture frame
    camera_fb_t* fb = esp_camera_fb_get();
    
    // 2. Find brightest region (face tends to be brighter)
    FaceRegion face = detectFaceRegion(fb);
    
    // 3. Compare to calibrated baseline
    float y_deviation = face.centerY - baseline.centerY;
    float size_deviation = face.size - baseline.size;
    
    // 4. Calculate "posture angle" (pseudo-angle for simplicity)
    float angle = (y_deviation * Y_WEIGHT) + (size_deviation * SIZE_WEIGHT);
    
    // 5. Update state
    state.currentAngle = angle;
    state.isSlouching = abs(angle) > SLOUCH_THRESHOLD;
}
```

### Calibration

On startup (or when triggered):

1. Capture 30 frames
2. Track face position in each
3. Average to establish baseline
4. Store as "good posture" reference

### Why This Works

| Posture Change | Effect on Camera |
|----------------|------------------|
| Head drops forward | Face moves DOWN in frame |
| Shoulders hunch | Face moves DOWN + gets LARGER |
| Lean toward screen | Face gets LARGER |
| Slide down in chair | Face moves DOWN |

All common slouching patterns result in face moving down and/or getting larger.

## Escalation State Machine

```
                    ┌─────────────────────┐
                    │                     │
                    ▼                     │
┌────────┐      ┌────────┐      ┌────────┐│     ┌────────┐
│ GOOD   │─────▶│ WARN_1 │─────▶│ WARN_2 │├────▶│ WARN_3 │──────▶ AIRHORN
│        │      │ (30s)  │      │ (2min) ││     │ (5min) │        (10min)
└────────┘      └────────┘      └────────┘│     └────────┘
    ▲               │               │     │         │
    │               │               │     │         │
    └───────────────┴───────────────┴─────┴─────────┘
              Good posture detected → Reset
```

## MQTT Messages

### Published Topics

```
posture-pilot/status    → "good" | "slouching"
posture-pilot/level     → 0-4 (escalation level)
posture-pilot/angle     → float (deviation in degrees)
posture-pilot/streak    → int (hours of good posture)
posture-pilot/face      → "detected" | "lost" (debug)
```

### Subscribed Topics

```
posture-pilot/calibrate ← "1" (trigger recalibration)
```

## Memory Layout

```
ESP32-S3 (XIAO)
├── SRAM (512KB)
│   ├── Stack + Heap (~200KB)
│   ├── MQTT buffers (~8KB)
│   └── State variables (~1KB)
│
└── PSRAM (8MB)
    ├── Camera frame buffer (~150KB @ QVGA)
    ├── Face detection buffer (~50KB)
    └── Available for ML model (~7.5MB)
```

The 8MB PSRAM is what makes this board great for ML - plenty of room for larger models if you want to upgrade.

## Power Consumption

| State | Current | Notes |
|-------|---------|-------|
| Active monitoring | ~120mA | Camera + WiFi |
| WiFi only | ~100mA | Camera off |
| Light sleep | ~2mA | Between checks |
| Deep sleep | ~14µA | Not implemented yet |

For battery operation, implement sleep modes between detection cycles.
