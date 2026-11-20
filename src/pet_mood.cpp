#include "pet_mood.h"
#include "desk_pet.h"
#include "telemetry.h"
#include "pc_stats.h"
#include "settings_manager.h"
#include "spotify_service.h"
#include "peripherals.h"
#include "ui.h"
#include "config.h"

#include <time.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

// =====================================================================
//  pet_mood.cpp - The Desk Pet's emotional brain
// =====================================================================
//  Priority ladder (highest wins):
//    9  print FAILED latch  -> ANGRY, ramps red over ~50s, escalates
//                              ANGRY > 30s -> FRUSTRATED > 60s -> CRYING
//    8  print FINISH latch  -> EXCITED party -> HAPPY until OK
//    7  temp anomalies      -> SAD (worried) / SCARED (over-temp)
//    6  weather (PC sync)   -> COLD <10C, HOT >32C, rain -> SAD
//    5  sickness            -> SICK / FEVER (manual or rare auto day)
//    4  music playing       -> DANCING to the beat (if vibe enabled)
//    3  night 22:00-07:00   -> SLEEPING
//    2  long print running  -> THINKING (watching)
//    2.5 anger grudge decay -> SAD while the anger fades after OK
//    1  idle                -> engine yields (desk_pet idle chain)
// =====================================================================

static float    s_anger = 0.0f;            // 0..100 gradual red ramp
static uint32_t s_failed_since_ms = 0;     // 0 = no active failure
static uint32_t s_finish_since_ms = 0;     // 0 = no active completion
static uint32_t s_temp_anom_since_ms = 0;  // nozzle temp anomaly persistence
static uint32_t s_print_since_ms = 0;      // current print start (watch)
static PetMood  s_current = PET_MOOD_NEUTRAL;
static uint32_t s_surprise_until_ms = 0;   // track-change surprise window
static uint32_t s_celebrate_until_ms = 0;  // post-overlay-ACK reaction
static uint8_t  s_celebrate_kind = 0;
static uint32_t s_forced_until_ms = 0;     // web-forced mood window
static PetMood  s_forced_mood = PET_MOOD_NEUTRAL;
static char     s_last_title[24] = {0};    // track-change detection
static bool     s_led_applied = false;     // LED mirroring active
static int      s_auto_sick_done_day = -1; // auto-sick fires once per day
static uint32_t s_last_sound_ms = 0;

// ---------------------------------------------------------------------
// Mood -> background atmosphere tint (RGB) + vibe bounce parameters
// ---------------------------------------------------------------------
static uint8_t vibeStyleFromGenre();   // fwd: defined below (helpers)

static void applyMoodVisuals(PetMood mood, float anger_pct, float vibe_energy) {
  switch (mood) {
    case PET_MOOD_ANGRY:
      deskPetSetTint(235, 40, 40, (uint8_t)(70 + anger_pct * 1.8f));
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_FRUSTRATED:
      deskPetSetTint(220, 30, 30, (uint8_t)(80 + anger_pct * 1.7f));
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_CRYING:
      deskPetSetTint(90, 120, 220, 140);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_SAD:
      deskPetSetTint(110, 130, 210, 90);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_SICK:
      deskPetSetTint(110, 200, 110, 120);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_FEVER:
      deskPetSetTint(235, 55, 55, 210);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_COLD:
      deskPetSetTint(90, 150, 255, 150);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_HOT:
      deskPetSetTint(255, 130, 60, 150);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_EXCITED:
      deskPetSetTint(255, 120, 160, 120);
      deskPetSetBounce(5.0f, 3.2f);
      deskPetSetVibeStyle(2);
      break;
    case PET_MOOD_DANCING: {
      // Energy + genre decide HOW it dances: drill nods cool with
      // half-lidded eyes, rock headbangs, lofi sways, EDM bounces hard.
      uint8_t style = vibeStyleFromGenre();
      deskPetSetVibeStyle(style);
      switch (style) {
        case 1:   // drill / trap: cool purple cheeks, slow deep nod
          deskPetSetTint(150, 110, 230, 90);
          deskPetSetBounce(5.0f, 1.3f);
          break;
        case 2:   // EDM / house: hype, fast big bounces
          deskPetSetTint(255, 110, 170, 110);
          deskPetSetBounce(4.5f + vibe_energy * 2.0f, 3.4f);
          break;
        case 3:   // rock / metal: red cheeks, relentless headbang
          deskPetSetTint(255, 80, 60, 110);
          deskPetSetBounce(6.0f, 2.6f);
          break;
        case 4:   // lofi / chill: blue cheeks, lazy sway
          deskPetSetTint(140, 170, 255, 80);
          deskPetSetBounce(1.5f, 0.9f);
          break;
        default:  // hip-hop / pop: standard groove scaled by energy
          deskPetSetTint(255, 110, 170, 100);
          deskPetSetBounce(3.0f + vibe_energy * 4.0f, 2.0f + vibe_energy * 1.2f);
          break;
      }
      break;
    }
    case PET_MOOD_SCARED:
      deskPetSetTint(150, 150, 180, 80);
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_THINKING:
      deskPetSetTint(150, 150, 170, 45);
      deskPetSetBounce(0, 0);
      break;
    default:
      deskPetSetTint(255, 140, 170, 60);   // faint healthy pink cheeks
      deskPetSetBounce(0, 0);
      break;
  }
}

