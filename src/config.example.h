#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// WiFi Configuration
// ============================================
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"

// ============================================
// MQTT Configuration (Home Assistant)
// ============================================
#define MQTT_SERVER "192.168.1.100"  // Your HA IP
#define MQTT_PORT 1883
#define MQTT_USER ""                  // Leave empty if no auth
#define MQTT_PASS ""
#define MQTT_CLIENT_ID "posture-pilot"

// MQTT Topics
#define TOPIC_STATUS "posture-pilot/status"
#define TOPIC_STREAK "posture-pilot/streak"
#define TOPIC_ANGLE "posture-pilot/angle"
#define TOPIC_LEVEL "posture-pilot/level"

// ============================================
// Posture Detection Settings
// ============================================
#define SLOUCH_THRESHOLD 15          // Degrees deviation from good posture
#define GOOD_POSTURE_ANGLE 0         // Reference angle (calibrate on startup)

// ============================================
// Escalation Timers (seconds)
// ============================================
#define LEVEL1_SECONDS 30            // Gentle reminder
#define LEVEL2_SECONDS 120           // Getting serious
#define LEVEL3_SECONDS 300           // Passive-aggressive
#define LEVEL4_SECONDS 600           // AIRHORN TIME

// ============================================
// Camera Settings
// ============================================
#define FRAME_RATE_FPS 5             // Frames per second to analyze
#define CAMERA_RESOLUTION FRAMESIZE_QVGA  // 320x240, good balance

// ============================================
// Debug
// ============================================
#define DEBUG_MODE true              // Serial output
#define STREAM_ENABLED false         // Web stream (uses more resources)

#endif // CONFIG_H
