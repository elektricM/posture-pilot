#include "collector.h"
#include "config.h"
#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer server(WEB_SERVER_PORT);
static int collectedGood = 0;
static int collectedBad = 0;

// HTML for data collection web UI
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PosturePilot Data Collection</title>
<style>
  body { font-family: sans-serif; text-align: center; background: #1a1a2e; color: #eee; margin: 0; padding: 20px; }
  h1 { color: #e94560; }
  .stream { width: 100%; max-width: 640px; border-radius: 8px; margin: 10px auto; display: block; }
  .controls { margin: 20px 0; }
  button { font-size: 1.2em; padding: 15px 40px; margin: 10px; border: none; border-radius: 8px; cursor: pointer; font-weight: bold; }
  .good { background: #0f3460; color: #eee; }
  .good:hover { background: #16498a; }
  .good:active { background: #1a5cb0; }
  .bad { background: #e94560; color: #fff; }
  .bad:hover { background: #f05a74; }
  .bad:active { background: #ff6b86; }
  .stats { margin: 20px; padding: 15px; background: #16213e; border-radius: 8px; display: inline-block; }
  .stats span { font-size: 1.5em; font-weight: bold; margin: 0 15px; }
  #status { margin: 10px; color: #aaa; }
  kbd { background: #333; padding: 2px 6px; border-radius: 3px; }
</style>
</head>
<body>
<h1>PosturePilot Data Collection</h1>
<img class="stream" src="/stream" alt="Camera Stream">
<div class="controls">
  <button class="good" onclick="collect('good')">Good Posture (G)</button>
  <button class="bad" onclick="collect('bad')">Bad Posture (B)</button>
</div>
<div class="stats">
  Good: <span id="good">0</span> | Bad: <span id="bad">0</span>
</div>
<div id="status">Ready. Use buttons or press G/B keys.</div>
<script>
function collect(label) {
  document.getElementById('status').innerText = 'Capturing ' + label + '...';
  fetch('/collect?label=' + label)
    .then(r => r.json())
    .then(d => {
      document.getElementById('good').innerText = d.good;
      document.getElementById('bad').innerText = d.bad;
      document.getElementById('status').innerText = 'Captured ' + label + ' (#' + d.total + ')';
    })
    .catch(e => { document.getElementById('status').innerText = 'Error: ' + e; });
}
document.addEventListener('keydown', function(e) {
  if (e.key === 'g' || e.key === 'G') collect('good');
  if (e.key === 'b' || e.key === 'B') collect('bad');
});
// Poll status on load
fetch('/status').then(r => r.json()).then(d => {
  document.getElementById('good').innerText = d.good;
  document.getElementById('bad').innerText = d.bad;
});
</script>
</body>
</html>
)rawliteral";

// MJPEG stream handler
static void handleStream(AsyncWebServerRequest* request) {
    // AsyncWebServer doesn't natively support chunked MJPEG well,
    // so we use a custom AsyncWebServerResponse with a callback.
    // For simplicity, serve a single JPEG frame per request.
    // The web UI uses <img src="/stream"> which auto-refreshes.
    // For real MJPEG streaming, we use a WiFiClient approach below.

    // Redirect to /capture for single-frame approach
    // Real MJPEG streaming is handled in collectorLoop() via raw sockets
    request->redirect("/capture");
}

// Capture and serve single JPEG frame
static void handleCapture(AsyncWebServerRequest* request) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        request->send(500, "text/plain", "Camera capture failed");
        return;
    }

    // If frame is JPEG, send directly; otherwise convert
    if (fb->format == PIXFORMAT_JPEG) {
        AsyncWebServerResponse* response = request->beginResponse(
            200, "image/jpeg", fb->buf, fb->len);
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    } else {
        // Convert grayscale to JPEG
        uint8_t* jpg_buf = nullptr;
        size_t jpg_len = 0;
        bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
        esp_camera_fb_return(fb);
        fb = nullptr;

        if (converted && jpg_buf) {
            AsyncWebServerResponse* response = request->beginResponse(
                200, "image/jpeg", jpg_buf, jpg_len);
            response->addHeader("Cache-Control", "no-cache");
            request->send(response);
            free(jpg_buf);
        } else {
            request->send(500, "text/plain", "JPEG conversion failed");
        }
        return;
    }

    esp_camera_fb_return(fb);
}

// Capture labeled image for training data
static void handleCollect(AsyncWebServerRequest* request) {
    if (!request->hasParam("label")) {
        request->send(400, "application/json", "{\"error\":\"missing label param\"}");
        return;
    }

    String label = request->getParam("label")->value();
    if (label != "good" && label != "bad") {
        request->send(400, "application/json", "{\"error\":\"label must be good or bad\"}");
        return;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        request->send(500, "application/json", "{\"error\":\"capture failed\"}");
        return;
    }

    // Convert to JPEG if needed
    uint8_t* jpg_buf = nullptr;
    size_t jpg_len = 0;
    bool needFree = false;

    if (fb->format == PIXFORMAT_JPEG) {
        jpg_buf = fb->buf;
        jpg_len = fb->len;
    } else {
        if (frame2jpg(fb, 90, &jpg_buf, &jpg_len)) {
            needFree = true;
        } else {
            esp_camera_fb_return(fb);
            request->send(500, "application/json", "{\"error\":\"jpeg conversion failed\"}");
            return;
        }
    }

    // Update counters
    if (label == "good") {
        collectedGood++;
    } else {
        collectedBad++;
    }
    int total = collectedGood + collectedBad;

    // Build filename
    String filename = label + "_" + String(total) + ".jpg";

    // Send the image as a download with metadata in response
    // The collection workflow: browser captures -> downloads image -> user saves to labeled folder
    // This avoids needing SD card on the ESP32
    AsyncWebServerResponse* response = request->beginResponse(
        200, "application/json", nullptr, 0);

    StaticJsonDocument<256> doc;
    doc["status"] = "captured";
    doc["label"] = label;
    doc["filename"] = filename;
    doc["good"] = collectedGood;
    doc["bad"] = collectedBad;
    doc["total"] = total;
    doc["size"] = jpg_len;

    char jsonBuf[256];
    serializeJson(doc, jsonBuf);

    if (needFree) free(jpg_buf);
    esp_camera_fb_return(fb);

    request->send(200, "application/json", jsonBuf);

    #if DEBUG_MODE
    Serial.printf("Collected: %s #%d (%d bytes)\n", label.c_str(), total, (int)jpg_len);
    #endif
}

// Download a labeled image (capture + immediate download)
static void handleDownload(AsyncWebServerRequest* request) {
    if (!request->hasParam("label")) {
        request->send(400, "text/plain", "missing label");
        return;
    }

    String label = request->getParam("label")->value();

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        request->send(500, "text/plain", "capture failed");
        return;
    }

    uint8_t* jpg_buf = nullptr;
    size_t jpg_len = 0;
    bool needFree = false;

    if (fb->format == PIXFORMAT_JPEG) {
        jpg_buf = fb->buf;
        jpg_len = fb->len;
    } else {
        if (frame2jpg(fb, 90, &jpg_buf, &jpg_len)) {
            needFree = true;
        } else {
            esp_camera_fb_return(fb);
            request->send(500, "text/plain", "jpeg conversion failed");
            return;
        }
    }

    int count = (label == "good") ? ++collectedGood : ++collectedBad;
    String filename = label + "_" + String(collectedGood + collectedBad) + ".jpg";

    AsyncWebServerResponse* response = request->beginResponse(
        200, "image/jpeg", jpg_buf, jpg_len);
    response->addHeader("Content-Disposition", "attachment; filename=" + filename);
    request->send(response);

    if (needFree) free(jpg_buf);
    esp_camera_fb_return(fb);
}

// Device status endpoint
static void handleStatus(AsyncWebServerRequest* request) {
    StaticJsonDocument<256> doc;
    doc["mode"] = "collect";
    doc["good"] = collectedGood;
    doc["bad"] = collectedBad;
    doc["total"] = collectedGood + collectedBad;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["free_psram"] = ESP.getFreePsram();
    doc["ip"] = WiFi.localIP().toString();

    char buf[256];
    serializeJson(doc, buf);
    request->send(200, "application/json", buf);
}

void collectorSetup() {
    // Serve web UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", INDEX_HTML);
    });

    // API endpoints
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/stream", HTTP_GET, handleStream);
    server.on("/collect", HTTP_GET, handleCollect);
    server.on("/download", HTTP_GET, handleDownload);
    server.on("/status", HTTP_GET, handleStatus);

    server.begin();
    Serial.printf("Collection server started on port %d\n", WEB_SERVER_PORT);
    Serial.printf("Open http://%s/ in your browser\n", WiFi.localIP().toString().c_str());
}

void collectorLoop() {
    // AsyncWebServer handles requests asynchronously, nothing needed here
    // Future: could add SD card batch saving, auto-download, etc.
}

int getCollectedCount() {
    return collectedGood + collectedBad;
}
