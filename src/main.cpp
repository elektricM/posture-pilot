/**
 * PosturePilot - AI Posture Monitor
 * 
 * Posture detection with escalating consequences
 * and Home Assistant integration.
 * 
 * Detection Method: Face position tracking
 * - Tracks face vertical position and size in frame
 * - When slouching: face drops lower in frame and appears larger (closer)
 * - No external ML models required - runs entirely on ESP32-S3
 * 
 * Hardware: Seeed Studio XIAO ESP32S3 Sense
 * - ESP32-S3 chip with 8MB OPI PSRAM
 * - OV2640 camera module (OV5640 also supported)
 * 
 * License: MIT
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "config.h"

// Camera pins for Seeed Studio XIAO ESP32S3 Sense
// Reference: https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage
#define PWDN_GPIO_NUM     -1    // Not used
#define RESET_GPIO_NUM    -1    // Not used
#define XCLK_GPIO_NUM     10    // XMCLK
#define SIOD_GPIO_NUM     40    // CAM_SDA (I2C data)
#define SIOC_GPIO_NUM     39    // CAM_SCL (I2C clock)
#define Y9_GPIO_NUM       48    // DVP_Y9
#define Y8_GPIO_NUM       11    // DVP_Y8
#define Y7_GPIO_NUM       12    // DVP_Y7
#define Y6_GPIO_NUM       14    // DVP_Y6
#define Y5_GPIO_NUM       16    // DVP_Y5
#define Y4_GPIO_NUM       18    // DVP_Y4
#define Y3_GPIO_NUM       17    // DVP_Y3
#define Y2_GPIO_NUM       15    // DVP_Y2
#define VSYNC_GPIO_NUM    38    // DVP_VSYNC
#define HREF_GPIO_NUM     47    // DVP_HREF
#define PCLK_GPIO_NUM     13    // DVP_PCLK

// Built-in LED for status (XIAO ESP32S3 uses GPIO21)
#define LED_GPIO_NUM      21

// ============================================
// State Management
// ============================================
enum PostureLevel {
    LEVEL_GOOD = 0,
    LEVEL_WARNING = 1,
    LEVEL_SERIOUS = 2,
    LEVEL_AGGRESSIVE = 3,
    LEVEL_AIRHORN = 4
};

struct PostureState {
    PostureLevel currentLevel;
    unsigned long slouchStartTime;
    unsigned long goodPostureTime;
    float currentAngle;
    int streak;  // Hours of good posture
    bool isSlouching;
} state;

// ============================================
// Face Detection State
// ============================================
struct FaceBaseline {
    float centerY;           // Normalized Y position (0-1)
    float centerX;           // Normalized X position (0-1)
    float size;              // Normalized face size (0-1)
    bool isCalibrated;
    int calibrationCount;
    
    // Accumulator for calibration averaging
    float sumY;
    float sumX;
    float sumSize;
} baseline;

struct FaceDetection {
    float centerY;           // Current face Y position
    float centerX;           // Current face X position  
    float size;              // Current face size
    bool detected;           // Was face found this frame?
    
    // Smoothed values
    float smoothY;
    float smoothX;
    float smoothSize;
} faceState;

// ============================================
// Globals
// ============================================
WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastFrameTime = 0;
unsigned long lastMqttPublish = 0;
const unsigned long FRAME_INTERVAL = 1000 / FRAME_RATE_FPS;
const unsigned long MQTT_INTERVAL = 5000;

// No-face detection counter
int noFaceFrames = 0;
const int MAX_NO_FACE_FRAMES = 10;

// ============================================
// Camera Setup
// ============================================
bool setupCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;  // Grayscale for processing
    config.frame_size = CAMERA_RESOLUTION;       // QVGA: 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }
    
    // Adjust camera settings for face detection
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 1);       // -2 to 2, slightly higher for face detection
        s->set_saturation(s, 0);     // -2 to 2
        s->set_gainceiling(s, GAINCEILING_4X);
        s->set_whitebal(s, 1);       // Auto white balance
        s->set_awb_gain(s, 1);       // Auto white balance gain
        s->set_exposure_ctrl(s, 1);  // Auto exposure
        s->set_aec2(s, 1);           // Auto exposure DSP
        s->set_ae_level(s, 0);       // -2 to 2
        s->set_gain_ctrl(s, 1);      // Auto gain
    }
    
    Serial.println("Camera initialized");
    return true;
}

// ============================================
// WiFi & MQTT
// ============================================
void setupWiFi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
}

void reconnectMQTT() {
    if (!mqtt.connected()) {
        Serial.print("Connecting to MQTT...");
        
        String clientId = MQTT_CLIENT_ID;
        clientId += String(random(0xffff), HEX);
        
        if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
            Serial.println("connected!");
            mqtt.publish("posture-pilot/status", "online");
            
            // Subscribe to calibration command
            mqtt.subscribe("posture-pilot/calibrate");
        } else {
            Serial.printf("failed, rc=%d\n", mqtt.state());
        }
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, "posture-pilot/calibrate") == 0) {
        Serial.println("📐 Calibration requested via MQTT");
        baseline.isCalibrated = false;
        baseline.calibrationCount = 0;
        baseline.sumY = 0;
        baseline.sumX = 0;
        baseline.sumSize = 0;
    }
}

void publishState() {
    if (!mqtt.connected()) return;
    
    StaticJsonDocument<256> doc;
    doc["level"] = state.currentLevel;
    doc["angle"] = state.currentAngle;
    doc["slouching"] = state.isSlouching;
    doc["streak"] = state.streak;
    doc["calibrated"] = baseline.isCalibrated;
    doc["face_detected"] = faceState.detected;
    
    char buffer[256];
    serializeJson(doc, buffer);
    
    mqtt.publish(TOPIC_STATUS, state.isSlouching ? "slouching" : "good");
    mqtt.publish(TOPIC_LEVEL, String(state.currentLevel).c_str());
    mqtt.publish(TOPIC_ANGLE, String(state.currentAngle, 1).c_str());
    mqtt.publish(TOPIC_STREAK, String(state.streak).c_str());
    mqtt.publish("posture-pilot/json", buffer);
    
    #if DEBUG_MODE
    Serial.printf("Published: level=%d, angle=%.1f, slouching=%d\n", 
                  state.currentLevel, state.currentAngle, state.isSlouching);
    #endif
}

// ============================================
// Face Detection via Brightness Analysis
// ============================================

/**
 * Detect face region using brightness centroid analysis.
 * 
 * Approach:
 * 1. Divide image into grid cells
 * 2. Calculate average brightness per cell
 * 3. Find connected bright region (likely face)
 * 4. Calculate centroid and size of bright region
 * 
 * This works because:
 * - Faces are typically brighter than background (skin reflectance)
 * - Face is usually the largest bright region in upper half of frame
 * - No ML model needed, very fast on ESP32
 */
