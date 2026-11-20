#include "desk_pet.h"
#include "config.h"
#include "pc_stats.h"
#include <math.h>

// =====================================================================
//  desk_pet.cpp - 100 FPS High-Framerate Procedural Desk Pet Engine
// =====================================================================
//  Inspired by Anki Vector, Cozmo, and Dieter Rams minimalist design:
//  - Crisp paper-white background (0xFFFF)
//  - Deep inky-black eyes (0x0000)
//  - Anti-aliased subpixel Signed Distance Field (SDF) rasterization
//  - Parametric eyelid carving (joyful crescents, angry brows, sad droops)
//  - Organic saccadic glances, natural blinks, sinusoidal breathing physics
//  - Music groove: rhythmic beat-bounce and squash-and-stretch
//  - Zero tacky clip-art (no fake headphones, no cartoon notes, no lips)
// =====================================================================

static PetMood  s_mood = PET_MOOD_NEUTRAL;
static uint32_t s_mood_until_ms = 0;

// Gaze physics (saccadic glances)
static float s_curr_look_x = 0.0f;
static float s_curr_look_y = 0.0f;
static float s_target_look_x = 0.0f;
static float s_target_look_y = 0.0f;
static uint32_t s_next_glance_ms = 0;

// Blink physics
static bool     s_is_blinking = false;
static uint32_t s_blink_start_ms = 0;
static uint32_t s_next_blink_ms = 0;
static const uint32_t BLINK_TOTAL_MS = 130;

// Reaction bounce physics
static uint32_t s_bounce_start_ms = 0;
static uint32_t s_pet_active_since_ms = 0;
static uint32_t s_transient_until_ms = 0;

// Music vibe bounce parameters
static float    s_vibe_amp = 0.0f;         // bounce amplitude (px)
static float    s_vibe_hz  = 2.0f;         // bounce frequency (Hz)
static uint8_t  s_vibe_style = 0;          // 0=groove 1=nod 2=hype 3=rock 4=chill

void deskPetInit() {
  s_mood = PET_MOOD_NEUTRAL;
  s_mood_until_ms = 0;
  s_curr_look_x = 0.0f;
  s_curr_look_y = 0.0f;
  s_target_look_x = 0.0f;
  s_target_look_y = 0.0f;
  s_next_glance_ms = millis() + 2000;
  s_next_blink_ms  = millis() + 3200;
  s_is_blinking    = false;
  s_bounce_start_ms = 0;
  s_transient_until_ms = 0;
  s_vibe_amp = 0.0f;
  s_vibe_hz = 2.0f;
  s_vibe_style = 0;
  s_pet_active_since_ms = millis();
}

void deskPetSetMood(PetMood mood, uint32_t duration_ms) {
  s_mood = mood;
  s_mood_until_ms = (duration_ms > 0) ? (millis() + duration_ms) : 0;
  s_pet_active_since_ms = millis();
}

PetMood deskPetGetMood() {
  return s_mood;
}

// Background tint stub - strictly preserved as pure paper white (0xFFFF)
void deskPetSetTint(uint8_t /*r*/, uint8_t /*g*/, uint8_t /*b*/, uint8_t /*amount*/) {
  // Purposely empty: prevents any pink/purple/red tint bleeding into the paper background
}

void deskPetSetBounce(float amp_px, float freq_hz) {
  s_vibe_amp = amp_px;
  s_vibe_hz  = (freq_hz > 0.5f && freq_hz < 8.0f) ? freq_hz : 2.0f;
}

void deskPetSetVibeStyle(uint8_t style) {
  s_vibe_style = (style <= 4) ? style : 0;
}

bool deskPetIsTransient() {
  return millis() < s_transient_until_ms;
}

void deskPetReactClick() {
  if (s_mood == PET_MOOD_SLEEPING || s_mood == PET_MOOD_SLEEPY) {
    // Startled wakeup! Wide alert eyes
    s_mood = PET_MOOD_SURPRISED;
    s_mood_until_ms = millis() + 900;
  } else {
    // Cheerful happy squint and joyful bounce
    s_mood = PET_MOOD_HAPPY;
    s_mood_until_ms = millis() + 1600;
  }
  s_bounce_start_ms = millis();
  s_transient_until_ms = millis() + 1600;
  s_pet_active_since_ms = millis();
}

