# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Security Considerations

### Network Security

**WiFi Credentials**: Stored in `src/config.h` which is gitignored. Never commit credentials to the repository.

**MQTT**: By default, PosturePilot connects to MQTT without TLS. For production use on untrusted networks:
- Use MQTT over TLS (port 8883)
- Enable MQTT authentication (`MQTT_USER` and `MQTT_PASS`)
- Restrict MQTT topics with ACLs on your broker

**OTA Updates**: Can be password-protected via `OTA_PASSWORD` in config.h. Without this, anyone on your network can upload firmware.

### Camera Privacy

**On-Device Processing**: All inference runs locally on the ESP32. No images are sent to the cloud.

**Collection Mode**: When in COLLECT mode, the ESP32 runs an HTTP server accessible to anyone on your local network. Only use this on trusted networks.

**Web UI**: The collection web UI has no authentication. Isolate the device on a trusted network or add authentication in `collector.cpp`.

### Data Storage

**No Persistent Storage**: The device doesn't store images or logs. Training data is only stored during collection when you manually download images.

**MQTT Topics**: Published posture state is sent in plain text. Use MQTT ACLs to restrict who can subscribe to topics.

## Reporting a Vulnerability

If you discover a security vulnerability, please report it by:

1. **Email**: martin@elektric.dev (preferred for sensitive issues)
2. **GitHub Issue**: For non-sensitive security improvements

Please include:
- Description of the vulnerability
- Steps to reproduce
- Potential impact
- Suggested fix (if any)

You can expect a response within 48 hours.

## Best Practices

1. **Change default OTA password** if enabled
2. **Use strong WiFi passwords**
3. **Enable MQTT authentication**
4. **Keep firmware updated**
5. **Only enable OTA on trusted networks**
6. **Don't expose the device to the internet** without proper firewall rules
7. **Review `config.h` before sharing or committing** (even though it's gitignored)

## Disclosure Policy

- Security issues will be patched as quickly as possible
- Critical vulnerabilities will be disclosed after a patch is released
- Users will be notified via GitHub releases and security advisories
