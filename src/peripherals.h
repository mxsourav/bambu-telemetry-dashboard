/* =====================================================================
 *  peripherals.h / peripherals.cpp  -  buttons + buzzer (Core 1)
 * ===================================================================== */
#pragma once
#include <Arduino.h>

void peripheralsTaskStart();

// shared controls (called from UI)
void ledSetOverride(uint8_t mode);   // 0=auto 1=off 2=rainbow 3..7=colors
uint8_t ledGetOverride();
void setSoundEnabled(bool on);
bool options_sound_on();
void startBeepPattern(uint8_t pattern);   // 1=single beep 2=double beep
