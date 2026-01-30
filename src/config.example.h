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
#define TOPIC_ANGLE  "posture-pilot/angle"
#define TOPIC_LEVEL  "posture-pilot/level"

// ============================================
// Operating Mode
// ============================================
// MODE_COLLECT = data collection (HTTP server + camera)
// MODE_MONITOR = posture monitoring (inference + MQTT)
enum DeviceMode { MODE_COLLECT, MODE_MONITOR };
#define DEFAULT_MODE MODE_MONITOR

// ============================================
// Model / Inference Settings
// ============================================
#define MODEL_INPUT_WIDTH   96       // Model input image width
#define MODEL_INPUT_HEIGHT  96       // Model input image height
#define MODEL_INPUT_CHANNELS 1       // Grayscale
#define CONFIDENCE_THRESHOLD 0.6f    // Min confidence for classification
#define INFERENCE_INTERVAL_MS 500    // Run inference every N ms
#define TENSOR_ARENA_SIZE (64 * 1024)

// ============================================
// Posture Detection Settings
// ============================================
#define SLOUCH_THRESHOLD 0.5f        // Confidence above this = slouching

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
#define FRAME_RATE_FPS 5
#define CAMERA_RESOLUTION FRAMESIZE_QVGA

// ============================================
// Data Collection Settings
// ============================================
#define COLLECT_IMAGE_WIDTH  320
#define COLLECT_IMAGE_HEIGHT 240
#define WEB_SERVER_PORT 80

// ============================================
// OTA Settings
// ============================================
#define OTA_HOSTNAME "posture-pilot"
// #define OTA_PASSWORD "changeme"

// ============================================
// Debug
// ============================================
#define DEBUG_MODE true
// #define STREAM_ENABLED false  // Removed: streaming now built into collector

#endif // CONFIG_H
