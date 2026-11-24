#pragma once
#include <Arduino.h>

struct SystemSettings {
  bool     theme_light  = false; // false = Dark Mode, true = Light Mode
  bool     clock_12h    = false; // false = 24-Hour, true = 12-Hour (AM/PM)
  uint8_t  buzzer_mode  = 0;     // 0 = Passive PWM, 1 = Active DC, 2 = Muted
  uint8_t  brightness   = 100;   // 10..100 %
  uint8_t  led_override = 0;     // 0 = Auto-sync, 1 = OFF, 2 = Rainbow, 3..7 = Presets, 8 = Custom, 9 = Music Sync
  uint8_t  led_r        = 255;
  uint8_t  led_g        = 255;
  uint8_t  led_b        = 255;
  // Alert bark policy (FINISH / FAILED / HMS):
  // 0 = auto-mute after the first notification burst
  // 1 = bark continuously until the user ACKs on the overlay (OK button)
  uint8_t  siren_mode   = 0;
  // Desk pet: emotion sound effects, music-vibe dancing, manual sickness
  uint8_t  pet_sounds      = 1;  // 1 = pet chirps/growls on mood changes
  uint8_t  pet_music_vibe  = 1;  // 1 = pet dances on screen while music plays
  uint8_t  pet_sick        = 0;  // 0 = healthy, 1 = sick, 2 = fever (manual)
  uint8_t  pet_show_stats  = 1;  // 1 = show bottom PC stats pill on pet, 0 = hide
  uint8_t  pet_show_music  = 1;  // 1 = show top now playing card on pet, 0 = hide
  char     default_screen[16] = "pet"; // pet, printer, pc_hud, weather, ai, spotify, image, clock
  char     spotify_refresh_token[160] = {0};
};

extern SystemSettings g_settings;

void settingsInit();
void settingsLoad();
void settingsSave();
