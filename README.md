# STΔN studio. - ESP32 3D Printer Monitor and Autonomous Desk Companion

Phase 2 Stable Release.

An embedded firmware for ESP32 powering an autonomous desk companion, Bambu Lab 3D printer telemetry monitor, PC hardware HUD, and synchronized media visualizer with an ST7735 1.8-inch TFT display.

---

## What is New in Phase 2

Phase 2 transitions the system from a passive 3D printer monitor into an active, multi-modal autonomous desktop companion.

| Feature Area | Phase 1 (Legacy Telemetry Monitor) | Phase 2 (Autonomous Desk Pet and Studio) |
|---|---|---|
| Primary Role | Dedicated Bambu Lab MQTT monitor | Dual-engine: Bambu telemetry plus autonomous companion |
| Display Pipeline | Standard Adafruit GFX canvas at 15-20 FPS | LovyanGFX double-buffered vector pipeline at 100 FPS |
| Desk Pet Engine | Not present | 11 parametric emotional states with Disney eyelid easing |
| Music Visualization | Not present | Windows Media Foundation bridge with genre-specific dance engine |
| PC Telemetry | Not present | Real-time CPU, GPU, RAM, and thermals HUD |
| Sleep Progression | Fixed screen timeout | Dynamic daytime ladder (5m drowsy, 7m yawn, 8m sleep, 12m deep sleep) |
| Web Platform | Basic status text endpoint | visionOS-inspired mobile PWA with live RLE display mirror |
| Storage and OTA | Basic SD mount | Full FAT32 file manager, SD asset loading, and Web OTA |
| Audio Feedback | Single beep tones | Non-blocking PWM audio engine with state-specific chirps |

---

## Visual Overview

### Autonomous Desk Pet and Mood Engine
The desk companion runs at 100 FPS on FreeRTOS Core 1, rendering anti-aliased squircles and expressive parametric eyelids.

| Neutral State | Dance Vibe (Hip-Hop) | Cry Response |
|---|---|---|
| ![Neutral](docs/images/desk_pet_neutral.png) | ![Dancing](docs/images/desk_pet_dancing.png) | ![Crying](docs/images/desk_pet_crying.png) |

| Rage / Warning | Calm State |
|---|---|
| ![Angry](docs/images/desk_pet_angry.png) | ![Calm](docs/images/desk_pet_calm.png) |

### Telemetry and Media Integration

| Bambu 3D Printer Monitor | PC Hardware HUD | Spotify / Media Visualizer |
|---|---|---|
| ![Printer](docs/images/bambu_printer_telemetry.png) | ![PC HUD](docs/images/pc_hardware_hud.png) | ![Spotify](docs/images/spotify_media_sync.png) |

---

## System Architecture

```
+-------------------------------------------------------------------------+
|                               HOST PC / MOBILE                          |
|                                                                         |
|  +---------------------------+       +-------------------------------+  |
|  |   spotify_desk_sync.py    |       |   STΔN studio. Web PWA        |  |
|  |  (Windows Media + psutil) |       |  (Mobile Chrome / Safari)     |  |
|  +-------------+-------------+       +---------------+---------------+  |
+----------------|-------------------------------------|------------------+
                 | HTTP POST /api/spotify/now          | HTTP GET/POST
                 | HTTP POST /api/pc/stats             | WebSocket / RLE
                 v                                     v
+-------------------------------------------------------------------------+
|                                ESP32 FIRMWARE                           |
|                                                                         |
|  Core 0 (Networking and Services):                                      |
|    - FreeRTOS HTTP Server (REST API, RLE Screen Streamer, Web OTA)      |
|    - Bambu Lab MQTT Client (SSL/TLS, JSON payload parser)               |
|    - WiFi Station Manager and Reconnection Loop                         |
|                                                                         |
|  Core 1 (Graphics and Real-Time Interaction):                           |
|    - LovyanGFX 100 FPS Vector Rendering Pipeline                       |
|    - Autonomous Pet State Machine (11 Moods, Eyelid Blinks, Sleep)      |
|    - Genre-Aware Vibe Engine (Hip-Hop, Drill, Rock, EDM, Lo-Fi)         |
|    - Non-blocking PWM Tone Generator                                    |
|    - Hardware Debounced Button Interrupt Handlers                       |
+-------------------------------------------------------------------------+
                 | SPI (40 MHz)      | SPI (4 MHz)     | GPIO
                 v                   v                 v
          +-------------+     +-------------+   +---------------+
          | ST7735 LCD  |     | MicroSD FAT |   | WS2812B / Spk |
          |  (160x128)  |     | File System |   | Menu / Select |
          +-------------+     +-------------+   +---------------+
```

---

## Hardware Pinout and Wiring

Target board: ESP32 DevKit v1 (30-pin, dual-core Tensilica Xtensa LX6 @ 240 MHz).

### ST7735 1.8-inch TFT Display (SPI)
| Display Pin | ESP32 GPIO | Description |
|---|---|---|
| VCC | 3.3V or 5V | Power supply (matches module regulator) |
| GND | GND | Ground |
| CS | GPIO 5 | Chip Select |
| RESET | GPIO 4 | Hardware Reset |
| A0 / DC | GPIO 2 | Data / Command Control |
| SDA / MOSI | GPIO 23 | VSPI MOSI (Hardware SPI) |
| SCK / SCLK | GPIO 18 | VSPI SCK (Hardware SPI @ 40 MHz) |
| LED / BL | 3.3V | Backlight power |

