/**
 * PosturePilot - AI Posture Monitor
 *
 * TFLite-based posture detection with escalating consequences
 * and Home Assistant integration.
 *
 * Modes:
 *   MONITOR - Run TFLite model to classify posture, publish via MQTT
 *   COLLECT - HTTP server for capturing labeled training images
 *
 * Hardware: Seeed Studio XIAO ESP32S3 Sense
 * - ESP32-S3 chip with 8MB OPI PSRAM
 * - OV2640 camera module
 *
 * License: MIT
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "esp_camera.h"
#include "config.h"
#include "inference.h"
#include "collector.h"

// Camera pins for Seeed Studio XIAO ESP32S3 Sense
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// Built-in LED
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
    float confidence;   // Model output: 0=good, 1=bad
    int streak;         // Hours of good posture
    bool isSlouching;
} state;

// ============================================
// Globals
// ============================================
WiFiClient espClient;
PubSubClient mqtt(espClient);

DeviceMode currentMode = DEFAULT_MODE;

unsigned long lastFrameTime = 0;
unsigned long lastMqttPublish = 0;
const unsigned long FRAME_INTERVAL = 1000 / FRAME_RATE_FPS;
const unsigned long MQTT_INTERVAL = 5000;

bool modelLoaded = false;

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
    config.frame_size = CAMERA_RESOLUTION;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;

    if (currentMode == MODE_COLLECT) {
        // JPEG for web serving — lower number = better quality (4-63)
        config.pixel_format = PIXFORMAT_JPEG;
        config.jpeg_quality = 6;
        config.fb_count = 4;
        config.frame_size = FRAMESIZE_VGA;  // 640x480 for collection
    } else {
        // Grayscale for inference
        config.pixel_format = PIXFORMAT_GRAYSCALE;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);         // Slightly brighter
        s->set_contrast(s, 0);           // Default contrast
        s->set_saturation(s, 0);         // Default saturation
        s->set_whitebal(s, 1);           // Auto white balance on
        s->set_awb_gain(s, 1);           // AWB gain on
        s->set_wb_mode(s, 0);            // Auto WB mode
        s->set_exposure_ctrl(s, 1);      // Auto exposure on
        s->set_aec2(s, 1);              // AEC DSP on (better auto-exposure)
        s->set_ae_level(s, 1);          // Slightly brighter exposure target
        s->set_aec_value(s, 400);        // Longer exposure for less noise
        s->set_gain_ctrl(s, 1);          // Auto gain on
        s->set_agc_gain(s, 0);           // Start with low gain
        s->set_gainceiling(s, (gainceiling_t)2);  // 4X max — less noise
        s->set_bpc(s, 1);               // Bad pixel correction
        s->set_wpc(s, 1);               // White pixel correction
        s->set_raw_gma(s, 1);           // Gamma correction
        s->set_lenc(s, 1);              // Lens correction
        s->set_dcw(s, 1);               // Downsize enable
    }

    Serial.println("Camera initialized");
    return true;
}

// ============================================
// WiFi
// ============================================
void setupWiFi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    
    // Set WiFi mode to station (client)
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Signal strength: %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial.println("Check SSID/password in config.h");
        Serial.println("Note: ESP32 only supports 2.4GHz WiFi");
    }
}

// ============================================
// OTA
// ============================================
void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);

    #ifdef OTA_PASSWORD
    ArduinoOTA.setPassword(OTA_PASSWORD);
    #endif

    ArduinoOTA.onStart([]() {
        Serial.println("OTA update starting...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA update complete!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error [%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("OTA ready");
}

// ============================================
// MQTT
// ============================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, "posture-pilot/mode") == 0) {
        // Switch mode via MQTT
        String msg;
        for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

        if (msg == "collect") {
            Serial.println("Mode switch to COLLECT requested - restart required");
            // Can't switch camera format at runtime; signal the need to restart
            mqtt.publish("posture-pilot/info", "Restart with MODE_COLLECT to collect data");
        }
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
            mqtt.subscribe("posture-pilot/mode");
        } else {
            Serial.printf("failed, rc=%d\n", mqtt.state());
        }
    }
}

void publishState() {
    if (!mqtt.connected()) return;

    StaticJsonDocument<256> doc;
    doc["level"] = state.currentLevel;
    doc["confidence"] = state.confidence;
    doc["slouching"] = state.isSlouching;
    doc["streak"] = state.streak;
    doc["model_loaded"] = modelLoaded;

    char buffer[256];
    serializeJson(doc, buffer);

    mqtt.publish(TOPIC_STATUS, state.isSlouching ? "slouching" : "good");
    mqtt.publish(TOPIC_LEVEL, String(state.currentLevel).c_str());
    mqtt.publish(TOPIC_ANGLE, String(state.confidence, 2).c_str());
    mqtt.publish(TOPIC_STREAK, String(state.streak).c_str());
    mqtt.publish("posture-pilot/json", buffer);

    #if DEBUG_MODE
    Serial.printf("Published: level=%d, conf=%.2f, slouching=%d\n",
                  state.currentLevel, state.confidence, state.isSlouching);
    #endif
}

// ============================================
// Escalation Logic
// ============================================
/**
 * Update escalation level based on how long the user has been slouching.
 * 
 * Escalation timeline (configurable in config.h):
 *   0-30s:   LEVEL_GOOD (no warning)
 *   30s-2m:  LEVEL_WARNING (gentle reminder)
 *   2m-5m:   LEVEL_SERIOUS (getting annoying)
 *   5m-10m:  LEVEL_AGGRESSIVE (very annoying)
 *   10m+:    LEVEL_AIRHORN (nuclear option)
 * 
 * When posture improves, immediately resets to LEVEL_GOOD and starts
 * tracking a "good posture streak" (published to MQTT in hours).
 * 
 * On level changes, publishes MQTT update and flashes LED (# of flashes = level).
 */