bool detectFaceRegion(camera_fb_t* fb) {
    if (!fb || !fb->buf || fb->len == 0) {
        return false;
    }
    
    const int width = fb->width;    // 320 for QVGA
    const int height = fb->height;  // 240 for QVGA
    
    // Grid parameters
    const int GRID_COLS = 16;
    const int GRID_ROWS = 12;
    const int cellWidth = width / GRID_COLS;
    const int cellHeight = height / GRID_ROWS;
    
    // Calculate average brightness for each grid cell
    uint8_t cellBrightness[GRID_ROWS][GRID_COLS];
    uint8_t maxBrightness = 0;
    uint8_t minBrightness = 255;
    
    for (int gy = 0; gy < GRID_ROWS; gy++) {
        for (int gx = 0; gx < GRID_COLS; gx++) {
            uint32_t sum = 0;
            int count = 0;
            
            // Sample pixels in this cell (skip some for speed)
            for (int y = gy * cellHeight; y < (gy + 1) * cellHeight; y += 2) {
                for (int x = gx * cellWidth; x < (gx + 1) * cellWidth; x += 2) {
                    int idx = y * width + x;
                    if (idx < fb->len) {
                        sum += fb->buf[idx];
                        count++;
                    }
                }
            }
            
            uint8_t avg = count > 0 ? sum / count : 0;
            cellBrightness[gy][gx] = avg;
            
            if (avg > maxBrightness) maxBrightness = avg;
            if (avg < minBrightness) minBrightness = avg;
        }
    }
    
    // Calculate brightness threshold for face detection
    uint8_t range = maxBrightness - minBrightness;
    if (range < FACE_BRIGHTNESS_THRESHOLD) {
        // Image too uniform, can't detect face
        #if DEBUG_MODE
        Serial.println("Image too uniform for face detection");
        #endif
        return false;
    }
    
    // Threshold: cells above 60% of brightness range are considered "face"
    uint8_t threshold = minBrightness + (range * 60 / 100);
    
    // Find bright region centroid (weighted by brightness)
    float sumX = 0, sumY = 0, sumWeight = 0;
    int brightCells = 0;
    int minGx = GRID_COLS, maxGx = 0, minGy = GRID_ROWS, maxGy = 0;
    
    // Focus on upper 2/3 of frame where face typically is
    int maxRowForFace = GRID_ROWS * 2 / 3;
    
    for (int gy = 0; gy < maxRowForFace; gy++) {
        for (int gx = 0; gx < GRID_COLS; gx++) {
            if (cellBrightness[gy][gx] >= threshold) {
                float weight = cellBrightness[gy][gx] - threshold;
                sumX += gx * weight;
                sumY += gy * weight;
                sumWeight += weight;
                brightCells++;
                
                // Track bounding box
                if (gx < minGx) minGx = gx;
                if (gx > maxGx) maxGx = gx;
                if (gy < minGy) minGy = gy;
                if (gy > maxGy) maxGy = gy;
            }
        }
    }
    
    // Need minimum number of bright cells for valid face
    if (brightCells < 4 || sumWeight < 1.0f) {
        return false;
    }
    
    // Calculate normalized centroid (0.0 to 1.0)
    float centroidX = (sumX / sumWeight) / GRID_COLS;
    float centroidY = (sumY / sumWeight) / GRID_ROWS;
    
    // Calculate normalized size (based on bounding box)
    float faceWidth = (float)(maxGx - minGx + 1) / GRID_COLS;
    float faceHeight = (float)(maxGy - minGy + 1) / GRID_ROWS;
    float faceSize = faceWidth * faceHeight;
    
    // Validate face-like aspect ratio (0.6 to 1.5)
    float aspectRatio = faceWidth / max(faceHeight, 0.01f);
    if (aspectRatio < 0.5f || aspectRatio > 2.0f) {
        return false;
    }
    
    // Validate reasonable size
    if (faceSize < 0.02f || faceSize > 0.5f) {
        return false;
    }
    
    // Update face state with raw values
    faceState.centerX = centroidX;
    faceState.centerY = centroidY;
    faceState.size = faceSize;
    faceState.detected = true;
    
    // Apply exponential smoothing
    if (faceState.smoothX == 0) {
        // Initialize on first detection
        faceState.smoothX = centroidX;
        faceState.smoothY = centroidY;
        faceState.smoothSize = faceSize;
    } else {
        faceState.smoothX = faceState.smoothX * (1 - POSITION_SMOOTHING) + centroidX * POSITION_SMOOTHING;
        faceState.smoothY = faceState.smoothY * (1 - POSITION_SMOOTHING) + centroidY * POSITION_SMOOTHING;
        faceState.smoothSize = faceState.smoothSize * (1 - POSITION_SMOOTHING) + faceSize * POSITION_SMOOTHING;
    }
    
    #if DEBUG_MODE
    Serial.printf("Face: x=%.2f y=%.2f size=%.3f cells=%d\n", 
                  faceState.smoothX, faceState.smoothY, faceState.smoothSize, brightCells);
    #endif
    
    return true;
}