### MicroSD Card Reader (HSPI)
| MicroSD Pin | ESP32 GPIO | Description |
|---|---|---|
| CS | GPIO 15 | HSPI Chip Select |
| SCK | GPIO 25 | Software/Hardware SPI Clock |
| MOSI | GPIO 13 | Master Out Slave In |
| MISO | GPIO 26 | Master In Slave Out |
| VCC | 3.3V / 5V | Power supply |
| GND | GND | Ground |

### Peripherals and Inputs
| Component | ESP32 GPIO | Configuration |
|---|---|---|
| Menu Button | GPIO 4 | Active LOW, internal pull-up |
| Select Button | GPIO 16 | Active LOW, internal pull-up |
| Status LED | GPIO 27 | WS2812B Addressable RGB strip |
| Buzzer | GPIO 14 | Passive buzzer driven by LEDC PWM |

---

## Desk Pet Emotional States and Timing

The autonomous pet operates on an internal state machine driven by sensory triggers, incoming telemetry, and idle timers:

| Mood State | Trigger Condition | Visual Appearance |
|---|---|---|
| Neutral | Default idle state | Wide open squircle eyes with specular highlights |
| Happy | Print success / Good thermals | Inverted arc smiling eyes with bouncing cheeks |
| Sad | Print failure / Low PC memory | Lowered gaze with bottom specular glints |
| Crying | Critical error / Print cancel | Solid crystal anime blue body with tear flow |
| Angry | High CPU/GPU temp (>85C) | Crimson body with diagonal aggressive brows |
| Surprised | Sudden telemetry delta | Enlarged pupils with raised curved brows |
| Wink | Button click confirmation | Asymmetric single eye wink with smirk |
| Sleepy | 5 minutes idle (300s) | Half-lidded drooping eyelids |
| Yawning | 7 minutes idle (420s) | Elliptical mouth expansion with eye squint |
| Sleeping | 8 minutes idle (480s) | Closed curved eye slots with slow breath cycle |
| Deep Sleep | 12 minutes idle (720s) | Pulsing sZz indicator with minimal redraws |

Wakeup triggers: Any physical button press, incoming music packet, print state transition, or REST API call resets the idle timer immediately to 0.

---

## Music Dance and Genre Classification

The companion daemon (`tools/spotify_desk_sync.py`) monitors the Windows Media Control Session and Spotify desktop instances. It classifies genres and pushes real-time dynamic cues to the ESP32:

- Hip-Hop / Drill / Trap: 11.0px downward head nod, 6.0px lateral lean, dynamic squash and stretch, half-lidded attitude gaze, and beatbox mouth pulses.
- Rock / Metal: 13.5px rapid vertical headbang with aggressive brow angle.
- EDM / Dance: 10.0px high-frequency double-hop with wide specular flashes.
- Lo-Fi / Chill: 4.5px gentle sinusoidal sway with relaxed eyelid curves.
- Acoustic / Ballad: 2.5px slow breathing rhythm with steady gaze.

---

## REST API Reference

The ESP32 runs a multi-threaded HTTP server on port 80.

### Pet and Mood Control
- `POST /api/pet/mood`: Update mood manually.
  - Body: `{"mood": "happy"}` (Options: neutral, happy, sad, crying, angry, surprised, wink, sleepy, yawn, sleeping, deep_sleep)
- `GET /api/pet/mood`: Read current mood and idle ticks.
  - Response: `{"mood": "neutral", "idle_ms": 12400, "fps": 99.8}`

### Media and PC Telemetry Push
- `POST /api/spotify/now`: Push current playback metadata and vibe style.
  - Body: `{"track": "Song Title", "artist": "Artist", "is_playing": true, "genre": "hip-hop", "bpm": 140}`
- `POST /api/pc/stats`: Push PC hardware monitor statistics.
  - Body: `{"cpu_pct": 24, "gpu_pct": 42, "ram_pct": 58, "cpu_temp": 48.5, "gpu_temp": 52.0}`

### Remote Display and Framebuffer
- `GET /api/control/screen`: Fetch full 160x128 16-bit BMP framebuffer capture.
- `GET /api/control/screen_rle`: Stream run-length encoded (RLE) delta stream for web canvas mirror.

### System and Management
- `GET /api/status`: Return WiFi RSSI, free heap, printer connection state, and temperatures.
- `POST /api/control/button`: Inject virtual button click (`{"button": "menu"}` or `{"button": "select"}`).
- `POST /update`: Multipart firmware upload for Web OTA flashing.

---

## Installation and Flashing

### 1. Build from Source with PlatformIO
Clone the repository and compile the firmware:
```bash
git clone https://github.com/mxsourav/bambu-telemetry-dashboard.git
cd bambu-telemetry-dashboard
pio run
```

### 2. Flash to ESP32
Connect your ESP32 via USB and upload:
```bash
pio run -t upload --upload-port COM11
```
Alternatively, flash the binary directly using esptool:
```bash
python -m esptool --chip esp32 --port COM11 --baud 460800 write-flash 0x10000 .pio/build/esp32dev/firmware.bin
```

### 3. Run the PC Companion Daemon
Install required Python dependencies and start the media bridge:
```bash
pip install winsdk psutil requests urllib3
python tools/spotify_desk_sync.py
```

### 4. Open the Web PWA
Navigate to the local IP of your ESP32 in any web browser:
```
http://192.168.137.171
```

For native standalone installation on mobile Chrome:
1. Open `chrome://flags` in mobile Chrome.
2. Search for `insecure` in the search box (do not type the `#` character).
3. Set `Insecure origins treated as secure` to `Enabled`.
4. Enter `http://192.168.137.171` in the text box.
5. Tap `Relaunch`.
6. Visit `http://192.168.137.171` and tap `Install app`.

---

## License

This project is open-source under the MIT License.
