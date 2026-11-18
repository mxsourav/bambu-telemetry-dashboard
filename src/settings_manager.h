#pragma once
#include <Arduino.h>

struct SystemSettings {
  bool     theme_light  = false; // false = Dark Mode, true = Light Mode
  bool     clock_12h    = false; // false = 24-Hour, true = 12-Hour (AM/PM)
  uint8_t  buzzer_mode  = 0;     // 0 = Passive PWM, 1 = Active DC, 2 = Muted
  uint8_t  brightness   = 100;   // 10..100 %
  uint8_t  led_override = 0;     // 0 = Auto-sync, 1 = OFF, 2 = Rainbow, 3..7 = Presets
  uint8_t  led_r        = 255;
  uint8_t  led_g        = 255;
  uint8_t  led_b        = 255;
  char     spotify_refresh_token[160] = {0};
};

extern SystemSettings g_settings;

void settingsInit();
void settingsLoad();
void settingsSave();
