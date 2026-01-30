#include "collector.h"
#include "config.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>
#include <ArduinoJson.h>

static int collectedGood = 0;
static int collectedBad = 0;
static httpd_handle_t stream_httpd = NULL;
static httpd_handle_t camera_httpd = NULL;

// MJPEG stream boundary
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

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
  .bad { background: #e94560; color: #fff; }
  .bad:hover { background: #f05a74; }
  .stats { margin: 20px; padding: 15px; background: #16213e; border-radius: 8px; display: inline-block; }
  .stats span { font-size: 1.5em; font-weight: bold; margin: 0 15px; }
  #status { margin: 10px; color: #aaa; }
</style>
</head>
<body>
<h1>PosturePilot Data Collection</h1>
<img class="stream" id="cam" src="">
<div class="controls">
  <button class="good" onclick="collect('good')">Good Posture (G)</button>
  <button class="bad" onclick="collect('bad')">Bad Posture (B)</button>
</div>
<div class="stats">
  Good: <span id="good">0</span> | Bad: <span id="bad">0</span>
</div>
<div id="status">Ready. Use buttons or press G/B keys.</div>
<script>
// Start MJPEG stream on port 81
var cam = document.getElementById('cam');
var host = window.location.hostname;
cam.src = 'http://' + host + ':81/stream';

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
fetch('/status').then(r => r.json()).then(d => {
  document.getElementById('good').innerText = d.good;
  document.getElementById('bad').innerText = d.bad;
});
</script>
</body>
</html>
)rawliteral";

// ============================================
// MJPEG Stream handler (runs on port 81)
// Uses esp_http_server for proper chunked streaming
// Based on Espressif's official CameraWebServer example
// ============================================
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char part_buf[64];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Stream: capture failed");
            res = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            bool ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            esp_camera_fb_return(fb);
            fb = NULL;
            if (!ok) {
                Serial.println("Stream: JPEG convert failed");
                res = ESP_FAIL;
                break;
            }
        } else {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, 64, STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }

        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }

        if (res != ESP_OK) break;

        delay(30);  // ~30fps cap
    }

    return res;
}

// ============================================
// REST API handlers (run on port 80)
// ============================================

// Serve web UI
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

// Capture single JPEG
static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t res;
    if (fb->format == PIXFORMAT_JPEG) {
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        bool ok = frame2jpg(fb, 90, &jpg_buf, &jpg_len);
        esp_camera_fb_return(fb);
        if (ok) {
            res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
            free(jpg_buf);
        } else {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        return res;
    }

    esp_camera_fb_return(fb);
    return res;
}

// Collect labeled image
static esp_err_t collect_handler(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }

    char label[8];
    if (httpd_query_key_value(buf, "label", label, sizeof(label)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing label param");
        return ESP_FAIL;
    }

    if (strcmp(label, "good") != 0 && strcmp(label, "bad") != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "label must be good or bad");
        return ESP_FAIL;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_camera_fb_return(fb);

    if (strcmp(label, "good") == 0) collectedGood++;
    else collectedBad++;
    int total = collectedGood + collectedBad;

    StaticJsonDocument<256> doc;
    doc["status"] = "captured";
    doc["label"] = label;
    doc["good"] = collectedGood;
    doc["bad"] = collectedBad;
    doc["total"] = total;

    char jsonBuf[256];
    serializeJson(doc, jsonBuf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, jsonBuf, strlen(jsonBuf));
}

// Device status
static esp_err_t status_handler(httpd_req_t *req) {
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, strlen(buf));
}

void collectorSetup() {
    // Start camera HTTP server on port 80 (web UI + API)
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
    httpd_uri_t collect_uri = { .uri = "/collect", .method = HTTP_GET, .handler = collect_handler };
    httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
        httpd_register_uri_handler(camera_httpd, &collect_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        Serial.printf("Web UI: http://%s/\n", WiFi.localIP().toString().c_str());
    }

    // Start MJPEG stream server on port 81 (separate so it doesn't block API)
    config.server_port = 81;
    config.ctrl_port = 32769;

    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.printf("Stream: http://%s:81/stream\n", WiFi.localIP().toString().c_str());
    }
}

void collectorLoop() {
    // Both servers handle requests via ESP-IDF httpd tasks, nothing to do here
}

int getCollectedCount() {
    return collectedGood + collectedBad;
}
