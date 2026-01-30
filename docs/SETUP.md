# Setup Guide

## What you need

- XIAO ESP32S3 Sense with camera module
- USB-C cable
- WiFi (2.4GHz)
- Home Assistant with MQTT broker (for monitoring mode)
- Python 3.8+ with pip (for model training)

## Hardware

### Camera

The XIAO ESP32S3 Sense comes with a detachable OV2640 camera. If it's not attached, find the B2B connector and press the camera board in until it clicks.

### Mounting

For good results, place the camera:
- At eye level when sitting
- 50–80cm away, facing you
- Avoid backlighting (don't put it in front of a window)

## Software

### Install PlatformIO

VS Code extension (search "PlatformIO") or CLI:

```bash
pip install platformio
```

### Clone and configure

```bash
git clone https://github.com/elektricM/posture-pilot.git
cd posture-pilot
cp src/config.example.h src/config.h
```

Edit `src/config.h` with your network details:

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASS "your-password"
#define MQTT_SERVER "192.168.1.100"
```

## Step 1: Collect training data

Set the mode in `config.h`:

```cpp
#define DEFAULT_MODE MODE_COLLECT
```

Flash and open the web UI:

```bash
pio run -t upload
pio device monitor
```

Serial output will show the device IP. Open `http://<ip>/` in a browser.

You'll see a live camera feed. Use the buttons (or press G/B on keyboard) to label frames:
- **Good** — sit with proper posture
- **Bad** — slouch, lean, hunch over, etc.

Collect at least 200 images per class. More variety = better model. Move around, change lighting, wear different clothes.

Save the labeled images into `scripts/data/good/` and `scripts/data/bad/`.

## Step 2: Train the model

```bash
cd scripts
pip install -r requirements.txt
python train_model.py --data ./data --output ../src/model.h
```

This trains a small CNN and converts it to a C header. Takes a few minutes depending on your machine.

If accuracy is below 80%, try:
- More training data
- Transfer learning: `python train_model.py --data ./data --output ../src/model.h --transfer`
- More epochs: `--epochs 50`

## Step 3: Monitor

Switch mode back in `config.h`:

```cpp
#define DEFAULT_MODE MODE_MONITOR
```

Flash:

```bash
pio run -t upload
```

After the first flash, you can update over WiFi:

```bash
pio run -t upload --upload-port posture-pilot.local
```

## Home Assistant

### Enable MQTT

1. Settings → Add-ons → Install "Mosquitto broker"
2. Settings → Integrations → Add MQTT

### Add sensors

In `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Posture Status"
      state_topic: "posture-pilot/status"
      icon: mdi:human-handsup

    - name: "Posture Level"
      state_topic: "posture-pilot/level"
      icon: mdi:alert-circle

    - name: "Posture Streak"
      state_topic: "posture-pilot/streak"
      unit_of_measurement: "hours"
      icon: mdi:trophy
```

Copy automations from `ha-config/automations.yaml` or set them up in the UI.

## Troubleshooting

**Camera init failed** — Check the camera module is seated properly. Power cycle.

**WiFi won't connect** — ESP32 only does 2.4GHz, not 5GHz.

**Model won't load** — Check serial output. If arena is too small, increase `TENSOR_ARENA_SIZE` in config.h.

**Bad accuracy** — More data, better variety, or try `--transfer` mode.

**MQTT not connecting** — Check broker IP and port 1883.

## LED patterns

| Pattern | Meaning |
|---------|---------|
| Blinking | Starting up |
| Quick flash pattern | Escalation level change (flashes = level) |
| Off | Normal operation |
