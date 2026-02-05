#!/bin/bash
# Test MQTT connection and subscribe to PosturePilot topics
# Usage: ./mqtt-test.sh [broker-ip]

BROKER=${1:-"192.168.1.100"}

echo "Subscribing to PosturePilot MQTT topics on $BROKER"
echo "Press Ctrl+C to stop"
echo ""

# Check if mosquitto_sub is installed
if ! command -v mosquitto_sub &> /dev/null; then
    echo "Error: mosquitto_sub not found"
    echo "Install with: sudo apt install mosquitto-clients"
    exit 1
fi

# Subscribe to all PosturePilot topics
mosquitto_sub -h "$BROKER" -t "posture-pilot/#" -v
