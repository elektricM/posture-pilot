# Changelog

All notable changes to PosturePilot will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Project structure and initial documentation

### Changed
- N/A

### Fixed
- N/A

## [0.1.0] - 2025-01-30

### Added
- Initial release
- ESP32-S3 camera-based posture monitoring
- TFLite Micro inference on-device
- Two-mode operation (COLLECT/MONITOR)
- Web UI for training data collection with live MJPEG stream
- Model training script with INT8 quantization
- MQTT integration for Home Assistant
- Escalating posture warnings (4 levels)
- OTA firmware updates
- Comprehensive documentation (README, ARCHITECTURE, SETUP)
- MIT License

### Features
- Real-time posture classification using custom TFLite CNN
- Privacy-first: all processing on-device, no cloud
- Horizontal stream mirroring in web UI for natural self-view
- Keyboard shortcuts (G/B) for rapid data labeling
- Automatic image download during data collection
- Full INT8 quantization for efficient ESP32 inference
- Good posture streak tracking
- LED visual feedback on escalation level changes

### Technical
- XIAO ESP32S3 Sense support with OV2640 camera
- 8MB PSRAM utilization for model storage
- Bilinear image resizing for model input preprocessing
- Representative dataset calibration for INT8 quantization
- Early stopping and learning rate reduction during training
- Data augmentation (flip, rotation, brightness) for robustness

[Unreleased]: https://github.com/elektricM/posture-pilot/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/elektricM/posture-pilot/releases/tag/v0.1.0
