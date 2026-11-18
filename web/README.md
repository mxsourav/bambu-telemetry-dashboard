# STΔN studio. Bambu Monitor Web Control Platform

A visionOS-inspired dark mode control platform for the ESP32 Bambu Lab 3D Printer Monitor.

## Features
- **1.8" ST7735 Live Display Mirror**: 160×128 pixel-by-pixel canvas mirror streamed with lightweight RLE compression.
- **Interactive Color Wheel**: Mouse and touch-draggable hue and saturation wheel with live crosshairs and RGB sliders.
- **RGB Presets & Instant OFF**: Instant toggle for Red, Green, Blue, White, Amber, Firoza, and OFF without latency.
- **Virtual D-Pad**: Menu and Select hardware keys mirrored in the web browser with haptic vibration.
- **Micro-SD File Explorer**: Browse, download, delete, and upload files to the micro-SD card over Wi-Fi.
- **Custom Screen Image Pusher**: Upload any picture, auto-crop to 160×128, and push to the physical ST7735 screen.
- **Wireless Web OTA Flasher**: Flash custom `.bin` builds over the air without USB.

## Deploying to Vercel

### Option 1: Vercel CLI
```bash
npm i -g vercel
cd web
vercel
```

### Option 2: GitHub
1. Push this repository to GitHub.
2. In Vercel, click **Add New Project** and select this repo.
3. Set the **Root Directory** to `web`.
4. Click **Deploy**.

## Linking to your ESP32
- **Default IP**: Auto-fills to `192.168.0.154`.
- **Query Parameter**: Open `https://your-app.vercel.app/?ip=192.168.x.x` to connect directly to any ESP32 IP.
- **Manual Change**: Click the connection pill in the top right to change the target IP at any time (persists in `localStorage`).
