#ifndef COLLECTOR_H
#define COLLECTOR_H

#include <Arduino.h>

// Initialize the HTTP server for data collection
// Starts AsyncWebServer with endpoints:
//   /          - Web UI for data collection
//   /capture   - Capture single JPEG image
//   /stream    - MJPEG live stream
//   /status    - JSON device status
//   /collect   - Capture labeled image (?label=good or ?label=bad)
void collectorSetup();

// Call in loop to handle streaming clients
void collectorLoop();

// Get count of collected images
int getCollectedCount();

#endif // COLLECTOR_H