// ---------------------------------------------------------------------
// Mood-entry sound effects (rate limited; respects sound settings)
// ---------------------------------------------------------------------
static void moodSoundEdge(PetMood prev, PetMood now) {
  if (prev == now) return;
  if (!g_settings.pet_sounds || getBuzzerMode() == 2) return;
  uint32_t ms = millis();
  if (ms - s_last_sound_ms < 3000) return;
  s_last_sound_ms = ms;

  switch (now) {
    case PET_MOOD_EXCITED: startBeepPattern(2); break;           // fanfare
    case PET_MOOD_ANGRY:     playHardwareBeep(BUZZER_FREQ_LO, 90); break;
    case PET_MOOD_FRUSTRATED: playHardwareBeep(900, 110); break;
    case PET_MOOD_CRYING:    playHardwareBeep(1400, 110); break; // sad low tone
    case PET_MOOD_SCARED:    playHardwareBeep(2500, 60); break;  // startled squeak
    case PET_MOOD_DANCING:   beepPet(); break;                   // happy chirp
    default: break;
  }
}

// ---------------------------------------------------------------------
// LED strip mirrors strong pet emotions while the user has auto mode
// ---------------------------------------------------------------------
static void ledMirrorEdge(PetMood now) {
  bool strong = (now == PET_MOOD_ANGRY || now == PET_MOOD_FRUSTRATED ||
                 now == PET_MOOD_CRYING || now == PET_MOOD_COLD ||
                 now == PET_MOOD_SICK  || now == PET_MOOD_FEVER ||
                 now == PET_MOOD_HOT   || now == PET_MOOD_DANCING);
  if (strong && ledGetOverride() == 0 && !s_led_applied) {
    uint8_t r = 255, g = 140, b = 170;
    switch (now) {
      case PET_MOOD_ANGRY: case PET_MOOD_FRUSTRATED: r = 255; g = 30; b = 30; break;
      case PET_MOOD_CRYING:   r = 40; g = 80;  b = 255; break;
      case PET_MOOD_COLD:     r = 60; g = 140; b = 255; break;
      case PET_MOOD_SICK:     r = 60; g = 220; b = 80;  break;
      case PET_MOOD_FEVER:    r = 255; g = 40; b = 40;  break;
      case PET_MOOD_HOT:      r = 255; g = 110; b = 30; break;
      case PET_MOOD_DANCING:  r = 180; g = 60; b = 255; break;
      default: break;
    }
    ledSetCustomRgb(r, g, b);
    ledSetOverride(8);
    s_led_applied = true;
  } else if (!strong && s_led_applied) {
    ledSetOverride(0);   // hand the strip back to auto-sync
    s_led_applied = false;
  }
}

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
static int currentHour() {
  struct tm t;
  if (getLocalTime(&t, 0)) return t.tm_hour;
  return 12; // NTP not synced yet: treat as daytime
}

static int currentDayNum() {
  struct tm t;
  if (getLocalTime(&t, 0)) return (int)(t.tm_yday + t.tm_year * 366);
  return 0;
}

static float vibeEnergy() {
  return (pcStatsIsActive() && g_pc_stats.music_vibe > 0.0f)
         ? g_pc_stats.music_vibe : 0.5f;
}