// ============================================
// Calibration
// ============================================
void updateCalibration() {
    if (baseline.isCalibrated || !faceState.detected) {
        return;
    }
    
    // Accumulate values for averaging
    baseline.sumY += faceState.smoothY;
    baseline.sumX += faceState.smoothX;
    baseline.sumSize += faceState.smoothSize;
    baseline.calibrationCount++;
    
    Serial.printf("📐 Calibrating... %d/%d\n", baseline.calibrationCount, CALIBRATION_FRAMES);
    
    // Flash LED during calibration
    digitalWrite(LED_GPIO_NUM, baseline.calibrationCount % 2 == 0 ? HIGH : LOW);
    
    if (baseline.calibrationCount >= CALIBRATION_FRAMES) {
        // Calculate averages
        baseline.centerY = baseline.sumY / baseline.calibrationCount;
        baseline.centerX = baseline.sumX / baseline.calibrationCount;
        baseline.size = baseline.sumSize / baseline.calibrationCount;
        baseline.isCalibrated = true;
        
        digitalWrite(LED_GPIO_NUM, LOW);
        
        Serial.println("✅ Calibration complete!");
        Serial.printf("   Baseline: x=%.2f y=%.2f size=%.3f\n", 
                      baseline.centerX, baseline.centerY, baseline.size);
        
        // Announce calibration complete
        if (mqtt.connected()) {
            mqtt.publish("posture-pilot/calibrated", "true");
            
            StaticJsonDocument<128> doc;
            doc["x"] = baseline.centerX;
            doc["y"] = baseline.centerY;
            doc["size"] = baseline.size;
            char buffer[128];
            serializeJson(doc, buffer);
            mqtt.publish("posture-pilot/baseline", buffer);
        }
    }
}

