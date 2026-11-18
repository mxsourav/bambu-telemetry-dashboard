# Project Handover & Architectural Blueprint

**To:** GLM 5.3 Flash (Original Firmware Architect & Developer)  
**From:** Antigravity (Assistant Engineer)  
**Date:** November 18, 2026  
**Repository:** [github.com/mxsourav/bambu-telemetry-dashboard](https://github.com/mxsourav/bambu-telemetry-dashboard)  
**Branch:** `main` (Latest commit: `e433da0`)  
**Target Hardware:** ESP32-D0WD-V3 (ESP32-WROOM-32) + 1.8" ST7735 (160×128 SPI) Display  

---

## 1. Executive Summary & Context

This document is the official project handover back to you (GLM 5.3 Flash). Over the past sprints, the firmware and web interface have been significantly stabilized, optimized for memory efficiency, and shielded against hardware electrical anomalies.

The Bambu Lab MQTT telemetry stream is live, reliable, and responsive. However, the user requires **seamless, autonomous Spotify detection**: when they play music on their mobile phone, laptop, or desktop Spotify app, the desk monitor must **automatically detect playback, switch to the Fullscreen Spotify Cover Art player, and display track info and progress without requiring an open browser tab or background PC script**.

This document outlines:
1. Complete system architecture, pinout, memory maps, and task allocation.
2. Verified working subsystems and recent critical bug fixes.
3. The exact root-cause analysis of the Spotify playback detection gap.
4. A production-ready architectural implementation blueprint for you to execute autonomous Spotify playback on the device.

---

## 2. Hardware Specification & Pin Map

| Subsystem | GPIO | Function / Notes |
| :--- | :--- | :--- |
| **Display (VSPI)** | GPIO 17 | `PIN_TFT_MOSI` (40 MHz SPI write) |
| | GPIO 14 | `PIN_TFT_SCLK` (40 MHz SPI clock) |
| | GPIO 5 | `PIN_TFT_CS` (Chip Select) |
| | GPIO 23 | `PIN_TFT_DC` (Data / Command) |
| | GPIO 18 | `PIN_TFT_RST` (Reset) |
| | GPIO 26 | `PIN_TFT_BL` (Backlight control / 3.3V) |
| **Physical Buttons** | GPIO 4 | `PIN_BTN_MENU` (Active LOW, internal pull-up) |
| | GPIO 16 | `PIN_BTN_SELECT` (Active LOW, internal pull-up) |
| **Buzzer** | GPIO 19 | Dual-mode: Passive PWM (LEDC Ch 1) / Active DC |
| **Analog RGB Strip** | GPIO 27 | `PIN_LED_R` (LEDC Ch 2, 5 kHz PWM) |
| | GPIO 22 | `PIN_LED_G` (LEDC Ch 3, 5 kHz PWM) |
| | GPIO 21 | `PIN_LED_B` (LEDC Ch 4, 5 kHz PWM) |
| **MicroSD Card (HSPI)** | GPIO 15, 25, 13, 26 | CS=15, SCK=25, MOSI=13, MISO=26 (Dedicated HSPI bus) |

### Memory & Core Allocation (FreeRTOS Dual Core)

```
========================================================================================
                      ESP32 DUAL-CORE TASK ARCHITECTURE
========================================================================================

           CORE 0 (Networking & Web)                CORE 1 (UI & Peripherals)
  ┌────────────────────────────────────────┐ ┌────────────────────────────────────────┐
  │  [WiFi Stack & lwIP]                   │ │  [uiTask] (Priority 2, 4KB Stack)      │
  │  Radio sleep disabled (WiFi.setSleep(0)│ │  LovyanGFX 160x128 DMA Sprite          │
  │                                        │ │  30 FPS rendering pipeline             │
  │  [mqttTask] (Priority 1, 8KB Stack)    │ │  Fullscreen Spotify / Dashboards       │
  │  TLS 1.2 to Bambu Lab (Port 8883)      │ │                                        │
  │  PubSubClient buffer: 4096 bytes       │ │  [periTask] (Priority 2, 3KB Stack)    │
  │                                        │ │  6-sample 60ms button debouncer        │
  │  [webTask] (Priority 2, 8KB Stack)     │ │  SPI crosstalk shield (g_tft_spi_busy) │
  │  Async WebServer on :80                │ │  LEDC RGB strip FSM & Buzzer           │
  │  REST endpoints & RLE screen streaming │ │                                        │
  └────────────────────────────────────────┘ └────────────────────────────────────────┘
```

- **Current Free Heap**: ~108 KB to 112 KB free runtime internal heap.
- **Max Allocatable 8-Bit Block**: ~69 KB (critical for TLS handshakes).

---

## 3. Completed Features & Recent Stabilizations

### A. Bambu Lab TLS & Telemetry Sync
- **Heap Exhaustion Fix**: Removed static 40 KB `.bss` buffers from `ui.cpp` and `spotify_service.cpp`, reducing compile-time static RAM usage from 30.5% down to **18.1%** (59 KB). This freed >108 KB runtime heap, completely eliminating `MBEDTLS_ERR_SSL_ALLOC_FAILED (-32512)`.
- **Immediate Pushall Sync**: Firmware now publishes `{"pushing":{"sequence_id":"1","command":"pushall"}}` to `device/<SN>/request` immediately upon MQTT connection. Telemetry (temperatures, nozzle target, print percentage, layer count) updates instantly without waiting for printer state changes.

### B. Button Ghosting & EMI Crosstalk Elimination
- **Root Cause**: GPIO 16 (`PIN_BTN_SELECT`) is physically adjacent on the ESP32 die to GPIO 17 (`PIN_TFT_MOSI`), which runs at 40 MHz SPI. Because the internal pull-up is weak (~50 kΩ), SPI draw cycles induced capacitive voltage drops, causing 2-sample debounced false clicks (`event=2`) in an infinite feedback loop.
- **Fix Applied**:
  1. **6-Sample Filter**: Requires 6 consecutive 10 ms identical reads (60 ms continuous) for both press (`0x00`) and release (`0x3F`).
  2. **50 ms Minimum Hold Filter**: Rejects microsecond EMI spikes.
  3. **SPI Busy Lockout**: `g_tft_spi_busy` flag disables button sampling during `sprite.pushSprite()` DMA transfers.
  4. **Anti-Chatter Protection**: Auto-suppresses pin events if >3 transitions occur in 2 seconds.

### C. NVS Flash Wear & Web Server Protection
- **Smart Throttled Writes**: `settingsSave()` in `src/settings_manager.cpp` now performs in-memory delta comparisons before writing to NVS Preferences, throttled to a minimum 1.0s interval. This eliminates flash locking pauses that previously caused HTTP socket timeouts.
- **Web UI & D-Pad Remote**:
  - `web/index.html` features the VisionOS dark aesthetic with the matching physical-style D-Pad (`UP`, `DOWN`, `LEFT`, `RIGHT`, `OK`, `BACK`).
  - Screen streaming (`/api/control/screen_rle`) is paused when navigating away from the Remote tab and runs via chained 1000 ms delays, maintaining low network utilization on weak Wi-Fi connections (-85 dBm).

---

## 4. The Spotify Issue: Root Cause Analysis

### Why Music Doesn't Auto-Play on the Device

When the user plays music on their mobile phone or laptop Spotify app, the desk monitor does not switch to the Spotify player. Here is why:

1. **`spotifyPollLoop()` Is An Orphaned Function**:
   - In `src/spotify_service.cpp`, `spotifyPollLoop()` is defined, but **it is never called by any task in the entire codebase**. It does not exist in `main.cpp`, `webTask`, or `mqttTask`.
2. **Device Relies Purely on External Pushes**:
   - Currently, the firmware only switches to `VIEW_SPOTIFY` if it receives:
     - `POST /api/spotify/now` (title, artist, progress, duration, is_playing)
     - `POST /api/spotify/cover` (160×128 RGB565 binary)
   - These endpoints are only called when the user:
     - Has `web/index.html` open in an active browser tab (which polls Spotify Web API via client JS), OR
     - Runs the Windows background script `tools/spotify_desk_sync.py`.
   - **If the browser tab is closed and the PC script isn't running, the ESP32 has zero awareness of Spotify playback.**
3. **Empty Client Secret & Authentication Scope**:
   - In `src/spotify_service.cpp`:
     ```cpp
     static const char* SPOTIFY_CLIENT_ID = "bc80d4154155425a939097ea6079c225";
     static const char* SPOTIFY_CLIENT_SECRET = "";
     ```
   - `refreshAccessToken()` attempts to hit `https://accounts.spotify.com/api/token` using a standard refresh token grant, which requires a valid client secret (or PKCE authorization code parameters).
4. **The Dual-TLS Heap Hazard**:
   - The Bambu Lab MQTT client maintains a persistent TLS 1.2 connection on Core 0 (`mbedtls` context consumes ~35–45 KB).
   - If the ESP32 independently spins up a second `WiFiClientSecure` connection to `api.spotify.com` every 3 seconds to poll `/v1/me/player/currently-playing`, TLS memory overhead will trigger heap fragmentation, dropping the Bambu MQTT connection.

---

## 5. Production Implementation Blueprint for GLM 5.3 Flash

To make Spotify playback 100% autonomous—so the device auto-detects playback from any phone, laptop, or tablet on the user's Spotify account—implement the following production architecture.

### Architectural Recommendation: Lightweight Autonomous Poller on Core 0

Instead of spinning up and tearing down a full TLS connection every 3 seconds, implement a dedicated, persistent, memory-safe Spotify poller.

```
┌────────────────────────────────────────────────────────────────────────┐
│                   AUTONOMOUS SPOTIFY POLLING ENGINE                    │
└────────────────────────────────────────────────────────────────────────┘

  [1. Initial Setup via Web UI / Pair Tool]
    User logs in once via Spotify OAuth2 (PKCE or with Client Secret).
    Permanent Refresh Token is stored in NVS (`sp_rtoken`) and SD (`/settings.json`).

  [2. Execution in FreeRTOS Task (Core 0)]
    Task runs every 3.5s to 5.0s (adaptive backoff):
      - If NOT playing: Poll every 5s.
      - If PLAYING: Poll every 3s, locally interpolate progress_ms on Core 1 between polls.
      - If Token Expired: Refresh Access Token once per hour.

  [3. TLS Connection Optimization (Avoiding MbedTLS Heap Exhaustion)]
    Option A: Set MbedTLS buffer sizes in SDK / client:
              `client.setBufferSizes(1024, 512);`
              Reduces TLS RAM from 35 KB down to ~8 KB!
    Option B: Reuse a single persistent HTTPS socket (`Keep-Alive`) to api.spotify.com.

  [4. Auto-Switching UI State Machine]
    When `is_playing == true`:
      If current view != VIEW_SPOTIFY and not in Menu:
        Transition view -> VIEW_SPOTIFY
        Update title, artist, progress_ms, duration_ms
        Fetch and stream cover art thumbnail (56x56 or full 160x128)
    When `is_playing == false` or stopped for >15s:
      Transition view -> VIEW_HOME (smoothly revert to 3D printer dashboard)
```

---

### Step-by-Step Implementation Tasks

#### Task 1: Store & Validate Spotify Credentials in NVS
Ensure `src/secrets.h` or NVS stores:
- `SPOTIFY_CLIENT_ID`
- `SPOTIFY_CLIENT_SECRET` (or use Spotify PKCE Flow without secret)
- `SPOTIFY_REFRESH_TOKEN`

In `src/settings_manager.cpp`, verify `sp_rtoken` loads correctly at boot into `g_settings.spotify_refresh_token`.

#### Task 2: Implement Low-Memory TLS Poller
In `src/spotify_service.cpp`:
1. Use `client.setInsecure()` and `client.setBufferSizes(2048, 1024)` to minimize dynamic heap allocation.
2. In `refreshAccessToken()`:
   ```cpp
   // POST https://accounts.spotify.com/api/token
   // Header: Authorization: Basic base64(client_id:client_secret)
   // Body: grant_type=refresh_token&refresh_token=<token>
   ```
3. In `spotifyPollLoop()`:
   ```cpp
   // GET https://api.spotify.com/v1/me/player/currently-playing
   // Header: Authorization: Bearer <access_token>
   // 200 OK -> parse JSON: item.name, item.artists[0].name, progress_ms, duration_ms, is_playing, item.album.images[1].url
   // 204 No Content -> is_playing = false
   // 401 Unauthorized -> force refreshAccessToken()
   ```

#### Task 3: Schedule `spotifyPollLoop()` in `webTask` or Dedicated Task
In `src/web_server.cpp` (inside `webTask` on Core 0):
```cpp
// Inside while(true) loop of webTask:
server.handleClient();
spotifyPollLoop(); // Runs non-blocking check every 3.5s
vTaskDelay(pdMS_TO_TICKS(5));
```

#### Task 4: Cover Art Ingestion (Zero Additional Heap Allocation)
- When a track changes (`item.id != last_track_id`), download the 64×64 or 300×300 album image.
- Stream image bytes directly into `uiGetCustomImageBuffer()` via `JPEGDEC` or raw RGB565 stream without creating intermediate heap copies.
- If cover art is unavailable or loading, fallback to the stylized procedural vinyl/music disc graphic already implemented in `ui.cpp`.

#### Task 5: Core 1 Interpolation & Screen Transition
In `src/ui.cpp`:
- `spotifyIsActive()` is already wired to auto-switch to `VIEW_SPOTIFY`.
- Increment `g_spotify.progress_ms += frame_delta_ms` on Core 1 between network polls for butter-smooth progress bar animation at 30 FPS.
- When playback ends, auto-revert to `VIEW_HOME` after 12 seconds of inactivity.

---

## 6. Verification Protocol for Handover

When you (GLM 5.3 Flash) complete the implementation, verify with this checklist:
1. **Serial Stability**: Boot device, monitor COM11. Verify zero `[BTN] event=...` ghost clicks while idle.
2. **Dual-TLS Heap Check**: With Bambu Lab printing telemetry active (`IDLE`/`RUNNING`), trigger Spotify playback. Verify `ESP.getFreeHeap()` stays above **65 KB** and no TLS handshake failures occur.
3. **Autonomous Auto-Switch**:
   - Start Spotify on mobile phone (browser closed, no PC script running).
   - Confirm screen switches to `VIEW_SPOTIFY` within 4 seconds.
   - Confirm song title, artist, and progress bar advance smoothly.
   - Pause Spotify on mobile phone -> confirm screen reverts to `VIEW_HOME` after 12 seconds.
4. **Git Sync**: Pull latest `main` before building (`git pull origin main`). Do not modify `src/secrets.h` directly in commits.

---
*End of Handover Specification.*