// Map the track genre to a dance style:
// 1 = cool drill/trap nod, 2 = hype rave, 3 = rock headbang,
// 4 = chill lofi sway, 0 = default groove.
// Genre source: Spotify artist API on-device (primary); the PC sync
// script's lookup is the fallback while playing YouTube/media outside
// Spotify (where Spotify has no metadata).
static uint8_t vibeStyleFromGenre() {
  const char* g = g_spotify.genre[0] ? g_spotify.genre
                                     : g_pc_stats.music_genre;
  if (!g[0]) return 0;
  if (g == g_pc_stats.music_genre && !pcStatsIsActive()) return 0;
  char buf[24];
  strncpy(buf, g, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  for (char* c = buf; *c; ++c) *c = (char)tolower(*c);
  if (strstr(buf, "drill") || strstr(buf, "trap") || strstr(buf, "grime") ||
      strstr(buf, "dubstep") || strstr(buf, "getto") || strstr(buf, "ghetto")) return 1;
  if (strstr(buf, "rock") || strstr(buf, "metal") || strstr(buf, "punk") ||
      strstr(buf, "grunge")) return 3;
  if (strstr(buf, "lofi") || strstr(buf, "lo-fi") || strstr(buf, "acoustic") ||
      strstr(buf, "jazz") || strstr(buf, "classical") || strstr(buf, "ambient") ||
      strstr(buf, "chill") || strstr(buf, "folk") || strstr(buf, "indie") ||
      strstr(buf, "piano")) return 4;
  if (strstr(buf, "house") || strstr(buf, "techno") || strstr(buf, "electro") ||
      strstr(buf, "edm") || strstr(buf, "dance") || strstr(buf, "drum") ||
      strstr(buf, "trance") || strstr(buf, "hardstyle")) return 2;
  return 0;   // hip-hop / rap / pop / everything else: standard groove
}

static bool weatherDescHas(const char* keys) {
  const char* d = g_pc_stats.weather_desc;
  if (!d[0]) return false;
  char buf[16];
  strncpy(buf, d, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  for (char* c = buf; *c; ++c) *c = (char)tolower(*c);
  char tmp[16];
  strncpy(tmp, keys, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = 0;
  for (char* k = strtok(tmp, ","); k; k = strtok(nullptr, ",")) {
    if (strstr(buf, k)) return true;
  }
  return false;
}

// ---------------------------------------------------------------------
// Main per-frame update
// ---------------------------------------------------------------------
void petMoodUpdate(uint32_t ms, const TelemetryData& d) {
  // A transient click reaction owns the face briefly - yield to it.
  if (deskPetIsTransient()) return;
  if (ms < s_surprise_until_ms) return;   // track-change surprise window

  // ---- forced web mood has top priority while its window is open ----
  if (s_forced_until_ms > ms) {
    applyMoodVisuals(s_forced_mood, 0, vibeEnergy());
    deskPetSetMood(s_forced_mood, (s_forced_until_ms - ms) + 200);
    ledMirrorEdge(s_forced_mood);
    moodSoundEdge(s_current, s_forced_mood);
    s_current = s_forced_mood;
    return;
  }

  // ---- post-overlay-ACK celebration / relief ----
  if (s_celebrate_until_ms > ms) {
    PetMood m = (s_celebrate_kind == 1) ? PET_MOOD_EXCITED : PET_MOOD_SAD;
    applyMoodVisuals(m, 0, vibeEnergy());
    deskPetSetMood(m, (s_celebrate_until_ms - ms) + 200);
    moodSoundEdge(s_current, m);
    s_current = m;
    return;
  }
  s_celebrate_kind = 0;

  // ---- gradual anger bookkeeping ----
  bool failing = (ui_latched_result == 2);
  if (failing && s_failed_since_ms == 0) s_failed_since_ms = ms;
  if (!failing) s_failed_since_ms = 0;

  // anger: +2%/s while the failure is unacknowledged, -1%/s decaying
  static uint32_t s_last_ramp_ms = 0;
  uint32_t dt = (s_last_ramp_ms && ms > s_last_ramp_ms) ? (ms - s_last_ramp_ms) : 16;
  if (dt > 200) dt = 200;
  s_last_ramp_ms = ms;
  if (failing) {
    s_anger += 2.0f * (float)dt / 1000.0f;
    if (s_anger > 100.0f) s_anger = 100.0f;
  } else if (s_anger > 0.0f) {
    s_anger -= 1.0f * (float)dt / 1000.0f;
    if (s_anger < 0.0f) s_anger = 0.0f;
  }

  // ---- FINISH latch bookkeeping ----
  bool finished = (ui_latched_result == 1);
  if (finished && s_finish_since_ms == 0) s_finish_since_ms = ms;
  if (!finished) s_finish_since_ms = 0;

  // ---- print duration (watching) ----
  if (d.state == PST_RUNNING) {
    if (s_print_since_ms == 0) s_print_since_ms = ms;
  } else {
    s_print_since_ms = 0;
  }

  // ---- nozzle temperature anomaly persistence ----
  bool temp_anom = ((d.state == PST_RUNNING || d.state == PST_PREPARE) &&
                    d.nozzle_target >= 100 && d.nozzle_temp < d.nozzle_target * 0.5f);
  bool overtemp  = (d.nozzle_temp > 330.0f);
  if (temp_anom && s_temp_anom_since_ms == 0) s_temp_anom_since_ms = ms;
  if (!temp_anom) s_temp_anom_since_ms = 0;

  // ---- weather inputs ----
  float wtemp = g_pc_stats.weather_temp_c;
  bool weather_ok = pcStatsIsActive() && wtemp > -50.0f && wtemp < 60.0f;
  int hour = currentHour();
  bool night = (hour >= 22 || hour < 7);
  bool raining = weather_ok && weatherDescHas("rain,drizzle,thunder,storm,snow");

  // ---- music ----
  bool music = spotifyIsActive();

  // ---- evaluate the priority ladder ----
  PetMood want = PET_MOOD_NEUTRAL;
  float anger_for_visual = s_anger;

  if (failing) {
    // 9: failure ramp - escalates if ignored
    uint32_t held = ms - s_failed_since_ms;
    if (held > 60000)     want = PET_MOOD_CRYING;
    else if (held > 30000) want = PET_MOOD_FRUSTRATED;
    else                   want = PET_MOOD_ANGRY;
  } else if (finished) {
    // 8: completion party, then lingering happiness until OK
    want = (ms - s_finish_since_ms < 4000) ? PET_MOOD_EXCITED : PET_MOOD_HAPPY;
  } else if (overtemp) {
    // 7b: dangerous over-temp
    want = PET_MOOD_SCARED;
  } else if (temp_anom && (ms - s_temp_anom_since_ms > 45000)) {
    // 7: heating is stuck far below target
    want = PET_MOOD_SAD;
  } else if (weather_ok && wtemp < 10.0f) {
    want = PET_MOOD_COLD;
  } else if (weather_ok && wtemp > 32.0f) {
    want = PET_MOOD_HOT;
  } else if (g_settings.pet_sick == 2) {
    want = PET_MOOD_FEVER;
  } else if (g_settings.pet_sick == 1) {
    want = PET_MOOD_SICK;
  } else {
    // rare auto sick-day: ~1 of every 3 days, one morning window,
    // fires once per day (also serves as a "mood swing")
    int dn = currentDayNum();
    if (dn != s_auto_sick_done_day && (dn % 3) == 0 && hour >= 10 && hour < 12) {
      want = PET_MOOD_SICK;
      s_auto_sick_done_day = dn;
    }
  }

  if (want == PET_MOOD_NEUTRAL) {
    if (music && g_settings.pet_music_vibe) {
      want = PET_MOOD_DANCING;                       // 4: vibe to the music
    } else if (night) {
      want = PET_MOOD_SLEEPING;                      // 3: bed time
    } else if (raining) {
      want = PET_MOOD_SAD;                           // 6b: gloomy rain
    } else if (d.state == PST_RUNNING && s_print_since_ms > 0 &&
               (ms - s_print_since_ms > 3600000UL)) {
      want = PET_MOOD_THINKING;                      // 2: long print watch
    } else if (s_anger > 20.0f) {
      want = PET_MOOD_SAD;                           // 2.5: lingering grudge
    }
  }

  // ---- apply / yield ----
  if (want == PET_MOOD_NEUTRAL) {
    // Hand control back to the desk_pet idle chain (sleepy/sleeping)
    if (s_current != PET_MOOD_NEUTRAL && s_current != PET_MOOD_SLEEPY &&
        s_current != PET_MOOD_SLEEPING) {
      deskPetSetMood(PET_MOOD_NEUTRAL, 0);
      moodSoundEdge(s_current, PET_MOOD_NEUTRAL);
    }
    if (s_anger <= 0.0f) deskPetSetTint(255, 140, 170, 0);
    s_current = PET_MOOD_NEUTRAL;
    ledMirrorEdge(PET_MOOD_NEUTRAL);
    return;
  }

  // Track change while vibing: brief startled surprise, keep the groove
  if (want == PET_MOOD_DANCING && g_spotify.title[0]) {
    if (strncmp(s_last_title, g_spotify.title, sizeof(s_last_title) - 1) != 0) {
      strncpy(s_last_title, g_spotify.title, sizeof(s_last_title) - 1);
      s_last_title[sizeof(s_last_title) - 1] = 0;
      if (s_current == PET_MOOD_DANCING) {   // only surprise on CHANGE
        deskPetSetMood(PET_MOOD_SURPRISED, 800);
        s_surprise_until_ms = ms + 850;
        return;
      }
    }
  }

  applyMoodVisuals(want, anger_for_visual, vibeEnergy());
  deskPetSetMood(want, 1200);   // continuous refresh keeps engine in charge
  moodSoundEdge(s_current, want);
  ledMirrorEdge(want);
  s_current = want;
}

// ---------------------------------------------------------------------
// Web API
// ---------------------------------------------------------------------
void petMoodForce(const char* mood_name, uint32_t seconds) {
  if (!mood_name || !mood_name[0]) return;
  if (!strcmp(mood_name, "treat") || !strcmp(mood_name, "cure")) {
    g_settings.pet_sick = 0;
    settingsSave();
    s_forced_until_ms = 0;
    s_anger = 0;
    deskPetSetMood(PET_MOOD_HAPPY, 2500);   // grateful smile
    s_surprise_until_ms = millis() + 2600;
    return;
  }
  PetMood m = PET_MOOD_NEUTRAL;
  if (!strcmp(mood_name, "happy"))     m = PET_MOOD_HAPPY;
  else if (!strcmp(mood_name, "wink")) m = PET_MOOD_WINK;
  else if (!strcmp(mood_name, "sleepy")) m = PET_MOOD_SLEEPY;
  else if (!strcmp(mood_name, "sleeping")) m = PET_MOOD_SLEEPING;
  else if (!strcmp(mood_name, "surprised")) m = PET_MOOD_SURPRISED;
  else if (!strcmp(mood_name, "angry")) m = PET_MOOD_ANGRY;
  else if (!strcmp(mood_name, "frustrated")) m = PET_MOOD_FRUSTRATED;
  else if (!strcmp(mood_name, "sad")) m = PET_MOOD_SAD;
  else if (!strcmp(mood_name, "crying")) m = PET_MOOD_CRYING;
  else if (!strcmp(mood_name, "sick")) m = PET_MOOD_SICK;
  else if (!strcmp(mood_name, "fever")) m = PET_MOOD_FEVER;
  else if (!strcmp(mood_name, "cold")) m = PET_MOOD_COLD;
  else if (!strcmp(mood_name, "hot")) m = PET_MOOD_HOT;
  else if (!strcmp(mood_name, "excited")) m = PET_MOOD_EXCITED;
  else if (!strcmp(mood_name, "dancing")) m = PET_MOOD_DANCING;
  else if (!strcmp(mood_name, "scared")) m = PET_MOOD_SCARED;
  else if (!strcmp(mood_name, "thinking")) m = PET_MOOD_THINKING;
  else return;
  if (seconds < 1 || seconds > 600) seconds = 10;
  s_forced_mood = m;
  s_forced_until_ms = millis() + seconds * 1000UL;
}

void petMoodOnLeave() {
  s_forced_until_ms = 0;
  s_surprise_until_ms = 0;
  if (s_led_applied) {
    ledSetOverride(0);          // hand the strip back to auto-sync
    s_led_applied = false;
  }
  s_current = PET_MOOD_NEUTRAL;
}

void petMoodCelebrate(uint8_t kind) {
  s_celebrate_kind = kind;
  s_celebrate_until_ms = millis() + (kind == 1 ? 5000 : 8000);
}

void petMoodStatusJson(char* out, size_t len) {
  struct tm t;
  bool have_time = getLocalTime(&t, 0);
  snprintf(out, len,
           "{\"mood\":\"%s\",\"anger\":%d,\"music\":%s,\"vibe\":%d,"
           "\"sounds\":%d,\"night\":%s,\"weather_c\":%.1f,\"sick\":%d,"
           "\"hour\":\"%02d:%02d\"}",
           deskPetMoodName(deskPetGetMood()),
           (int)s_anger,
           spotifyIsActive() ? "true" : "false",
           g_settings.pet_music_vibe,
           g_settings.pet_sounds,
           (have_time && (t.tm_hour >= 22 || t.tm_hour < 7)) ? "true" : "false",
           pcStatsIsActive() ? (double)g_pc_stats.weather_temp_c : -99.0,
           g_settings.pet_sick,
           have_time ? t.tm_hour : 0, have_time ? t.tm_min : 0);
}