// ============================================
// Posture Detection
// ============================================

/**
 * Calculate posture angle from face position deviation.
 * 
 * Slouching indicators:
 * 1. Face moves DOWN in frame (Y increases) - head drooping
 * 2. Face gets LARGER (closer to camera) - leaning forward
 * 3. Face moves to sides - lateral lean
 * 
 * We combine these into a single "posture angle" for compatibility
 * with the existing escalation logic.
 */
float detectPostureAngle(camera_fb_t* fb) {
    if (!detectFaceRegion(fb)) {
        noFaceFrames++;
        faceState.detected = false;
        
        if (noFaceFrames > MAX_NO_FACE_FRAMES) {
            // No face for too long - maybe person left
            // Return neutral to avoid false alarms
            return 0;
        }
        
        // Keep last known angle briefly
        return state.currentAngle;
    }
    
    noFaceFrames = 0;
    
    // During calibration, report good posture
    if (!baseline.isCalibrated) {
        updateCalibration();
        return 0;
    }
    
    // Calculate deviations from baseline
    float yDeviation = faceState.smoothY - baseline.centerY;  // Positive = lower in frame
    float sizeDeviation = faceState.smoothSize - baseline.size;  // Positive = larger/closer
    float xDeviation = abs(faceState.smoothX - baseline.centerX);  // Lateral deviation
    
    // Weight factors (tuned for typical slouching patterns)
    const float Y_WEIGHT = 60.0f;     // Y position is most important
    const float SIZE_WEIGHT = 40.0f;  // Size/distance is secondary
    const float X_WEIGHT = 20.0f;     // Lateral is less common
    
    // Convert to "angle" (0-30 range typical)
    float posture_score = 0;
    
    // Y deviation: positive = slouching (head dropped)
    // Typical range: -0.2 to +0.3
    posture_score += yDeviation * Y_WEIGHT;
    
    // Size deviation: positive = leaning forward (closer to camera)
    // Typical range: -0.1 to +0.2
    posture_score += sizeDeviation * SIZE_WEIGHT;
    
    // X deviation: any direction indicates lean
    posture_score += xDeviation * X_WEIGHT;
    
    // Constrain to reasonable range
    posture_score = constrain(posture_score, -30.0f, 45.0f);
    
    #if DEBUG_MODE
    Serial.printf("Posture: y_dev=%.3f size_dev=%.3f x_dev=%.3f -> angle=%.1f\n",
                  yDeviation, sizeDeviation, xDeviation, posture_score);
    #endif
    
    return posture_score;
}

bool isSlouching(float angle) {
    return angle > SLOUCH_THRESHOLD;
}

