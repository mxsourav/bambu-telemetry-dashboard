# STΔN studio. — Comprehensive Setup & Deployment Guide

This guide walks through every step required to assemble, flash, configure, and operate the STΔN studio ESP32 3D Printer Monitor, Autonomous Desk Companion, and Web PWA platform.

---

## Table of Contents
1. [Hardware Specifications & Pinout](#1-hardware-specifications--pinout)
2. [Display Offsets & Tab Troubleshooting](#2-display-offsets--tab-troubleshooting)
3. [Firmware Compilation & Flashing](#3-firmware-compilation--flashing)
4. [Windows Autonomous Companion Setup](#4-windows-autonomous-companion-setup)
5. [Localhost Admin Panel & Reverse Proxy](#5-localhost-admin-panel--reverse-proxy)
6. [Vercel Cloud Deployment & PNA Configuration](#6-vercel-cloud-deployment--pna-configuration)
7. [Render Cloud Backend Deployment](#7-render-cloud-backend-deployment)
8. [Spotify Media Integration Setup](#8-spotify-media-integration-setup)
9. [Troubleshooting & Diagnostics](#9-troubleshooting--diagnostics)

---

## 1. Hardware Specifications & Pinout

### Target Microcontroller
- **Board**: ESP32 DevKit v1 (30-pin)
- **SoC**: Dual-core Tensilica Xtensa LX6 @ 240 MHz
- **Memory**: 520 KB SRAM, 4 MB SPI Flash
- **Partition Scheme**: `min_spiffs.csv` (1.9 MB App, 1.9 MB OTA, 190 KB SPIFFS)

### ST7735 1.8-inch TFT LCD (VSPI)
| Screen Pin | ESP32 GPIO | Function / Notes |
|---|---|---|
| **VCC** | 3.3V or 5V | Input power (verify onboard regulator) |
| **GND** | GND | Common ground |
| **CS** | GPIO 5 | Chip Select (Hardware VSPI) |
| **RESET** | GPIO 4 | Hardware display reset |
| **A0 / DC** | GPIO 2 | Register select (Data / Command) |
| **SDA / MOSI** | GPIO 23 | VSPI MOSI (Master Out Slave In) |
| **SCK / SCLK** | GPIO 18 | VSPI SCK (SPI Clock up to 40 MHz) |
| **LED / BL** | 3.3V | Backlight LED anode |

### MicroSD Card SPI Reader (HSPI)
| MicroSD Pin | ESP32 GPIO | Function / Notes |
|---|---|---|
| **CS** | GPIO 15 | HSPI Chip Select |
| **SCK** | GPIO 25 | HSPI Clock (4 MHz) |
| **MOSI** | GPIO 13 | Master Out Slave In |
| **MISO** | GPIO 26 | Master In Slave Out |
| **VCC / GND** | 3.3V / GND | Regulated logic power and ground |

### Additional Peripherals
| Peripheral | Pin | Type | Notes |
|---|---|---|---|
| **Menu Button** | GPIO 4 | Input (Pull-up) | Active LOW debounced button |
| **Select Button** | GPIO 16 | Input (Pull-up) | Active LOW debounced button |
| **Passive Buzzer** | GPIO 14 | PWM Output | Driven via ESP32 LEDC channel |
| **Status RGB LED** | GPIO 27 | WS2812B Data | Single pixel or short strip |

---

## 2. Display Offsets & Tab Troubleshooting

ST7735 1.8" displays (128×160 or 160×128) are manufactured across various silicon revisions with differing Graphics RAM (GRAM) offsets. If your screen shows a 1–2 pixel static noise border along one edge:

- **Black Tab**: Uses 0-pixel offset.
- **Green Tab / Green Tab 2 / 3**: Expects a 1 to 2 pixel GRAM coordinate offset.
- **Red Tab**: Standard 0 or 1 pixel offset depending on revision.

In STΔN studio, the display driver is configured directly using `LovyanGFX` inside `src/main.ino` for direct DMA transfers without external header conflicts.

---

## 3. Firmware Compilation & Flashing

### Prerequisites
- [PlatformIO Core](https://platformio.org/) or the PlatformIO VSCode extension.
- Python 3.10+ with `esptool`.

### Step-by-Step Build
1. Open the repository root directory in a terminal:
   ```bash
   cd bambu-telemetry-dashboard
   ```
2. Verify dependencies and compile the firmware:
   ```bash
   pio run
   ```
3. Connect your ESP32 to your PC via USB and upload:
   ```bash
   pio run -t upload
   ```
   *(Optional)* To specify the serial COM port manually:
   ```bash
   pio run -t upload --upload-port COM11
   ```
4. Monitor serial logs at 115200 baud:
   ```bash
   pio device monitor -b 115200
   ```

---

## 4. Windows Autonomous Companion Setup

The Windows host companion runs as a zero-maintenance background service that monitors PC hardware metrics, captures WASAPI desktop audio, and runs the NVIDIA NIM AI cognitive sentinels.

### Dependencies
Install the required Python packages:
```bash
pip install winsdk psutil requests urllib3 soundcard numpy scipy
```

### One-Click Autonomous Launch
Double-click `STAN_AUTONOMOUS_START.bat` in the repository root. This script:
1. **Verifies Windows Mobile Hotspot**: Ensures your laptop's hotspot is enabled on `192.168.137.1` so the ESP32 can connect securely.
2. **Registers Startup Registry Entry**: Configures Windows to silently launch `tools/start_sync_silent.vbs` on boot.
3. **Spawns Daemons**: Launches `tools/spotify_desk_sync.py` and `tools/audio_beat_analyzer.py` in the background.
4. **Autonomous Sleep / Wake**: Puts the desk pet into drowsy/sleep mode after 5 minutes of PC inactivity and wakes it upon return.

To interact with the pet at any time:
```bash
TALK_TO_STAN.bat
```
To terminate background tasks:
```bash
STAN_STOP.bat
```

---

## 5. Localhost Admin Panel & Reverse Proxy

For a local dashboard experience without internet dependencies:

1. Double-click `START_ADMIN_PANEL.bat` or run:
   ```bash
   python tools/admin_server.py
   ```
2. What happens automatically:
   - Scans candidate IPs (`192.168.137.35`, `192.168.0.154`, `192.168.1.35`, etc.) to locate your active ESP32.
   - Spins up a multi-threaded reverse proxy on `http://localhost:3000/`.
   - Injects Private Network Access (PNA) and CORS headers.
   - Opens the visionOS web dashboard in your default browser.

---

## 6. Vercel Cloud Deployment & PNA Configuration

The frontend located in `web/` is a progressive web app (PWA) deployable to Vercel.

### Deploying to Vercel
```bash
npm i -g vercel
cd web
vercel --prod
```
Or import the GitHub repository in the Vercel dashboard with Root Directory set to `web`.

### Private Network Access (PNA) Bypass
Modern browsers block public HTTPS websites (such as `https://*.vercel.app`) from sending requests to local private IPs (`http://192.168.x.x`).

To enable seamless browser access:
1. Run `AUTO_SETUP_VERCEL.bat` or run:
   ```bash
   python tools/setup_vercel_pna.py
   ```
2. The helper automatically opens Chrome, Brave, or Edge with:
   ```
   --unsafely-treat-insecure-origin-as-secure="http://192.168.137.35"
   ```
3. Alternatively, in Chrome or Edge navigate to `chrome://flags` and set **Insecure origins treated as secure** to your ESP32's IP address.

---

## 7. Render Cloud Backend Deployment

A 24/7 cloud API gateway is provided in `backend/` to provide AI intelligence and token refreshing when the host PC is turned off.

1. In the [Render Dashboard](https://dashboard.render.com/), choose **New +** -> **Blueprint**.
2. Select your `bambu-telemetry-dashboard` repository.
3. Render automatically picks up `backend/render.yaml`:
   - **Runtime**: Python 3.11 with FastAPI / Uvicorn.
   - **Environment Variables**:
     - `NVIDIA_API_KEY`: NVIDIA NIM API Key.
     - `SPOTIFY_CLIENT_ID`: Spotify Developer Client ID.
     - `SPOTIFY_CLIENT_SECRET`: Spotify Developer Client Secret.
4. Access the cloud gateway at `https://stan-desk-station-backend.onrender.com/api/status`.

---

## 8. Spotify Media Integration Setup

1. Create an application in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard).
2. Set Redirect URI to `http://localhost:8888/callback`.
3. Run the permanent authorization tool:
   ```bash
   Pair_Spotify_Permanent.bat
   ```
   or:
   ```bash
   python tools/spotify_auth.py
   ```
4. Complete the authorization in your browser. Refresh tokens are saved to `.spotify_cache.json` for persistent unattended sync.

---

## 9. Troubleshooting & Diagnostics

| Symptom | Cause | Solution |
|---|---|---|
| Screen displays white screen | SPI wiring issue or inverted reset | Check GPIO 4 (RESET) and GPIO 2 (DC) connections; verify 3.3V power |
| Edge static / noise line | GRAM coordinate offset mismatch | Set correct tab flag or adjust offset in `src/main.ino` |
| Web app fails to reach ESP32 | Browser PNA block or wrong IP | Run `AUTO_SETUP_VERCEL.bat` or use `START_ADMIN_PANEL.bat` on localhost |
| MQTT disconnects from Bambu | TLS certificate or password mismatch | Verify Bambu Lab printer IP, Access Code, and Serial Number in config |
| Audio beat analyzer high latency | Default audio device mismatch | Verify WASAPI loopback device in Windows Sound settings |
