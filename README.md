# PosturePilot

**Posture correction that escalates from gentle reminders to an airhorn.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Build-orange.svg)](https://platformio.org/)

A tiny ESP32 camera that watches your posture and gets increasingly aggressive until you sit up straight. Uses a custom TFLite model trained on your own posture data, integrated with Home Assistant.

<!-- TODO: Add demo GIF -->
<!-- ![Demo](docs/images/demo.gif) -->

## Why?

I slouch constantly. Ergonomic chairs don't help. Those posture reminder apps? I ignore them.

What I needed was something that *escalates* — starts polite but eventually resorts to an airhorn if I keep ignoring it.

## How it works

1. **Collect** — Flash in collect mode, open the web UI, label ~200 images of good and bad posture
2. **Train** — Run the training script to build a TFLite model from your data
3. **Monitor** — Flash with the trained model, it classifies your posture in real-time and escalates if you slouch

The model runs entirely on the ESP32-S3 using TFLite Micro. No cloud, no latency, no privacy concerns.

## Hardware

| Part | Cost |
|------|------|
| [XIAO ESP32S3 Sense](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html) | ~€15 |
| USB-C cable | — |
| Optional: small speaker | ~€3 |

The XIAO ESP32S3 Sense has everything built in — camera, 8MB PSRAM for the model, and it's tiny (21 x 17.8mm).

## Setup

```bash
git clone https://github.com/elektricM/posture-pilot.git
cd posture-pilot
cp src/config.example.h src/config.h
```

Edit `src/config.h` with your WiFi and MQTT details:

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASS "your-password"
#define MQTT_SERVER "192.168.1.x"
```

### Step 1: Collect training data

Set `DEFAULT_MODE` to `MODE_COLLECT` in config.h, then flash:

```bash
pio run -t upload
```

Open `http://<device-ip>/` in your browser. You'll see a live camera feed with buttons to label frames. Collect at least 200 images per class — sit normally for "good", slouch in various ways for "bad".

### Step 2: Train the model

```bash
cd scripts
pip install -r requirements.txt
python train_model.py --data ./data --output ../src/model.h
```

This trains a small CNN and exports it as a C header that gets compiled into the firmware.

For better accuracy (but larger model), use transfer learning:

```bash
python train_model.py --data ./data --output ../src/model.h --transfer
```

### Step 3: Monitor

Set `DEFAULT_MODE` back to `MODE_MONITOR`, then flash again:

```bash
pio run -t upload
pio device monitor
```

Once running, you can also flash over WiFi (OTA):

```bash
pio run -t upload --upload-port posture-pilot.local
```

## Home Assistant

Add these sensors to `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Posture Status"
      state_topic: "posture-pilot/status"

    - name: "Posture Level"
      state_topic: "posture-pilot/level"

    - name: "Posture Streak"
      state_topic: "posture-pilot/streak"
      unit_of_measurement: "hours"
```

See [ha-config/](ha-config/) for automations including the airhorn trigger.

## Project structure

```
posture-pilot/
├── src/
│   ├── main.cpp           # Main app (mode switching, escalation)
│   ├── inference.h/cpp    # TFLite model loading + inference
│   ├── collector.h/cpp    # HTTP server for data collection
│   ├── model.h            # Trained model (generated)
│   └── config.h           # Your settings
├── scripts/
│   ├── train_model.py     # Training script
│   └── requirements.txt
├── ha-config/             # Home Assistant configs
└── docs/                  # Architecture docs
```

## Configuration

All in `src/config.h`:

```cpp
#define SLOUCH_THRESHOLD 0.5f    // Model confidence to trigger (0-1)
#define LEVEL1_SECONDS 30        // Time before first warning
#define LEVEL2_SECONDS 120       // Getting serious
#define LEVEL3_SECONDS 300       // Passive-aggressive
#define LEVEL4_SECONDS 600       // AIRHORN
```

## Troubleshooting

**Camera not starting** — Make sure PSRAM is enabled in platformio.ini

**Model won't load** — Check serial output. Arena might be too small — increase `TENSOR_ARENA_SIZE` in config.h

**Bad accuracy** — Collect more data, make sure lighting is consistent, try `--transfer` flag

**MQTT not connecting** — Check broker IP, make sure port 1883 isn't blocked

## License

MIT
