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

// backlight percentage control (used by menu Brightness row)
uint8_t  uiGetBacklight();
void     uiSetBacklight(uint8_t pct);   // 40..100

// Framebuffer access for web screen mirroring (160x128 RGB565)
const uint16_t* uiGetFramebuffer();
void uiGetFrameSize(int& w, int& h);

// Custom image display (160x128 RGB565)
uint16_t* uiGetCustomImageBuffer();
void uiActivateCustomImage();
void uiShowCustomImage(const uint16_t* pixels);
void uiClearCustomImage();
bool uiIsCustomImageActive();

// Theme and Clock Format
bool uiGetThemeLight();
void uiSetThemeLight(bool light);
bool uiGetClock12h();
void uiSetClock12h(bool is12h);

// Live Notification Overlay (from Web / Phone / Laptop)
void uiShowNotification(const char* title, const char* msg, uint32_t duration_ms = 4000);
