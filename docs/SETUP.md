# Setup Guide

Detailed instructions for getting PosturePilot running.

## Prerequisites

- XIAO ESP32S3 Sense with camera attached
- USB-C cable
- WiFi network (2.4GHz)
- Home Assistant with MQTT broker

## Hardware

### Camera module

The XIAO ESP32S3 Sense comes with a detachable camera. If not attached:

1. Find the B2B connector on the board
2. Align the camera board connector
3. Press until it clicks

### Mounting

For best results:
- Eye level when sitting
- 50-80cm from your face
- Facing you directly
- Avoid backlighting (don't put in front of a window)

## Software

### Install PlatformIO

Either VS Code extension (search "PlatformIO") or CLI:

```bash
pip install platformio
```

### Clone and configure

```bash
git clone https://github.com/elektricM/posture-pilot.git
cd posture-pilot
cp src/config.example.h src/config.h
```

Edit `src/config.h`:

```cpp
#define WIFI_SSID "MyWiFiNetwork"
#define WIFI_PASS "MyWiFiPassword"
#define MQTT_SERVER "192.168.1.100"  // Your HA IP
```

### Build and flash

```bash
pio run -t upload
```

If upload fails, hold BOOT button while connecting USB.

### Monitor

```bash
pio device monitor -b 115200
```

Output should look like:

```
PosturePilot Starting...
Camera initialized
Connecting to MyWiFiNetwork...
Connected! IP: 192.168.1.50
MQTT connected!
Calibrating... sit with good posture
Calibration complete!
Monitoring...
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

  button:
    - name: "Recalibrate Posture"
      command_topic: "posture-pilot/calibrate"
      payload_press: "1"
```

### Automations

Copy from `ha-config/automations.yaml` or create manually in the UI.

## Calibration

1. Sit properly - back straight, shoulders relaxed
2. Face the camera
3. Wait 5 seconds - LED blinks during calibration
4. Done - LED goes solid

To recalibrate: press button in HA or restart device.

## Troubleshooting

**Camera init failed**
- Check camera module is attached firmly
- Power cycle the device

**WiFi connection failed**
- Must be 2.4GHz (ESP32 doesn't do 5GHz)
- Check credentials

**MQTT connection failed**
- Verify broker IP
- Check port 1883 isn't blocked

**False positives**
- Recalibrate
- Increase SLOUCH_THRESHOLD in config.h
- Check for shadows on your face

## LED patterns

| Pattern | Meaning |
|---------|---------|
| Slow blink | Calibrating |
| Solid | Ready, good posture |
| Fast blink | Slouching |
| Off | Error |
