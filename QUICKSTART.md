# QuickStart Guide

Get PosturePilot running in 15 minutes.

## Prerequisites

- **Hardware:** XIAO ESP32S3 Sense with camera
- **Software:** PlatformIO, Python 3.8+
- **Network:** 2.4GHz WiFi, MQTT broker (Home Assistant)

## 1. Clone & Configure (2 min)

```bash
git clone https://github.com/elektricM/posture-pilot.git
cd posture-pilot
cp src/config.example.h src/config.h
```

Edit `src/config.h`:
```cpp
#define WIFI_SSID "YourWiFi"
#define WIFI_PASS "YourPassword"
#define MQTT_SERVER "192.168.1.100"  // Your Home Assistant IP
```

## 2. Collect Training Data (10 min)

Set mode to COLLECT in `config.h`:
```cpp
#define DEFAULT_MODE MODE_COLLECT
```

Flash and find device IP:
```bash
pio run -t upload
pio device monitor
# Look for "Connected! IP: 192.168.x.x"
```

Open `http://<device-ip>/` in browser. You'll see a live camera feed.

**Collect images:**
- Sit normally → press **G** (good) 200+ times
- Slouch/hunch → press **B** (bad) 200+ times
- Vary positions, angles, lighting

Save images to `scripts/data/good/` and `scripts/data/bad/`.

## 3. Train Model (5 min)

```bash
cd scripts
pip install -r requirements.txt
python train_model.py --data ./data --output ../src/model.h
```

Wait for training to complete. Check validation accuracy (should be >80%).

## 4. Monitor (2 min)

Set mode to MONITOR in `config.h`:
```cpp
#define DEFAULT_MODE MODE_MONITOR
```

Flash:
```bash
cd ..
pio run -t upload
pio device monitor
```

Watch serial output for inference results:
```
Inference: good=0.92 bad=0.08 (187ms)
```

## 5. Home Assistant Integration (3 min)

Add to `configuration.yaml`:
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

Restart Home Assistant. Check Developer Tools → States for new entities.

## 6. Add Automations (optional)

Example: Send notification on LEVEL_AIRHORN

```yaml
automation:
  - alias: "Posture Airhorn Alert"
    trigger:
      - platform: state
        entity_id: sensor.posture_level
        to: "4"
    action:
      - service: notify.mobile_app
        data:
          message: "SIT UP STRAIGHT! You've been slouching for 10 minutes!"
```

More examples in `ha-config/automations.yaml`.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Camera won't start | Check camera module is seated properly |
| WiFi won't connect | ESP32 is 2.4GHz only (not 5GHz) |
| Model won't load | Run training script, check `src/model.h` exists |
| Bad accuracy | Collect more data (300+ per class) |
| MQTT not working | Check broker IP, port 1883, credentials |

For detailed help, see:
- [Setup Guide](docs/SETUP.md)
- [FAQ](docs/FAQ.md)
- [Troubleshooting section in README](README.md#-troubleshooting)

---

## Next Steps

- **Customize escalation timing** in `config.h` (LEVEL1_SECONDS, etc.)
- **Adjust threshold** with `SLOUCH_THRESHOLD` (higher = less sensitive)
- **Add automations** in Home Assistant (lights, sounds, notifications)
- **OTA updates:** `pio run -t upload --upload-port posture-pilot.local`

Happy slouching prevention! 🧘