const char* deskPetMoodName(PetMood mood) {
  switch (mood) {
    case PET_MOOD_HAPPY:      return "happy";
    case PET_MOOD_WINK:       return "wink";
    case PET_MOOD_SLEEPY:     return "sleepy";
    case PET_MOOD_SLEEPING:   return "sleeping";
    case PET_MOOD_SURPRISED:  return "surprised";
    case PET_MOOD_ANGRY:      return "angry";
    case PET_MOOD_FRUSTRATED: return "frustrated";
    case PET_MOOD_SAD:        return "sad";
    case PET_MOOD_CRYING:     return "crying";
    case PET_MOOD_SICK:       return "sick";
    case PET_MOOD_FEVER:      return "fever";
    case PET_MOOD_COLD:       return "cold";
    case PET_MOOD_HOT:        return "hot";
    case PET_MOOD_EXCITED:    return "excited";
    case PET_MOOD_DANCING:    return "dancing";
    case PET_MOOD_SCARED:     return "scared";
    case PET_MOOD_THINKING:   return "thinking";
    default:                  return "neutral";
  }
}

// Convert 8-bit grayscale intensity to RGB565 for anti-aliasing against white (0xFFFF)
static inline uint16_t grayToRgb565(uint8_t v) {
  uint16_t r5 = (v * 31) / 255;
  uint16_t g6 = (v * 63) / 255;
  uint16_t b5 = (v * 31) / 255;
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

// ---------------------------------------------------------------------
// Bold Rounded-Cap Upward Smiling Crescent Arc (Happy / Joy / Dance)
// Modeled on Anki Vector / Cozmo joyful squint: thick, organic, anti-aliased
// ---------------------------------------------------------------------
static void drawHappyEyeAA(LGFX_Sprite& sprite, float cx, float cy) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const float r = 19.5f;
  const float th = 9.5f;
  const float half_th = th * 0.5f;
  const float arc_cy = cy + 13.0f;
  const float ang_start = 28.0f;
  const float ang_end   = 152.0f;

  // Precompute end-cap center coordinates in screen space (screen y points downward)
  const float a0_rad = ang_start * 0.0174532925f;
  const float a1_rad = ang_end   * 0.0174532925f;
  const float p0_x = cx + r * cosf(a0_rad);
  const float p0_y = arc_cy - r * sinf(a0_rad);
  const float p1_x = cx + r * cosf(a1_rad);
  const float p1_y = arc_cy - r * sinf(a1_rad);

  const int x_min = (int)fmaxf(0.0f, cx - 22.0f);
  const int x_max = (int)fminf(159.0f, cx + 22.0f);
  const int y_min = (int)fmaxf(0.0f, cy - 14.0f);
  const int y_max = (int)fminf(127.0f, cy + 15.0f);

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float dx = px - cx;
      float dy = arc_cy - py; // dy > 0 above arc center
      float dist = sqrtf(dx * dx + dy * dy);

      float ang_deg = atan2f(dy, dx) * 57.29578f;
      if (ang_deg < 0.0f) ang_deg += 360.0f;

      float d;
      if (ang_deg >= ang_start && ang_deg <= ang_end) {
        d = fabsf(dist - r) - half_th;
      } else {
        // Distance to closest rounded end-cap
        float d0 = sqrtf((px - p0_x) * (px - p0_x) + (py - p0_y) * (py - p0_y)) - half_th;
        float d1 = sqrtf((px - p1_x) * (px - p1_x) + (py - p1_y) * (py - p1_y)) - half_th;
        d = fminf(d0, d1);
      }

      if (d <= -0.5f) {
        row[x] = 0x0000; // Solid inky black
      } else if (d < 0.5f) {
        uint8_t v = (uint8_t)((d + 0.5f) * 255.0f);
        uint16_t col = grayToRgb565(v);
        if (col < row[x]) row[x] = col;
      }
    }
  }
}

