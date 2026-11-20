/* =====================================================================
 *  ui.h  -  display rendering task (pinned to Core 1) - 160x128 landscape
 * ===================================================================== */
#pragma once
#include <Arduino.h>

void uiTaskStart();

// Button events emitted by the peripherals task or web controller:
// 1 = menu press   2 = select / ok   3 = menu long-press
// 4 = back (menu double)  5 = select long-press
// 6 = UP           7 = DOWN          8 = LEFT (back/dec)
// 9 = RIGHT (enter/inc)   10 = HOME (return to home)
extern volatile uint8_t ui_button_event;
// Timestamp of menu press start (ms) for hold detection; 0 = released
extern volatile uint32_t ui_button_hold_start_ms;
// Latched result overlay: 0=none 1=print complete 2=print failed.
// Set by UI task on MQTT FINISH/FAILED; cleared ONLY by Select=OK.
extern volatile uint8_t ui_latched_result;

// True during active SPI transmission to the panel (used by peripherals task to reject crosstalk)
extern volatile bool g_tft_spi_busy;

// backlight percentage control (used by menu Brightness row)
uint8_t  uiGetBacklight();
void     uiSetBacklight(uint8_t pct);   // 40..100

// Framebuffer access for web screen mirroring (160x128 RGB565)
const uint16_t* uiGetFramebuffer();
void uiGetFrameSize(int& w, int& h);

// Custom image display (160x128 RGB565 and Direct-to-RAM JPEG)
uint16_t* uiGetCustomImageBuffer();
uint8_t*  uiGetCustomJpegBuffer(size_t max_len = 0);
void      uiSetCustomJpeg(const uint8_t* data, size_t len);
void uiActivateCustomImage();
void uiShowCustomImage(const uint16_t* pixels);
void uiClearCustomImage();
bool uiIsCustomImageActive();

// App Modes: 3D Printer MQTT Mode (default) vs Spotify Player Mode
enum AppMode : uint8_t {
  MODE_PRINTER = 0,
  MODE_SPOTIFY = 1
};

void uiSetAppMode(AppMode mode);
AppMode uiGetAppMode();

// Spotify display activation
void uiActivateSpotify();

// Desk Pet Mode (100 FPS procedural companion)
void uiActivateDeskPet();
bool uiIsPetMode();

// Theme and Clock Format
bool uiGetThemeLight();
void uiSetThemeLight(bool light);
bool uiGetClock12h();
void uiSetClock12h(bool is12h);

// Live Notification Overlay (from Web / Phone / Laptop)
void uiShowNotification(const char* title, const char* msg, uint32_t duration_ms = 4000);