// ============================================
// Escalation Logic
// ============================================
void updateEscalationLevel() {
    if (!state.isSlouching) {
        // Reset when posture is good
        state.currentLevel = LEVEL_GOOD;
        state.slouchStartTime = 0;
        
        // Track good posture time for streak
        if (state.goodPostureTime == 0) {
            state.goodPostureTime = millis();
        }
        
        // Update streak (hours of good posture)
        unsigned long goodDuration = (millis() - state.goodPostureTime) / 1000;
        state.streak = goodDuration / 3600;
        
        return;
    }
    
    // Slouching detected
    state.goodPostureTime = 0;
    
    if (state.slouchStartTime == 0) {
        state.slouchStartTime = millis();
    }
    
    unsigned long slouchDuration = (millis() - state.slouchStartTime) / 1000;
    
    PostureLevel previousLevel = state.currentLevel;
    
    if (slouchDuration >= LEVEL4_SECONDS) {
        state.currentLevel = LEVEL_AIRHORN;
    } else if (slouchDuration >= LEVEL3_SECONDS) {
        state.currentLevel = LEVEL_AGGRESSIVE;
    } else if (slouchDuration >= LEVEL2_SECONDS) {
        state.currentLevel = LEVEL_SERIOUS;
    } else if (slouchDuration >= LEVEL1_SECONDS) {
        state.currentLevel = LEVEL_WARNING;
    }
    
    // Publish immediately on level change
    if (state.currentLevel != previousLevel) {
        Serial.printf("⚠️ ESCALATION: Level %d -> %d (slouching %lu seconds)\n", 
                      previousLevel, state.currentLevel, slouchDuration);
        publishState();
        
        // Flash LED on escalation
        for (int i = 0; i <= state.currentLevel; i++) {
            digitalWrite(LED_GPIO_NUM, HIGH);
            delay(100);
            digitalWrite(LED_GPIO_NUM, LOW);
            delay(100);
        }
    }
}

// ============================================
// Main Loop
// ============================================
void processFrame() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }
    
    // Detect posture angle
    state.currentAngle = detectPostureAngle(fb);
    state.isSlouching = isSlouching(state.currentAngle);
    
    // Update escalation state
    updateEscalationLevel();
    
    esp_camera_fb_return(fb);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n🪑 PosturePilot Starting...");
    Serial.println("Face-position based posture detection");
    
    // LED setup
    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, LOW);
    
    // Initialize state
    state.currentLevel = LEVEL_GOOD;
    state.slouchStartTime = 0;
    state.goodPostureTime = millis();
    state.currentAngle = 0;
    state.streak = 0;
    state.isSlouching = false;
    
    // Initialize face state
    faceState.centerX = 0;
    faceState.centerY = 0;
    faceState.size = 0;
    faceState.detected = false;
    faceState.smoothX = 0;
    faceState.smoothY = 0;
    faceState.smoothSize = 0;
    
    // Initialize baseline
    baseline.isCalibrated = false;
    baseline.calibrationCount = 0;
    baseline.sumY = 0;
    baseline.sumX = 0;
    baseline.sumSize = 0;
    
    // Setup camera
    if (!setupCamera()) {
        Serial.println("Camera setup failed! Restarting...");
        delay(1000);
        ESP.restart();
    }
    
    // Connect to WiFi
    setupWiFi();
    
    // Setup MQTT
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    
    Serial.println("");
    Serial.println("===========================================");
    Serial.println("📐 CALIBRATION MODE");
    Serial.println("===========================================");
    Serial.println("Sit with GOOD POSTURE for calibration.");
    Serial.println("Look at the camera naturally.");
    Serial.printf("Capturing %d frames...\n", CALIBRATION_FRAMES);
    Serial.println("===========================================");
    Serial.println("");
}

void loop() {
    // Maintain MQTT connection
    if (!mqtt.connected()) {
        reconnectMQTT();
    }
    mqtt.loop();
    
    // Process frames at target FPS
    unsigned long now = millis();
    if (now - lastFrameTime >= FRAME_INTERVAL) {
        lastFrameTime = now;
        processFrame();
    }
    
    // Publish state periodically
    if (now - lastMqttPublish >= MQTT_INTERVAL) {
        lastMqttPublish = now;
        publishState();
    }
}