void updateEscalationLevel() {
    if (!state.isSlouching) {
        // Good posture detected — reset escalation
        state.currentLevel = LEVEL_GOOD;
        state.slouchStartTime = 0;

        // Track good posture streak (for MQTT streak sensor)
        if (state.goodPostureTime == 0) {
            state.goodPostureTime = millis();
        }

        unsigned long goodDuration = (millis() - state.goodPostureTime) / 1000;
        state.streak = goodDuration / 3600;  // Convert to hours

        return;
    }

    // Slouching detected — escalate over time
    state.goodPostureTime = 0;

    // Start timer on first slouch detection
    if (state.slouchStartTime == 0) {
        state.slouchStartTime = millis();
    }

    unsigned long slouchDuration = (millis() - state.slouchStartTime) / 1000;

    PostureLevel previousLevel = state.currentLevel;

    // Determine escalation level based on duration
    if (slouchDuration >= LEVEL4_SECONDS) {
        state.currentLevel = LEVEL_AIRHORN;
    } else if (slouchDuration >= LEVEL3_SECONDS) {
        state.currentLevel = LEVEL_AGGRESSIVE;
    } else if (slouchDuration >= LEVEL2_SECONDS) {
        state.currentLevel = LEVEL_SERIOUS;
    } else if (slouchDuration >= LEVEL1_SECONDS) {
        state.currentLevel = LEVEL_WARNING;
    }

    // Publish immediately on level change (not just on periodic interval)
    if (state.currentLevel != previousLevel) {
        Serial.printf("ESCALATION: Level %d -> %d (slouching %lu seconds)\n",
                      previousLevel, state.currentLevel, slouchDuration);
        publishState();

        // Visual feedback: flash LED N times where N = escalation level
        // (e.g., LEVEL_AIRHORN = 4 flashes)
        for (int i = 0; i <= state.currentLevel; i++) {
            digitalWrite(LED_GPIO_NUM, HIGH);
            delay(100);
            digitalWrite(LED_GPIO_NUM, LOW);
            delay(100);
        }
    }
}

// ============================================
// Frame Processing (Monitor Mode)
// ============================================
void processFrame() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return;
    }

    if (modelLoaded) {
        InferenceResult result = runInference(fb);
        state.confidence = result.confidence;
        state.isSlouching = result.isBadPosture;
        
        #if DEBUG_MODE
        // Print inference stats every 10 frames to avoid log spam
        static int frameCount = 0;
        if (++frameCount >= 10) {
            Serial.printf("Inference: conf=%.2f, slouch=%d, time=%lums\n",
                          result.confidence, result.isBadPosture, result.inferenceTimeMs);
            frameCount = 0;
        }
        #endif
    } else {
        // No model loaded - default to good posture
        // This allows testing without a trained model
        state.confidence = 0.0f;
        state.isSlouching = false;
    }

    updateEscalationLevel();

    esp_camera_fb_return(fb);
}

// ============================================
// Setup
// ============================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\nPosturePilot Starting...");
    Serial.printf("Mode: %s\n", currentMode == MODE_COLLECT ? "COLLECT" : "MONITOR");

    // LED setup
    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, LOW);

    // Initialize state
    state.currentLevel = LEVEL_GOOD;
    state.slouchStartTime = 0;
    state.goodPostureTime = millis();
    state.confidence = 0;
    state.streak = 0;
    state.isSlouching = false;

    // Setup camera
    if (!setupCamera()) {
        Serial.println("Camera setup failed! Restarting...");
        delay(1000);
        ESP.restart();
    }

    // Connect to WiFi
    setupWiFi();

    // Setup OTA
    setupOTA();

    if (currentMode == MODE_MONITOR) {
        // Setup MQTT
        mqtt.setServer(MQTT_SERVER, MQTT_PORT);
        mqtt.setCallback(mqttCallback);

        // Load TFLite model
        Serial.println("Loading TFLite model...");
        modelLoaded = inferenceSetup();
        if (modelLoaded) {
            Serial.println("Model loaded successfully");
        } else {
            Serial.println("Model load failed - running without inference");
            Serial.println("Flash a trained model or switch to COLLECT mode");
        }
    } else {
        // Start data collection server
        collectorSetup();
    }

    Serial.println("Setup complete!\n");
}

// ============================================
// Main Loop
// ============================================
void loop() {
    // Handle OTA updates
    ArduinoOTA.handle();

    if (currentMode == MODE_MONITOR) {
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
    } else {
        // Collection mode - server handles requests asynchronously
        collectorLoop();
    }
}