// ---------------------------------------------------------------------
// Anti-Aliased Parametric Rounded Squircle / Capsule Eye
// Supports upper eyelid angled cuts (angry inward slope, sad droop, drowsy)
// ---------------------------------------------------------------------
static void drawCapsuleEyeAA(LGFX_Sprite& sprite, float cx, float cy,
                             float w, float h, float r_corner,
                             float brow_slope = 0.0f, float brow_apex_y = 999.0f) {
  if (w <= 0.0f || h <= 0.0f) return;
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  if (r_corner > w * 0.5f) r_corner = w * 0.5f;
  if (r_corner > h * 0.5f) r_corner = h * 0.5f;

  const float bx = (w * 0.5f) - r_corner;
  const float by = (h * 0.5f) - r_corner;

  const int x_min = (int)fmaxf(0.0f, cx - (w * 0.5f) - 1.5f);
  const int x_max = (int)fminf(159.0f, cx + (w * 0.5f) + 1.5f);
  const int y_min = (int)fmaxf(0.0f, cy - (h * 0.5f) - 1.5f);
  const int y_max = (int)fminf(127.0f, cy + (h * 0.5f) + 1.5f);

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float dx = px - cx;
      float dy = py - cy;

      // Rounded rect SDF
      float ox = fmaxf(0.0f, fabsf(dx) - bx);
      float oy = fmaxf(0.0f, fabsf(dy) - by);
      float d_outer = sqrtf(ox * ox + oy * oy) - r_corner;
      float d_inner = fminf(fmaxf(fabsf(dx) - bx, fabsf(dy) - by), 0.0f) - r_corner;
      float d_shape = (fabsf(dx) > bx || fabsf(dy) > by) ? d_outer : d_inner;

      // Upper eyelid cutoff (if active)
      float d_cut = -999.0f;
      if (brow_apex_y < 900.0f) {
        // brow line equation: y_brow = brow_apex_y + brow_slope * dx
        // pixels with dy < brow_line are above the eyelid and cut off
        float brow_line = brow_apex_y + brow_slope * dx;
        d_cut = brow_line - dy; // positive when above eyelid
      }

      float d_total = fmaxf(d_shape, d_cut);

      if (d_total <= -0.5f) {
        row[x] = 0x0000;
      } else if (d_total < 0.5f) {
        uint8_t v = (uint8_t)((d_total + 0.5f) * 255.0f);
        uint16_t col = grayToRgb565(v);
        if (col < row[x]) row[x] = col;
      }
    }
  }
}

