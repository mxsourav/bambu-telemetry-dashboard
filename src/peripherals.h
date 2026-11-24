/* =====================================================================
 *  peripherals.h / peripherals.cpp  -  buttons + buzzer (Core 1)
 * ===================================================================== */
#pragma once
#include <Arduino.h>

void peripheralsTaskStart();

// shared controls (called from UI and Web REST)
enum LedOverrideMode : uint8_t {
  LED_MODE_AUTO    = 0,  // Auto-sync: follows 3D printer, or auto music sync when music plays
  LED_MODE_OFF     = 1,  // Forced OFF
  LED_MODE_RAINBOW = 2,  // Smooth rainbow cycle
  LED_MODE_RED     = 3,  // Preset Red
  LED_MODE_GREEN   = 4,  // Preset Green
  LED_MODE_BLUE    = 5,  // Preset Blue
  LED_MODE_WHITE   = 6,  // Preset White
  LED_MODE_AMBER   = 7,  // Preset Amber
  LED_MODE_CUSTOM  = 8,  // User-defined RGB / Album Art
  LED_MODE_MUSIC   = 9,  // Dedicated Music Sync mode (beat pulsating flash & genre colors)
};

void ledSetOverride(uint8_t mode);   // 0=auto 1=off 2=rainbow 3..7=colors 8=custom 9=music
uint8_t ledGetOverride();
void ledSetCustomRgb(uint8_t r, uint8_t g, uint8_t b);
void ledGetCustomRgb(uint8_t& r, uint8_t& g, uint8_t& b);
void ledSetBrightness(uint8_t pct);
uint8_t ledGetBrightness();
void ledSetInverted(bool inv);
bool ledGetInverted();
void setSoundEnabled(bool on);
bool options_sound_on();
uint8_t getBuzzerMode();                     // 0=Passive PWM, 1=Active DC, 2=Mute
void setBuzzerMode(uint8_t mode);
void startBeepPattern(uint8_t pattern);   // 1=chime notification 2=fanfare notification 3=click
void beepButton();                        // gentle low-volume button click beep
void beepPet();                           // playful 2-tone pet chirp
void playHardwareBeep(uint32_t freq, uint32_t duration_ms); // direct synchronous test pulse
void triggerSirenTest(uint32_t duration_ms = 3000); // test emergency rhythmic siren
