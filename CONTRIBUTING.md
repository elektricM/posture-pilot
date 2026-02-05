# Contributing to PosturePilot

Thanks for your interest in improving PosturePilot! This project is a personal learning exercise, but contributions are welcome.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/YOUR-USERNAME/posture-pilot.git`
3. Create a branch: `git checkout -b feature/your-feature-name`
4. Make your changes
5. Test on hardware if possible (or document what you tested)
6. Commit with clear messages: `git commit -m "Add feature: description"`
7. Push: `git push origin feature/your-feature-name`
8. Open a Pull Request

## Development Setup

### Hardware Requirements (for testing)
- XIAO ESP32S3 Sense board with camera
- USB-C cable
- Access to 2.4GHz WiFi

### Software Requirements
- PlatformIO (VS Code extension or CLI)
- Python 3.8+ (for model training)
- Git

### Building

```bash
# Install dependencies
pio pkg install

# Build firmware
pio run

# Upload to device
pio run -t upload

# Monitor serial output
pio device monitor
```

### Training Models

```bash
cd scripts
pip install -r requirements.txt
python train_model.py --data ./data --output ../src/model.h
```

## Code Style

- **C++**: Follow existing style (4 spaces, descriptive names)
- **Python**: PEP 8 compliant
- **Comments**: Explain *why*, not *what* (code should be self-documenting)
- **Commit messages**: Use present tense ("Add feature" not "Added feature")

## What to Contribute

### Good First Issues
- Documentation improvements
- Bug fixes
- Additional camera sensor support
- UI/UX improvements in the web interface
- Better error messages
- Training script enhancements

### Feature Ideas
- Different model architectures (MobileNet, EfficientNet variants)
- Multi-person support (track multiple postures)
- Configuration web interface (avoid recompiling for config changes)
- Bluetooth LE support for notifications
- Sound alerts (buzzer/speaker integration)
- Real-time inference dashboard in monitor mode
- Data augmentation improvements
- Model compression techniques

### Areas Needing Help
- Better camera tuning for different lighting conditions
- Transfer learning from pretrained models
- Battery-powered mode (deep sleep between checks)
- 3D-printable enclosure designs
- Additional Home Assistant automation examples

## Testing

Since this is embedded hardware:
- Test on real ESP32-S3 hardware when possible
- Document what you tested (hardware, WiFi, MQTT broker, etc.)
- Include serial output snippets for debugging
- Test both COLLECT and MONITOR modes if changes affect both

## Reporting Bugs

Open an issue with:
- **Description**: What happened vs. what you expected
- **Hardware**: Board model, camera module
- **Environment**: PlatformIO version, OS
- **Serial output**: Copy relevant logs
- **Steps to reproduce**: Clear, minimal steps

## Questions?

Open a discussion or issue. I'm happy to help!

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