// ---------------------------------------------------------------------
// Primary Desk Pet Frame Drawing (100 FPS)
// ---------------------------------------------------------------------
void deskPetDrawFrame(LGFX_Sprite& sprite, uint32_t ms) {
  // Pure Paper White Background (0xFFFF) - Crisp, Dieter Rams minimalism
  sprite.fillSprite(0xFFFF);

  if (s_pet_active_since_ms == 0) s_pet_active_since_ms = ms;

  // Expire temporary mood
  if (s_mood_until_ms > 0) {
    if (ms >= s_mood_until_ms) {
      s_mood_until_ms = 0;
    }
  } else {
    // Idle progression: neutral -> sleepy (45s) -> sleeping (60s)
    uint32_t idle_ms = ms - s_pet_active_since_ms;
    if (idle_ms >= 60000) {
      s_mood = PET_MOOD_SLEEPING;
    } else if (idle_ms >= 45000) {
      s_mood = PET_MOOD_SLEEPY;
    } else {
      s_mood = PET_MOOD_NEUTRAL;
    }
  }

  bool is_vibe = (s_mood == PET_MOOD_DANCING || s_mood == PET_MOOD_EXCITED);
  if (!is_vibe) s_vibe_amp *= 0.92f; // smooth decay out of music bounce

  // 1. Saccadic Glance State Machine
  if (!is_vibe && ms >= s_next_glance_ms) {
    s_next_glance_ms = ms + 2400 + (esp_random() % 2800);
    uint32_t pick = esp_random() % 7;
    switch (pick) {
      case 0: s_target_look_x =   0.0f; s_target_look_y =   0.0f; break; // Center
      case 1: s_target_look_x = -13.0f; s_target_look_y =   0.0f; break; // Left
      case 2: s_target_look_x =  13.0f; s_target_look_y =   0.0f; break; // Right
      case 3: s_target_look_x =  -9.0f; s_target_look_y =  -7.0f; break; // Up-Left curious
      case 4: s_target_look_x =   9.0f; s_target_look_y =  -7.0f; break; // Up-Right curious
      case 5: s_target_look_x =   0.0f; s_target_look_y =  -8.0f; break; // Up attentive
      default:s_target_look_x =   0.0f; s_target_look_y =   5.0f; break; // Down modest
    }
  }
  if (s_mood == PET_MOOD_THINKING) {
    s_target_look_x = -9.0f;
    s_target_look_y = -8.0f;
  }

  // Smooth exponential glide to glance target
  float glide = (s_mood == PET_MOOD_THINKING) ? 0.04f : 0.12f;
  s_curr_look_x += (s_target_look_x - s_curr_look_x) * glide;
  s_curr_look_y += (s_target_look_y - s_curr_look_y) * glide;

  // 2. Natural Blink State Machine
  if (!s_is_blinking && ms >= s_next_blink_ms) {
    s_is_blinking = true;
    s_blink_start_ms = ms;
  }

  float blink_factor = 1.0f; // 1.0 = fully open, 0.0 = fully closed
  if (s_is_blinking) {
    uint32_t elapsed = ms - s_blink_start_ms;
    if (elapsed < BLINK_TOTAL_MS) {
      if (elapsed < 45) {
        // Fast snap close (0..45ms)
        blink_factor = 1.0f - ((float)elapsed / 45.0f);
      } else {
        // Elastic ease-out open (45..130ms)
        float t = (float)(elapsed - 45) / 85.0f;
        blink_factor = t;
      }
    } else {
      s_is_blinking = false;
      blink_factor = 1.0f;
      s_next_blink_ms = ms + 3000 + (esp_random() % 3500);
      if ((esp_random() % 6) == 0) {
        s_next_blink_ms = ms + 220; // double blink
      }
    }
  }

  // 3. Natural Breathing Physics
  float breath_period = (s_mood == PET_MOOD_SLEEPING || s_mood == PET_MOOD_SLEEPY) ? 0.0012f : 0.0028f;
  float breath = sinf((float)ms * breath_period) * 1.5f;

  // 4. Click Reaction Bounce Physics
  float bounce_y = 0.0f;
  if (s_bounce_start_ms > 0) {
    uint32_t b_elapsed = ms - s_bounce_start_ms;
    if (b_elapsed < 350) {
      float t = (float)b_elapsed / 350.0f;
      bounce_y = -sinf(t * 3.14159f) * 6.0f;
    } else {
      s_bounce_start_ms = 0;
    }
  }

  // 5. Music Beat Groove & Squash-and-Stretch (Dancing)
  float vibe_y = 0.0f;
  float sway_x = 0.0f;
  float squash_w = 0.0f;
  float squash_h = 0.0f;

  if (is_vibe && s_vibe_amp > 0.2f) {
    float ph = (float)ms * 0.001f * s_vibe_hz * 6.283185f;
    float beat_sin = sinf(ph);
    // Head bobs downward on the downbeat:
    vibe_y = -fabsf(beat_sin) * s_vibe_amp;
    sway_x = sinf(ph * 0.5f) * 3.5f; // gentle rhythmic side-to-side sway

    // Squash & stretch physics: compress height and widen on the beat bounce
    float beat_impact = 1.0f - fabsf(beat_sin); // 1.0 at lowest bounce point
    squash_w = beat_impact * 2.5f;
    squash_h = -beat_impact * 4.0f;
  }

  // 6. Eye Coordinates (Centered on 160x128 display)
  // Base dimensions: 34w x 46h, 13r
  const float left_cx  = 53.0f + s_curr_look_x + sway_x;
  const float right_cx = 107.0f + s_curr_look_x + sway_x;
  const float center_y = 62.0f + s_curr_look_y + bounce_y + vibe_y;

  // Handle blink compression:
  float eye_w = 34.0f + squash_w;
  float eye_h = (46.0f + breath + squash_h) * blink_factor;
  if (eye_h < 4.0f) eye_h = 4.0f; // min height is a clean slit
  float eye_r = (eye_h < 13.0f) ? (eye_h * 0.5f) : 13.0f;

  // 7. Expressive Mood Rendering
  switch (s_mood) {
    case PET_MOOD_HAPPY: {
      if (blink_factor < 0.95f) {
        // Blinking through happy
        drawCapsuleEyeAA(sprite, left_cx, center_y, eye_w, eye_h, eye_r);
        drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r);
      } else {
        drawHappyEyeAA(sprite, left_cx, center_y);
        drawHappyEyeAA(sprite, right_cx, center_y);
      }
      break;
    }

    case PET_MOOD_DANCING: {
      // Grooving to the beat!
      if (s_vibe_style == 1 || s_vibe_style == 3) {
        // Cool nod / headbang: half-lidded focused eyes
        drawCapsuleEyeAA(sprite, left_cx, center_y, eye_w, eye_h, eye_r, 0.0f, -4.0f);
        drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r, 0.0f, -4.0f);
      } else {
        // Joyful smiling eyes bouncing in rhythm
        if (blink_factor < 0.95f) {
          drawCapsuleEyeAA(sprite, left_cx, center_y, eye_w, eye_h, eye_r);
          drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r);
        } else {
          drawHappyEyeAA(sprite, left_cx, center_y);
          drawHappyEyeAA(sprite, right_cx, center_y);
        }
      }
      break;
    }

    case PET_MOOD_EXCITED: {
      drawHappyEyeAA(sprite, left_cx, center_y);
      drawHappyEyeAA(sprite, right_cx, center_y);
      break;
    }

    case PET_MOOD_WINK: {
      drawHappyEyeAA(sprite, left_cx, center_y);
      drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r);
      break;
    }

    case PET_MOOD_SLEEPING: {
      // Peaceful resting horizontal capsules
      drawCapsuleEyeAA(sprite, left_cx, center_y + breath, 34.0f, 5.0f, 2.5f);
      drawCapsuleEyeAA(sprite, right_cx, center_y + breath, 34.0f, 5.0f, 2.5f);
      break;
    }

    case PET_MOOD_SLEEPY: {
      // Heavy drowsy eyelids (flat cut on upper lid)
      drawCapsuleEyeAA(sprite, left_cx, center_y, eye_w, eye_h, eye_r, 0.0f, -4.0f);
      drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r, 0.0f, -4.0f);
      break;
    }

    case PET_MOOD_SURPRISED:
    case PET_MOOD_SCARED: {
      // Dilated alert capsules
      float surp_h = (52.0f + breath) * blink_factor;
      if (surp_h < 4.0f) surp_h = 4.0f;
      drawCapsuleEyeAA(sprite, left_cx, center_y, 38.0f, surp_h, 16.0f);
      drawCapsuleEyeAA(sprite, right_cx, center_y, 38.0f, surp_h, 16.0f);
      break;
    }

    case PET_MOOD_ANGRY:
    case PET_MOOD_FRUSTRATED: {
      // Inward slanted brow cut: Left eye slopes down-right, Right eye slopes down-left
      float jitter_x = (s_mood == PET_MOOD_FRUSTRATED) ? sinf((float)ms * 0.05f) * 1.0f : 0.0f;
      drawCapsuleEyeAA(sprite, left_cx + jitter_x, center_y, eye_w, eye_h, eye_r,  0.45f, -5.0f);
      drawCapsuleEyeAA(sprite, right_cx + jitter_x, center_y, eye_w, eye_h, eye_r, -0.45f, -5.0f);
      break;
    }

    case PET_MOOD_SAD:
    case PET_MOOD_CRYING: {
      // Outward sloped puppy-dog brow cut: Left eye slopes down-left, Right eye slopes down-right
      drawCapsuleEyeAA(sprite, left_cx, center_y, eye_w, eye_h, eye_r, -0.45f, -5.0f);
      drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r,  0.45f, -5.0f);
      break;
    }

    case PET_MOOD_SICK:
    case PET_MOOD_FEVER:
    case PET_MOOD_COLD:
    case PET_MOOD_HOT: {
      // Unwell squint / drowsy droop
      float shiver = (s_mood == PET_MOOD_COLD) ? sinf((float)ms * 0.09f) * 1.5f : 0.0f;
      drawCapsuleEyeAA(sprite, left_cx + shiver, center_y, eye_w, eye_h * 0.65f, eye_r * 0.65f, 0.0f, -2.0f);
      drawCapsuleEyeAA(sprite, right_cx + shiver, center_y, eye_w, eye_h * 0.65f, eye_r * 0.65f, 0.0f, -2.0f);
      break;
    }

    case PET_MOOD_THINKING:
    case PET_MOOD_NEUTRAL:
    default: {
      // Pure Dieter Rams / Anki Vector rounded squircle capsule eyes
      drawCapsuleEyeAA(sprite, left_cx, center_y, eye_w, eye_h, eye_r);
      drawCapsuleEyeAA(sprite, right_cx, center_y, eye_w, eye_h, eye_r);
      break;
    }
  }

  // 8. Compact PC Hardware & Weather Pill (if PC stats stream is active)
  if (pcStatsIsActive()) {
    const int pill_x = 12;
    const int pill_y = 106;
    const int pill_w = 136;
    const int pill_h = 17;
    const int pill_r = 8;

    // Subtle off-white / light-gray capsule on paper white
    sprite.fillRoundRect(pill_x, pill_y, pill_w, pill_h, pill_r, 0xF7BE);
    sprite.drawRoundRect(pill_x, pill_y, pill_w, pill_h, pill_r, 0xD69A);

    char pill_str[36];
    if (g_pc_stats.weather_temp_c > -50.0f && g_pc_stats.weather_temp_c < 60.0f) {
      snprintf(pill_str, sizeof(pill_str), "CPU %.0f%%  GPU %.0fC  %.0fC",
               g_pc_stats.cpu_percent, g_pc_stats.gpu_temp_c, g_pc_stats.weather_temp_c);
    } else {
      snprintf(pill_str, sizeof(pill_str), "CPU %.0f%%  GPU %.0f%%  RAM %.0f%%",
               g_pc_stats.cpu_percent, g_pc_stats.gpu_percent, g_pc_stats.ram_percent);
    }

    sprite.setFont(&fonts::Font0);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setTextColor(0x0000, 0xF7BE);
    sprite.drawString(pill_str, 80, pill_y + pill_h / 2);
  }
}
