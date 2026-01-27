# PosturePilot

**Posture correction that escalates from gentle reminders to an airhorn.**

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Build-orange.svg)](https://platformio.org/)

A tiny ESP32 camera that watches your posture and gets increasingly aggressive until you sit up straight. Integrates with Home Assistant for notifications and automations.

<!-- TODO: Add demo GIF once hardware arrives -->
<!-- ![Demo](docs/images/demo.gif) -->

## Why?

I slouch constantly. Ergonomic chairs don't help. Those posture reminder apps? I ignore them. 

What I needed was something that *escalates* - starts polite but eventually resorts to an airhorn if I keep ignoring it.

## How it works

The detection is simple: track where your face is in the frame. When you slouch, your face drops lower and appears larger (you're leaning toward the camera). No heavy ML models needed - just basic image processing that runs at 15+ FPS.

Escalation is time-based - the longer you ignore it, the more annoying it gets.

## Hardware

| Part | Cost |
|------|------|
| [XIAO ESP32S3 Sense](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html) | ~€15 |
| USB-C cable | - |
| Optional: small speaker | ~€3 |

The XIAO ESP32S3 Sense has everything built in - camera, enough RAM for ML if you want it later, and it's tiny (21 x 17.8mm).

## Setup

```bash
git clone https://github.com/elektricM/posture-pilot.git
cd posture-pilot
cp src/config.example.h src/config.h
```

Edit `src/config.h` with your WiFi and Home Assistant details:

```cpp
#define WIFI_SSID "your-wifi"
#define WIFI_PASS "your-password"
#define MQTT_SERVER "192.168.1.x"  // Home Assistant IP
```

Flash it:

```bash
pio run -t upload
pio device monitor
```

On first boot, sit with good posture for 5 seconds while it calibrates. LED blinks during calibration, goes solid when ready.

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

See [ha-config/](ha-config/) for the full automation setup including the airhorn trigger.

## MQTT Topics

| Topic | Description |
|-------|-------------|
| `posture-pilot/status` | `good` or `slouching` |
| `posture-pilot/level` | Current escalation (0-4) |
| `posture-pilot/angle` | Deviation from baseline |
| `posture-pilot/streak` | Hours of good posture |
| `posture-pilot/calibrate` | Send `1` to recalibrate |

## Configuration

All in `src/config.h`:

```cpp
#define SLOUCH_THRESHOLD 15      // Degrees before triggering
#define LEVEL1_SECONDS 30        // Time before level 1
#define LEVEL2_SECONDS 120       // Time before level 2
// etc.
```

## Project structure

```
posture-pilot/
├── src/
│   ├── main.cpp           # Main application
│   └── config.h           # Your settings
├── ha-config/             # Home Assistant configs
└── docs/                  # Setup guide
```

## Troubleshooting

**Camera not starting** - Make sure PSRAM is enabled in platformio.ini

**False positives** - Recalibrate, increase SLOUCH_THRESHOLD, check lighting

**MQTT not connecting** - Check broker IP, make sure port 1883 isn't blocked

## License

MIT
