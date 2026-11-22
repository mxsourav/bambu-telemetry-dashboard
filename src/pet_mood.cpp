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
      deskPetSetTint(240, 32, 45, (uint8_t)(55 + anger_pct * 1.9f));
      deskPetSetBounce(0, 0);
      break;
    case PET_MOOD_FRUSTRATED:
      deskPetSetTint(235, 25, 40, (uint8_t)(95 + anger_pct * 1.6f));
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
        case 5:   // emotional / ballad: deep cyan-blue cheeks, gentle slow sway
          deskPetSetTint(90, 140, 240, 110);
          deskPetSetBounce(1.5f, 0.8f);
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
// Mood-entry sound effects: Muted per user requirement (no random beeps!).
// Buzzer is strictly reserved for boot chirp and deliberate button clicks.
// ---------------------------------------------------------------------
static void moodSoundEdge(PetMood prev, PetMood now) {
  (void)prev;
  (void)now;
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
// 1 = cool drill/trap nod, 2 = hype party/EDM, 3 = rock headbang,
// 4 = chill lofi sway, 5 = sad ballad tears, 0 = default pop groove.
static uint8_t vibeStyleFromGenre() {
  char buf[512] = {0};
  snprintf(buf, sizeof(buf), "%s %s %s %s",
           g_spotify.genre, g_pc_stats.music_genre, g_spotify.artist, g_spotify.title);
  for (char* c = buf; *c; ++c) *c = (char)tolower(*c);

  // 1. EDM / Dance / Rave / Party / Club / DJ (Priority 1: Party tracks ALWAYS vibe joyful!)
  if (strstr(buf, "party") || strstr(buf, "dance") || strstr(buf, "club") ||
      strstr(buf, "rave") || strstr(buf, "remix") || strstr(buf, "edm") ||
      strstr(buf, "house") || strstr(buf, "techno") || strstr(buf, "electro") ||
      strstr(buf, "trance") || strstr(buf, "hardstyle") || strstr(buf, "dj") ||
      strstr(buf, "bhangra") || strstr(buf, "dhol") || strstr(buf, "garmi") ||
      strstr(buf, "kala chashma") || strstr(buf, "ghungroo") || strstr(buf, "kar gayi chull") ||
      strstr(buf, "aankh marey") || strstr(buf, "chogada") || strstr(buf, "saturday saturday") ||
      strstr(buf, "nashe si chadh gayi") || strstr(buf, "kamariya") || strstr(buf, "jhoome jo") ||
      strstr(buf, "besharam rang") || strstr(buf, "lungi dance") || strstr(buf, "badtameez dil") ||
      strstr(buf, "subha hone") || strstr(buf, "high heels") || strstr(buf, "balam pichkari") ||
      strstr(buf, "baby doll") || strstr(buf, "chammak challo") || strstr(buf, "swag se swagat") ||
      strstr(buf, "dhoom") || strstr(buf, "mauja hi mauja") || strstr(buf, "tum hi ho bandhu") ||
      strstr(buf, "gallan goodiyaan") || strstr(buf, "london thumakda") || strstr(buf, "morni banke") ||
      strstr(buf, "dilliwaali") || strstr(buf, "dilliwali") || strstr(buf, "first class") ||
      strstr(buf, "sooraj dooba") || strstr(buf, "hookah bar") || strstr(buf, "nacho nacho") ||
      strstr(buf, "garrix") || strstr(buf, "avicii") || strstr(buf, "marshmello") ||
      strstr(buf, "skrillex") || strstr(buf, "tiesto") || strstr(buf, "alesso") ||
      strstr(buf, "guetta") || strstr(buf, "calvin harris") || strstr(buf, "alan walker") ||
      strstr(buf, "chainsmokers") || strstr(buf, "zedd") || strstr(buf, "nucleya") ||
      strstr(buf, "ritviz") || strstr(buf, "pitbull") || strstr(buf, "dua lipa")) return 2;

  // 2. Hip-Hop / Rap / Drill / Trap / Urban / R&B / Desi Hip-Hop
  if (strstr(buf, "hip-hop") || strstr(buf, "hip hop") || strstr(buf, "rap") ||
      strstr(buf, "drill") || strstr(buf, "trap") || strstr(buf, "grime") ||
      strstr(buf, "808") || strstr(buf, "r&b") || strstr(buf, "urban") ||
      strstr(buf, "sambata") || strstr(buf, "palti fire") || strstr(buf, "lal ranga") ||
      strstr(buf, "shob choritro") || strstr(buf, "bhooter raja") || strstr(buf, "juta abishkar") ||
      strstr(buf, "honey singh") || strstr(buf, "yo yo honey") || strstr(buf, "payal") ||
      strstr(buf, "anirudh") || strstr(buf, "hukum") || strstr(buf, "badass") ||
      strstr(buf, "jailer") || strstr(buf, "leo") || strstr(buf, "vikram") ||
      strstr(buf, "rolex") || strstr(buf, "master") || strstr(buf, "vathi") ||
      strstr(buf, "tauba tauba") || strstr(buf, "karan aujla") || strstr(buf, "aujla") ||
      strstr(buf, "cheques") || strstr(buf, "elevated") || strstr(buf, "we rollin") ||
      strstr(buf, "no love") || strstr(buf, "brown munde") || strstr(buf, "insane") ||
      strstr(buf, "winning speech") || strstr(buf, "antidote") || strstr(buf, "yeda yung") ||
      strstr(buf, "yung dsa") || strstr(buf, "shaggy dushing") || strstr(buf, "blood tears") ||
      strstr(buf, "rehmani keeda") || strstr(buf, "tadipaar") || strstr(buf, "insaan") ||
      strstr(buf, "basti ka hasti") || strstr(buf, "haath varthi") || strstr(buf, "snake") ||
      strstr(buf, "kohinoor") || strstr(buf, "mirchi") || strstr(buf, "chal bombay") ||
      strstr(buf, "3:59") || strstr(buf, "khatta flow") || strstr(buf, "namastute") ||
      strstr(buf, "nanchaku") || strstr(buf, "11k") || strstr(buf, "emiway") ||
      strstr(buf, "bantai") || strstr(buf, "divine") || strstr(buf, "mc stan") ||
      strstr(buf, "krsna") || strstr(buf, "raftaar") || strstr(buf, "seedhe maut") ||
      strstr(buf, "sidhu") || strstr(buf, "shubh") || strstr(buf, "dhillon") ||
      strstr(buf, "drake") || strstr(buf, "travis") || strstr(buf, "future") ||
      strstr(buf, "metro boomin") || strstr(buf, "21 savage") || strstr(buf, "savage") ||
      strstr(buf, "uzi") || strstr(buf, "carti") || strstr(buf, "eminem") ||
      strstr(buf, "kendrick") || strstr(buf, "kanye") || strstr(buf, "yeat") ||
      strstr(buf, "central cee") || strstr(buf, "pop smoke") || strstr(buf, "fivio") ||
      strstr(buf, "stormzy") || strstr(buf, "50 cent") || strstr(buf, "snoop") ||
      strstr(buf, "j. cole") || strstr(buf, "weeknd") || strstr(buf, "post malone")) return 1;

  // 3. Rock / Metal / Punk / Grunge / Bengali Rock
  if (strstr(buf, "rock") || strstr(buf, "metal") || strstr(buf, "punk") ||
      strstr(buf, "grunge") || strstr(buf, "fossils") || strstr(buf, "rupam islam") ||
      strstr(buf, "rupam") || strstr(buf, "cactus") || strstr(buf, "krosswindz") ||
      strstr(buf, "aaro ekbar") || strstr(buf, "hasnuhana") || strstr(buf, "lakkhichhara") ||
      strstr(buf, "moheener ghoraguli") || strstr(buf, "linkin") || strstr(buf, "nirvana") ||
      strstr(buf, "metallica") || strstr(buf, "queen") || strstr(buf, "ac/dc") ||
      strstr(buf, "slipknot") || strstr(buf, "system of a down") || strstr(buf, "greenday") ||
      strstr(buf, "green day") || strstr(buf, "foo fighters") || strstr(buf, "rammstein") ||
      strstr(buf, "arctic monkeys") || strstr(buf, "strokes") || strstr(buf, "radiohead") ||
      strstr(buf, "evanescence") || strstr(buf, "avenged sevenfold") || strstr(buf, "bring me the horizon")) return 3;

  // 4. Lo-Fi / Chill / Acoustic / Jazz / Ambient
  if (strstr(buf, "lofi") || strstr(buf, "lo-fi") || strstr(buf, "acoustic") ||
      strstr(buf, "jazz") || strstr(buf, "classical") || strstr(buf, "ambient") ||
      strstr(buf, "chill") || strstr(buf, "folk") || strstr(buf, "piano") ||
      strstr(buf, "study") || strstr(buf, "instrumental")) return 4;

  // 5. Explicit Emotional / Sad Ballads / Heartbreak (Only truly sad songs get tears)
  if (strstr(buf, "ballad") || strstr(buf, "sad version") || strstr(buf, "sad song") || strstr(buf, "heartbreak") ||
      strstr(buf, "crying") || strstr(buf, "tears") || strstr(buf, "goodbye") ||
      strstr(buf, "kal ho naa ho") || strstr(buf, "bela bose") || strstr(buf, "2441139") ||
      strstr(buf, "agar tum saath") || strstr(buf, "channa mereya") || strstr(buf, "tujhe bhula diya") ||
      strstr(buf, "tum hi ho") || strstr(buf, "sunn raha hai") || strstr(buf, "hamari adhuri") ||
      strstr(buf, "bekhayali") || strstr(buf, "pachtaoge") || strstr(buf, "duaa") ||
      strstr(buf, "alvida") || strstr(buf, "tadap tadap") || strstr(buf, "bhula dena") ||
      strstr(buf, "phir bhi tumko") || strstr(buf, "ranjish hi sahi") || strstr(buf, "faasle") ||
      strstr(buf, "woh lamhe") || strstr(buf, "kyun main jaagoon") || strstr(buf, "chithi na koi") ||
      strstr(buf, "kahani suno") || strstr(buf, "kaifi khalil") || strstr(buf, "iraaday") ||
      strstr(buf, "kinna sona") || strstr(buf, "mann bharryaa") || strstr(buf, "shei tumi") ||
      strstr(buf, "boba tunnel") || strstr(buf, "someone like you") || strstr(buf, "easy on me") ||
      strstr(buf, "someone you loved") || strstr(buf, "drivers license") || strstr(buf, "glimpse of us") ||
      strstr(buf, "slow dancing in the dark") || strstr(buf, "die with a smile")) return 5;

  return 0; // Pop / Commercial / Upbeat Groove
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
    if (s_forced_mood == PET_MOOD_ANGRY || s_forced_mood == PET_MOOD_FRUSTRATED) {
      s_anger += 4.5f * 0.05f; // progressive red over ~22 seconds
      if (s_anger > 100.0f) s_anger = 100.0f;
    } else {
      if (s_anger > 0.0f) {
        s_anger -= 3.0f * 0.05f;
        if (s_anger < 0.0f) s_anger = 0.0f;
      }
    }
    applyMoodVisuals(s_forced_mood, s_anger, vibeEnergy());
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

  // anger: +4.5%/s while angry, -3%/s decaying when calm
  static uint32_t s_last_ramp_ms = 0;
  uint32_t dt = (s_last_ramp_ms && ms > s_last_ramp_ms) ? (ms - s_last_ramp_ms) : 16;
  if (dt > 200) dt = 200;
  s_last_ramp_ms = ms;
  if (failing || s_current == PET_MOOD_ANGRY || s_current == PET_MOOD_FRUSTRATED) {
    s_anger += 4.5f * (float)dt / 1000.0f;
    if (s_anger > 100.0f) s_anger = 100.0f;
  } else if (s_anger > 0.0f) {
    s_anger -= 3.0f * (float)dt / 1000.0f;
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
  } else if (overtemp) {
    // 7b: dangerous over-temp
    want = PET_MOOD_SCARED;
  } else if (temp_anom && (ms - s_temp_anom_since_ms > 45000)) {
    // 7: heating is stuck far below target
    want = PET_MOOD_SAD;
  } else if (music && g_settings.pet_music_vibe) {
    // 6: active music playback: high priority dance groove!
    want = PET_MOOD_DANCING;
  } else if (finished) {
    // 8: completion party, then lingering happiness until OK
    want = (ms - s_finish_since_ms < 4000) ? PET_MOOD_EXCITED : PET_MOOD_HAPPY;
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
    if (night) {
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
    // Hand control back to the desk_pet idle chain (sleepy/sleeping/deep_sleep)
    if (s_current != PET_MOOD_NEUTRAL && s_current != PET_MOOD_SLEEPY &&
        s_current != PET_MOOD_SLEEPING && s_current != PET_MOOD_DEEP_SLEEP &&
        s_current != PET_MOOD_YAWN) {
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
  deskPetSetMood(want, 0);   // persistent mood! Never times out unexpectedly to pop eyes open
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
  if (!strcmp(mood_name, "neutral") || !strcmp(mood_name, "calm") || !strcmp(mood_name, "idle")) {
    s_forced_until_ms = 0;
    s_anger = 0;
    deskPetSetMood(PET_MOOD_NEUTRAL, 0);
    return;
  }
  PetMood m = PET_MOOD_NEUTRAL;
  if (!strcmp(mood_name, "happy"))     m = PET_MOOD_HAPPY;
  else if (!strcmp(mood_name, "wink")) m = PET_MOOD_WINK;
  else if (!strcmp(mood_name, "sleepy")) m = PET_MOOD_SLEEPY;
  else if (!strcmp(mood_name, "sleeping")) m = PET_MOOD_SLEEPING;
  else if (!strcmp(mood_name, "deep_sleep")) m = PET_MOOD_DEEP_SLEEP;
  else if (!strcmp(mood_name, "yawn")) m = PET_MOOD_YAWN;
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
  if (seconds < 1) seconds = 10;
  if (seconds > 86400) seconds = 86400;
  uiActivateDeskPet(); // switch display to desk pet so user sees the reaction immediately
  s_forced_mood = m;
  s_forced_until_ms = millis() + seconds * 1000UL;
  deskPetSetMood(m, seconds * 1000UL);
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
           "\"genre\":\"%s\",\"vibe_style\":%d,"
           "\"sounds\":%d,\"night\":%s,\"weather_c\":%.1f,\"sick\":%d,"
           "\"hour\":\"%02d:%02d\"}",
           deskPetMoodName(deskPetGetMood()),
           (int)s_anger,
           spotifyIsActive() ? "true" : "false",
           g_settings.pet_music_vibe,
           g_spotify.genre[0] ? g_spotify.genre : "Auto",
           vibeStyleFromGenre(),
           g_settings.pet_sounds,
           (have_time && (t.tm_hour >= 22 || t.tm_hour < 7)) ? "true" : "false",
           pcStatsIsActive() ? (double)g_pc_stats.weather_temp_c : -99.0,
           g_settings.pet_sick,
           have_time ? t.tm_hour : 0, have_time ? t.tm_min : 0);
}
