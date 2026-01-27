# Images for Documentation

Place your images here:

## Required Images

### demo.gif
**Main demo GIF for README** (600px wide recommended)

Record a ~10 second clip showing:
1. You sitting with good posture (green indicator)
2. Slouching forward
3. Escalation notification appearing
4. Sitting back up (reset)

**Tips:**
- Use screen recording + splice in notification popups
- Keep it under 5MB for GitHub
- Tools: OBS, Gifski, or ScreenToGif (Windows)

### dashboard.png
**Home Assistant dashboard screenshot** (800px wide)

Show:
- PosturePilot card with status/streak
- Maybe a history graph
- Notification example

### hardware.jpg (optional)
**Photo of your XIAO ESP32S3 Sense setup**

Show:
- The tiny board
- Mounted at desk
- Scale reference (coin, finger, etc.)

## Converting Video to GIF

```bash
# Using ffmpeg
ffmpeg -i demo.mp4 -vf "fps=10,scale=600:-1" -c:v gif demo.gif

# Using gifski (better quality)
gifski --fps 10 --width 600 -o demo.gif demo.mp4
```

## Compressing Images

Keep total image size reasonable for fast README loading:
- GIFs: < 5MB
- PNGs: < 500KB each

```bash
# Compress PNG
pngquant --quality=65-80 image.png

# Optimize GIF
gifsicle -O3 --lossy=80 demo.gif -o demo-opt.gif
```
