/* =====================================================================
 *  ui.h  -  display rendering task (pinned to Core 1) - 160x128 landscape
 * ===================================================================== */
#pragma once
#include <Arduino.h>

void uiTaskStart();

// Button events emitted by the peripherals task (single-fire, no ghost):
// 1 = menu press   2 = select press   3 = menu long-press
// 4 = menu double-press (-> home)     5 = select long-press
extern volatile uint8_t ui_button_event;
// Timestamp of menu press start (ms) for hold detection; 0 = released
extern volatile uint32_t ui_button_hold_start_ms;
// Latched result overlay: 0=none 1=print complete 2=print failed.
// Set by UI task on MQTT FINISH/FAILED; cleared ONLY by Select=OK.
extern volatile uint8_t ui_latched_result;

// backlight percentage control (used by menu Brightness row)
uint8_t  uiGetBacklight();
void     uiSetBacklight(uint8_t pct);   // 40..100
