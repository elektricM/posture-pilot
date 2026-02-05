#!/bin/bash
# Convenience script for monitoring PosturePilot serial output with filters

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "PosturePilot Serial Monitor"
echo "==========================="
echo ""

# Check if device is connected
if ! pio device list | grep -q "tty"; then
    echo -e "${RED}No device found!${NC}"
    echo "Connect your ESP32 and try again."
    exit 1
fi

# Monitor with color highlighting for key events
pio device monitor | while IFS= read -r line; do
    case "$line" in
        *"ESCALATION"*)
            echo -e "${RED}${line}${NC}"
            ;;
        *"slouching"*)
            echo -e "${YELLOW}${line}${NC}"
            ;;
        *"good"*)
            echo -e "${GREEN}${line}${NC}"
            ;;
        *"Error"*|*"error"*|*"failed"*)
            echo -e "${RED}${line}${NC}"
            ;;
        *"Inference:"*)
            # Only show inference lines (filter out noise)
            echo "$line"
            ;;
        *)
            echo "$line"
            ;;
    esac
done
