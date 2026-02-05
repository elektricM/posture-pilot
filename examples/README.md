# Examples and Utilities

Helper scripts and examples for PosturePilot development and testing.

## Scripts

### `platformio-monitor.sh`

Enhanced serial monitor with color-coded output for easier debugging.

```bash
./platformio-monitor.sh
```

Highlights:
- 🔴 Red: Errors and escalation events
- 🟡 Yellow: Slouching detected
- 🟢 Green: Good posture

### `mqtt-test.sh`

Test MQTT connection and monitor all PosturePilot topics.

```bash
./mqtt-test.sh [broker-ip]
```

Requires `mosquitto_sub` (install via `apt install mosquitto-clients`).

## Configuration Examples

### Quick Development Config

For rapid testing without MQTT:

```cpp
// config.h
#define MQTT_SERVER ""  // Leave empty to skip MQTT
#define DEBUG_MODE true
#define FRAME_RATE_FPS 1  // Slow down for easier serial reading
```

### Production Config

Optimized for real use:

```cpp
// config.h
#define MQTT_SERVER "192.168.1.100"
#define MQTT_USER "homeassistant"
#define MQTT_PASS "your-password"
#define DEBUG_MODE false
#define FRAME_RATE_FPS 5
#define SLOUCH_THRESHOLD 0.55f
#define OTA_PASSWORD "your-ota-password"
```

### Testing Config (permissive)

Easier to trigger during development:

```cpp
// config.h
#define SLOUCH_THRESHOLD 0.3f  // Lower = more sensitive
#define LEVEL1_SECONDS 10      // Fast escalation for testing
#define LEVEL2_SECONDS 20
#define LEVEL3_SECONDS 30
#define LEVEL4_SECONDS 40
```

## Home Assistant Automation Examples

### Notification on Level 3

```yaml
automation:
  - alias: "Posture Alert"
    trigger:
      platform: state
      entity_id: sensor.posture_level
      to: "3"
    action:
      service: notify.mobile_app
      data:
        message: "Fix your posture! Level 3 reached."
```

### Play Sound on Airhorn Level

```yaml
automation:
  - alias: "Posture Airhorn"
    trigger:
      platform: state
      entity_id: sensor.posture_level
      to: "4"
    action:
      service: media_player.play_media
      target:
        entity_id: media_player.living_room
      data:
        media_content_type: music
        media_content_id: "http://example.com/airhorn.mp3"
```

### Daily Streak Report

```yaml
automation:
  - alias: "Daily Posture Report"
    trigger:
      platform: time
      at: "20:00:00"
    action:
      service: notify.mobile_app
      data:
        message: "Today's posture streak: {{ states('sensor.posture_streak') }} hours"
```

## Training Data Examples

Structure for training data:

```
scripts/data/
  ├── good/
  │   ├── good_1.jpg      # Upright, shoulders back
  │   ├── good_2.jpg      # Slight recline, still aligned
  │   ├── good_3.jpg      # Turned slightly left
  │   └── ...
  └── bad/
      ├── bad_1.jpg       # Forward slouch
      ├── bad_2.jpg       # Side tilt
      ├── bad_3.jpg       # Hunched over
      └── ...
```

## Testing Checklist

Before deploying:

- [ ] Camera focuses correctly
- [ ] WiFi connects reliably
- [ ] MQTT publishes successfully
- [ ] Model loads without errors
- [ ] Inference time <500ms
- [ ] Good posture not triggering alerts
- [ ] Slouching triggers within 30s
- [ ] Escalation progresses correctly
- [ ] OTA updates work
- [ ] LED blinks on escalation
- [ ] Serial output shows expected values

## Common Development Tasks

### Flash and monitor in one command

```bash
pio run -t upload && pio device monitor
```

### OTA update (after first flash)

```bash
pio run -t upload --upload-port posture-pilot.local
```

### Quick rebuild after config change

```bash
pio run
```

### Clean build (if things get weird)

```bash
pio run -t clean
pio run
```

### Check available serial ports

```bash
pio device list
```

## Debugging Tips

### No camera image in collect mode

1. Check ribbon cable connection
2. Verify PSRAM enabled in platformio.ini
3. Check serial output for camera init errors
4. Try power cycling the device

### Inference always returns same value

1. Model may not be loaded (check serial)
2. Camera may be returning black frames
3. Check `TENSOR_ARENA_SIZE` is large enough
4. Verify model.h was generated correctly

### WiFi won't connect

1. ESP32 only supports 2.4GHz (not 5GHz)
2. Check SSID/password in config.h
3. Move closer to router
4. Check router allows new devices

### MQTT not publishing

1. Verify broker IP and port
2. Check firewall rules
3. Test with `mqtt-test.sh`
4. Enable DEBUG_MODE and check serial output
