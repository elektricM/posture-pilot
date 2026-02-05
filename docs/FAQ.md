# Frequently Asked Questions

## General

### What does PosturePilot do?

PosturePilot uses a tiny ESP32 camera to watch your posture in real-time. When it detects you slouching, it starts gentle warnings that escalate over time. Fix your posture and it resets. Keep slouching and eventually it'll get aggressive (airhorn-level aggressive).

### Is this a joke?

No! It's a real, working posture monitor. The "airhorn" part is metaphorical — you implement whatever consequence you want via Home Assistant automations (sound alerts, lights, notifications, etc.).

### Does it work?

Yes! The escalating approach is more effective than passive reminders because you can't ignore it indefinitely.

## Privacy & Security

### Does it send my camera feed to the cloud?

**No.** All processing happens on the ESP32 device. The model runs locally using TensorFlow Lite Micro. Nothing is sent to any external server.

### What data does it collect?

The device only sends MQTT messages to your local Home Assistant instance:
- Current posture status (good/slouching)
- Escalation level (0-4)
- Confidence score (0-1)
- Good posture streak (in hours)

No images are stored or transmitted during monitoring mode.

### What about data collection mode?

In COLLECT mode, images are captured only when you manually press the "Good" or "Bad" button. Images are downloaded to your browser, never stored on the device permanently.

## Technical

### Which ESP32 boards are supported?

Currently only the **XIAO ESP32S3 Sense** is tested and supported. It has:
- ESP32-S3 chip (dual-core, 240MHz)
- 8MB PSRAM (needed for model storage)
- OV2640 camera module included
- Tiny form factor (21×17.8mm)

Other ESP32-S3 boards with camera and PSRAM should work with pin configuration changes.

### How accurate is the model?

Depends on your training data quality. With 200+ images per class and consistent lighting, expect 85-95% accuracy. More data = better accuracy.

### How much power does it use?

Not measured precisely yet, but estimated ~200-300mA during active monitoring (WiFi + camera + inference). Not suitable for battery power without optimization.

### Can it detect multiple people?

No. The current model only detects "good posture" vs "bad posture" in frame. It doesn't identify individuals or track multiple people.

### How fast is inference?

~150-200ms per frame on ESP32-S3, running at 5 FPS by default. Fast enough for posture monitoring.

### What's the model size?

~20KB after INT8 quantization. Fits easily in the 64KB tensor arena.

## Setup & Usage

### Do I need Home Assistant?

Not strictly required, but recommended. Without it, you won't get MQTT integration, automations, or streak tracking. You could modify the code to use a different MQTT broker or add local alerts (LED, buzzer, etc.).

### Can I use this without training my own model?

No. Everyone's posture and seating position is different. You must collect your own training data and train a custom model.

### How long does training take?

Model training takes 5-10 minutes on a modern laptop (depends on CPU/GPU and number of images). The hard part is data collection — plan 10-15 minutes to capture 200+ images per class.

### Can I retrain the model without reflashing?

No. The model is compiled into the firmware. To update the model:
1. Train new model → generates `src/model.h`
2. Reflash firmware with `pio run -t upload`

After the first flash, you can use OTA (over WiFi) for faster updates.

### What happens if I move the camera?

You'll need to retrain the model. The camera position and angle affect how "good" and "bad" posture look in the frame.

### Can I adjust the escalation timing?

Yes! Edit `config.h`:

```cpp
#define LEVEL1_SECONDS 30    // Gentle reminder (30s)
#define LEVEL2_SECONDS 120   // Getting serious (2 min)
#define LEVEL3_SECONDS 300   // Aggressive (5 min)
#define LEVEL4_SECONDS 600   // AIRHORN (10 min)
```

### Can I change the confidence threshold?

Yes! Edit `config.h`:

```cpp
#define SLOUCH_THRESHOLD 0.5f  // 0.0 = more sensitive, 1.0 = less sensitive
```

Higher threshold = less false positives but might miss subtle slouching.

## Troubleshooting

### Camera stream is laggy

Normal. The web UI stream runs at ~30 FPS max. Inference runs at 5 FPS (configurable in config.h).

### Model accuracy is bad

Common causes:
- **Not enough data**: Collect 300+ images per class
- **Inconsistent lighting**: Train in the same lighting you'll use for monitoring
- **Wrong posture examples**: Make sure "bad" class covers all your slouching variations
- **Camera moved**: Retrain if you change camera position/angle

Try transfer learning with `--transfer` flag for better accuracy on small datasets.

### MQTT not working

Check:
1. MQTT broker is running (Home Assistant → Settings → Add-ons → Mosquitto broker)
2. `MQTT_SERVER` IP is correct in config.h
3. `MQTT_USER` and `MQTT_PASS` match broker settings (or leave empty if no auth)
4. Port 1883 isn't blocked by firewall
5. Serial monitor output for connection errors

### WiFi won't connect

- ESP32 only supports **2.4GHz WiFi**, not 5GHz
- Check `WIFI_SSID` and `WIFI_PASS` are correct
- Make sure WiFi network isn't hidden (some ESP32s struggle with hidden SSIDs)

### OTA upload fails

- Device must be on same network as your computer
- Hostname is `posture-pilot.local` (mDNS must work on your network)
- Try IP address instead: `pio run -t upload --upload-port <device-ip>`
- Check firewall isn't blocking port 3232

### Serial output shows "Model load failed"

Possible causes:
1. **No model**: Run `scripts/train_model.py` to generate `src/model.h`
2. **Arena too small**: Increase `TENSOR_ARENA_SIZE` in config.h (try 96KB or 128KB)
3. **Corrupted model**: Retrain and reflash

## Contributing

### Can I contribute?

Yes! See [CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines.

### I found a bug

Open an issue with:
- Hardware (board model)
- Serial output showing the error
- Steps to reproduce

### I have a feature idea

Open an issue with the `enhancement` label. Describe the use case and why it would be useful.

## Future Plans

### Will you support other cameras?

Maybe. The OV2640 on the XIAO ESP32S3 Sense is convenient because it's built-in. Supporting external cameras would complicate the hardware setup.

### Will you add sound alerts?

Not built-in, but you can add a buzzer/speaker and modify the code to trigger on escalation level changes. Home Assistant automations are the recommended approach (can play sounds on any smart speaker).

### Will you add a display?

Possible future enhancement. An OLED display could show current status, streak, and level. Low priority for now.

### Will you add Bluetooth notifications?

Possible. BLE notifications to a phone app could replace/complement the MQTT approach. Contributions welcome!
