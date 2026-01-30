#ifndef COLLECTOR_H
#define COLLECTOR_H

#include <Arduino.h>

// Start the data collection HTTP servers:
//   Port 80: Web UI + REST API (/capture, /collect, /status)
//   Port 81: MJPEG live stream (/stream)
void collectorSetup();

// Called in loop (not needed — servers run in background tasks)
void collectorLoop();

// Get count of collected images
int getCollectedCount();

#endif // COLLECTOR_H
