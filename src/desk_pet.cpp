#include "desk_pet.h"
#include "config.h"
#include "pc_stats.h"
#include <math.h>

// =====================================================================
//  desk_pet.cpp - High-Framerate Minimalist Procedural Desk Pet Engine
// =====================================================================
//  Pure Braun / Dieter Rams minimalist aesthetic:
//  - Crisp paper-white background (0xFFFF)
//  - Deep inky black eyes (0x0000)
//  - Anti-aliased subpixel Signed Distance Field (SDF) rasterization
//  - Saccadic glances, natural blinks, sinusoidal breathing physics
//  - 100 FPS frame pacing
//
//  v2: emotion renderers (anger / sadness / sickness / temperature),
//  cheek-blush tinting, sweat & tear particles, shiver + beat-bounce
//  physics. The emotional brain lives in pet_mood.cpp.
// =====================================================================

static PetMood  s_mood = PET_MOOD_NEUTRAL;
static uint32_t s_mood_until_ms = 0;

// Gaze physics
static float s_curr_look_x = 0.0f;
static float s_curr_look_y = 0.0f;
static float s_target_look_x = 0.0f;
static float s_target_look_y = 0.0f;
static uint32_t s_next_glance_ms = 0;

// Blink physics
static bool     s_is_blinking = false;
static uint32_t s_blink_start_ms = 0;
static uint32_t s_next_blink_ms = 0;
static const uint32_t BLINK_TOTAL_MS = 140;

// Reaction bounce physics
static uint32_t s_bounce_start_ms = 0;
static uint32_t s_pet_active_since_ms = 0;

// Transient reaction window (click bounce / startled wake): the mood
// engine must not stomp the face while this is running.
static uint32_t s_transient_until_ms = 0;

// v2 tint (cheek blush) + vibe bounce set by the mood engine
static uint8_t  s_tint_r = 255, s_tint_g = 120, s_tint_b = 140;
static uint8_t  s_tint_amt = 0;            // 0..255
static uint32_t s_last_tint_set_ms = 0;
static float    s_vibe_amp = 0.0f;         // bounce amplitude (px)
static float    s_vibe_hz  = 2.0f;         // bounce frequency (Hz)
static uint8_t  s_vibe_style = 0;          // 0=groove 1=cool nod 2=hype 3=rock 4=chill

void deskPetInit() {
  s_mood = PET_MOOD_NEUTRAL;
  s_mood_until_ms = 0;
  s_curr_look_x = 0.0f;
  s_curr_look_y = 0.0f;
  s_target_look_x = 0.0f;
  s_target_look_y = 0.0f;
  s_next_glance_ms = millis() + 2000;
  s_next_blink_ms  = millis() + 3500;
  s_is_blinking    = false;
  s_bounce_start_ms = 0;
  s_transient_until_ms = 0;
  s_tint_amt = 0;
  s_vibe_amp = 0.0f;
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

void deskPetSetTint(uint8_t r, uint8_t g, uint8_t b, uint8_t amount) {
  s_tint_r = r; s_tint_g = g; s_tint_b = b; s_tint_amt = amount;
  if (amount > 0) s_last_tint_set_ms = millis(); // engine still alive
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
    // Cheerful inverted arcs and happy bounce
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

static inline uint16_t grayToRgb565(uint8_t v) {
  uint16_t r5 = (v * 31) / 255;
  uint16_t g6 = (v * 63) / 255;
  uint16_t b5 = (v * 31) / 255;
  return (r5 << 11) | (g6 << 5) | b5;
}

// Proper alpha blend between the pixel already in the buffer and a
// source RGB565 color (alpha 0..255). Used for blush / sweat / tears.
static inline uint16_t blend565(uint16_t dst, uint16_t src, uint8_t alpha) {
  uint8_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
  uint8_t sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
  uint8_t r = (uint8_t)((dr * (255 - alpha) + sr * alpha) / 255);
  uint8_t g = (uint8_t)((dg * (255 - alpha) + sg * alpha) / 255);
  uint8_t b = (uint8_t)((db * (255 - alpha) + sb * alpha) / 255);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint16_t pack565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r * 31) / 255 << 11) | ((g * 63) / 255 << 5) | ((b * 31) / 255));
}

static void drawHappyEyeAA(LGFX_Sprite& sprite, int cx, int cy) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  float arc_cy = (float)cy + 13.0f;
  float r_target = 18.0f;
  float half_th = 2.4f;

  int x_min = cx - 22;
  int x_max = cx + 22;
  int y_min = cy - 12;
  int y_max = cy + 12;

  if (x_min < 0) x_min = 0;
  if (x_max > 159) x_max = 159;
  if (y_min < 0) y_min = 0;
  if (y_max > 127) y_max = 127;

  for (int y = y_min; y <= y_max; ++y) {
    if ((float)y > arc_cy - 4.0f) continue;
    uint16_t* row = &buf[y * 160];
    float py = (float)y;
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float dx = px - (float)cx;
      float dy = arc_cy - py;
      float dist = sqrtf(dx * dx + dy * dy);

      float angle_deg = atan2f(dy, dx) * 57.29578f;
      if (angle_deg < 32.0f || angle_deg > 148.0f) continue;

      float d_arc = fabsf(dist - r_target);
      if (d_arc <= half_th - 0.5f) {
        row[x] = 0x0000;
      } else if (d_arc < half_th + 0.5f) {
        uint8_t v = (uint8_t)((d_arc - (half_th - 0.5f)) * 255.0f);
        uint16_t col = grayToRgb565(v);
        if (col < row[x]) {
          row[x] = col;
        }
      }
    }
  }
}

// Mathematically perfect Anti-Aliased Signed Distance Field (SDF) Capsule
static void drawCapsuleEyeAA(LGFX_Sprite& sprite, int cx, int cy, int w, int h) {
  if (w <= 0 || h <= 0) return;
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  if (h >= w) {
    // Vertical capsule
    float r = (float)(w - 1) * 0.5f;
    float top_cy = (float)cy - (float)(h - w) * 0.5f;
    float bot_cy = (float)cy + (float)(h - w) * 0.5f;

    int x_min = (int)floorf((float)cx - r - 1.5f);
    int x_max = (int)ceilf((float)cx + r + 1.5f);
    int y_min = (int)floorf(top_cy - r - 1.5f);
    int y_max = (int)ceilf(bot_cy + r + 1.5f);

    if (x_min < 0) x_min = 0;
    if (x_max > 159) x_max = 159;
    if (y_min < 0) y_min = 0;
    if (y_max > 127) y_max = 127;

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dist;
        if (py < top_cy) {
          float dx = px - (float)cx;
          float dy = py - top_cy;
          dist = sqrtf(dx * dx + dy * dy);
        } else if (py > bot_cy) {
          float dx = px - (float)cx;
          float dy = py - bot_cy;
          dist = sqrtf(dx * dx + dy * dy);
        } else {
          dist = fabsf(px - (float)cx);
        }

        if (dist <= r - 0.5f) {
          row[x] = 0x0000;
        } else if (dist < r + 0.5f) {
          uint8_t v = (uint8_t)((dist - (r - 0.5f)) * 255.0f);
          uint16_t col = grayToRgb565(v);
          if (col < row[x]) {
            row[x] = col;
          }
        }
      }
    }
  } else {
    // Horizontal capsule / flattened pill
    float r = (float)(h - 1) * 0.5f;
    float left_cx = (float)cx - (float)(w - h) * 0.5f;
    float right_cx = (float)cx + (float)(w - h) * 0.5f;

    int x_min = (int)floorf(left_cx - r - 1.5f);
    int x_max = (int)ceilf(right_cx + r + 1.5f);
    int y_min = (int)floorf((float)cy - r - 1.5f);
    int y_max = (int)ceilf((float)cy + r + 1.5f);

    if (x_min < 0) x_min = 0;
    if (x_max > 159) x_max = 159;
    if (y_min < 0) y_min = 0;
    if (y_max > 127) y_max = 127;

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dist;
        if (px < left_cx) {
          float dx = px - left_cx;
          float dy = py - (float)cy;
          dist = sqrtf(dx * dx + dy * dy);
        } else if (px > right_cx) {
          float dx = px - right_cx;
          float dy = py - (float)cy;
          dist = sqrtf(dx * dx + dy * dy);
        } else {
          dist = fabsf(py - (float)cy);
        }

        if (dist <= r - 0.5f) {
          row[x] = 0x0000;
        } else if (dist < r + 0.5f) {
          uint8_t v = (uint8_t)((dist - (r - 0.5f)) * 255.0f);
          uint16_t col = grayToRgb565(v);
          if (col < row[x]) {
            row[x] = col;
          }
        }
      }
    }
  }
}

// Anti-aliased line segment (SDF of a capsule along a segment) - used
// for slanted eyebrows, frown arcs and wavy sick mouths.
static void drawSegAA(LGFX_Sprite& sprite, float x0, float y0,
                      float x1, float y1, float hw) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;
  float abx = x1 - x0, aby = y1 - y0;
  float ab2 = abx * abx + aby * aby;
  if (ab2 < 0.001f) return;

  int x_min = (int)floorf(fminf(x0, x1) - hw - 1.5f);
  int x_max = (int)ceilf (fmaxf(x0, x1) + hw + 1.5f);
  int y_min = (int)floorf(fminf(y0, y1) - hw - 1.5f);
  int y_max = (int)ceilf (fmaxf(y0, y1) + hw + 1.5f);
  if (x_min < 0) x_min = 0;
  if (x_max > 159) x_max = 159;
  if (y_min < 0) y_min = 0;
  if (y_max > 127) y_max = 127;

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float t = ((px - x0) * abx + (py - y0) * aby) / ab2;
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      float dx = px - (x0 + t * abx);
      float dy = py - (y0 + t * aby);
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist <= hw - 0.5f) {
        row[x] = 0x0000;
      } else if (dist < hw + 0.5f) {
        uint8_t v = (uint8_t)((dist - (hw - 0.5f)) * 255.0f);
        uint16_t col = grayToRgb565(v);
        if (col < row[x]) {
          row[x] = col;
        }
      }
    }
  }
}

// Soft alpha-blended filled disc: cheek blush, sweat & tear drops.
static void drawSoftDiscAA(LGFX_Sprite& sprite, float cx, float cy,
                           float r, uint16_t color, uint8_t alpha) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf || alpha == 0 || r < 0.5f) return;

  int x_min = (int)floorf(cx - r - 1.0f);
  int x_max = (int)ceilf (cx + r + 1.0f);
  int y_min = (int)floorf(cy - r - 1.0f);
  int y_max = (int)ceilf (cy + r + 1.0f);
  if (x_min < 0) x_min = 0;
  if (x_max > 159) x_max = 159;
  if (y_min < 0) y_min = 0;
  if (y_max > 127) y_max = 127;

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float dx = (float)x - cx;
      float dy = py - cy;
      float dist = sqrtf(dx * dx + dy * dy);
      if (dist <= r) {
        row[x] = blend565(row[x], color, alpha);
      } else if (dist < r + 1.0f) {
        uint8_t a = (uint8_t)(alpha * (1.0f - (dist - r)));
        row[x] = blend565(row[x], color, a);
      }
    }
  }
}

// Wavy squiggle mouth (sick / uncomfortable): tiny zigzag segments.
static void drawWavyMouth(LGFX_Sprite& sprite, float cx, float cy, float phase) {
  for (int i = 0; i < 4; ++i) {
    float xa = cx - 9.0f + i * 6.0f;
    float xb = xa + 6.0f;
    float ya = cy + ((i + (int)phase) % 2 == 0 ? -1.5f : 1.5f);
    float yb = cy + ((i + 1 + (int)phase) % 2 == 0 ? -1.5f : 1.5f);
    drawSegAA(sprite, xa, ya, xb, yb, 1.6f);
  }
}

// Frown mouth (sad): upper-half arc => corners droop.
static void drawFrownAA(LGFX_Sprite& sprite, float cx, float cy) {
  const int STEPS = 6;
  float px = cx - 9.0f, py = cy;
  for (int i = 1; i <= STEPS; ++i) {
    float t = (float)i / (float)STEPS;                 // 0..1 across
    float ang = 3.14159f * t;                          // pi..0
    float nx = cx - cosf(ang) * 9.0f;
    float ny = cy + 3.0f - sinf(ang) * 4.5f;           // arc apex above ends
    drawSegAA(sprite, px, py, nx, ny, 1.7f);
    px = nx; py = ny;
  }
}

// Falling tear particle: loops top->bottom with a slight sway.
static void drawTear(LGFX_Sprite& sprite, float ox, float top_y,
                     uint32_t ms, uint32_t phase_ms) {
  const float TRAVEL = 34.0f;
  const uint32_t PERIOD = 1300;
  uint32_t t = (ms + phase_ms) % PERIOD;
  float f = (float)t / (float)PERIOD;
  float y = top_y + f * TRAVEL;
  // fade in/out at the ends of the cycle
  uint8_t alpha = (uint8_t)(f < 0.1f ? f * 10.0f * 255.0f
                          : (f > 0.85f ? (1.0f - f) / 0.15f * 255.0f : 255.0f));
  float sway = sinf(f * 6.2831f * 2.0f) * 1.5f;
  drawSoftDiscAA(sprite, ox + sway, y, 2.2f, pack565(96, 176, 255), alpha);
}

void deskPetDrawFrame(LGFX_Sprite& sprite, uint32_t ms) {
  // Paper background lerped toward the mood tint (rage red, sick green,
  // cold blue...). The emotion color lives HERE - no cheek dots.
  uint8_t bg_amt = (uint8_t)(((uint16_t)s_tint_amt * 64) / 255); // max ~25%
  uint16_t bg = blend565(0xFFFF, pack565(s_tint_r, s_tint_g, s_tint_b), bg_amt);
  sprite.fillSprite(bg);

  if (s_pet_active_since_ms == 0) s_pet_active_since_ms = ms;

  // Expire temporary mood
  if (s_mood_until_ms > 0) {
    if (ms >= s_mood_until_ms) {
      s_mood_until_ms = 0;
    }
  } else {
    // Natural idle mood progression: neutral -> sleepy (45s) -> sleeping (60s)
    uint32_t idle_ms = ms - s_pet_active_since_ms;
    if (idle_ms >= 60000) {
      s_mood = PET_MOOD_SLEEPING;
    } else if (idle_ms >= 45000) {
      s_mood = PET_MOOD_SLEEPY;
    } else {
      s_mood = PET_MOOD_NEUTRAL;
    }
  }

  // Mood-driven environment: blush decays toward neutral if the engine
  // stops refreshing it (keeps a dead engine from leaving artifacts).
  if (s_tint_amt > 0 && (ms - s_last_tint_set_ms > 2500)) {
    s_tint_amt = (s_tint_amt > 4) ? (s_tint_amt - 4) : 0;
  }

  bool mood_is_vibe = (s_mood == PET_MOOD_DANCING || s_mood == PET_MOOD_EXCITED);
  if (!mood_is_vibe) s_vibe_amp *= 0.9f; // glide the bounce back out

  // 1. Saccadic Glance State Machine (suppressed while vibe-bouncing)
  if (!mood_is_vibe && ms >= s_next_glance_ms) {
    s_next_glance_ms = ms + 2400 + (esp_random() % 2800);
    uint32_t pick = esp_random() % 7;
    switch (pick) {
      case 0: s_target_look_x =   0.0f; s_target_look_y =   0.0f; break; // Center
      case 1: s_target_look_x = -13.0f; s_target_look_y =   0.0f; break; // Left
      case 2: s_target_look_x =  13.0f; s_target_look_y =   0.0f; break; // Right
      case 3: s_target_look_x =  -9.0f; s_target_look_y =  -8.0f; break; // Up-Left curious
      case 4: s_target_look_x =   9.0f; s_target_look_y =  -8.0f; break; // Up-Right curious
      case 5: s_target_look_x =   0.0f; s_target_look_y =  -9.0f; break; // Up attentive
      default:s_target_look_x =   0.0f; s_target_look_y =   6.0f; break; // Down modest
    }
  }
  if (s_mood == PET_MOOD_THINKING) {
    // Thoughtful fixed faraway stare, very slow glide
    s_target_look_x = -9.0f;
    s_target_look_y = -9.0f;
  }

  // Smooth exponential glide to glance target (100 FPS butter physics)
  float glide = (s_mood == PET_MOOD_THINKING) ? 0.03f : 0.12f;
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
      if (elapsed < 50) {
        // Fast snap close (0..50ms)
        blink_factor = 1.0f - ((float)elapsed / 50.0f);
      } else {
        // Smooth ease-out open (50..140ms)
        blink_factor = (float)(elapsed - 50) / 90.0f;
      }
    } else {
      s_is_blinking = false;
      blink_factor = 1.0f;
      // Schedule next natural blink (randomized 3.0 to 6.5s)
      s_next_blink_ms = ms + 3000 + (esp_random() % 3500);
      // 1-in-6 chance of an endearing quick double-blink
      if ((esp_random() % 6) == 0) {
        s_next_blink_ms = ms + 250;
      }
    }
  }

  // 3. Breathing physics - slow deep breaths while sleeping
  float breath_period = (s_mood == PET_MOOD_SLEEPING || s_mood == PET_MOOD_SLEEPY) ? 0.0012f : 0.0028f;
  float breath = sinf((float)ms * breath_period) * 1.5f;

  // 4. Click Reaction Bounce Physics
  float bounce_y = 0.0f;
  if (s_bounce_start_ms > 0) {
    uint32_t b_elapsed = ms - s_bounce_start_ms;
    if (b_elapsed < 350) {
      float t = (float)b_elapsed / 350.0f;
      bounce_y = -sinf(t * 3.14159f) * 6.0f; // 6px gentle bounce
    } else {
      s_bounce_start_ms = 0;
    }
  }

  // 4b. Vibe bounce (dancing / excited): shaped |sin| = hit on the beat.
  //     Style changes the MOVEMENT: drill nods cool and deep, rock
  //     headbangs relentlessly, chill just sways side to side.
  float vibe_y = 0.0f;
  float sway_x = 0.0f;
  if (mood_is_vibe && s_vibe_amp > 0.2f) {
    float ph = (float)ms * 0.001f * s_vibe_hz * 6.2831f;
    if (s_vibe_style == 1) {              // drill / trap: cool deep head-nod
      vibe_y = -fabsf(sinf(ph)) * s_vibe_amp;
      sway_x = sinf(ph * 0.35f) * 4.5f;   // slow lazy side drift
    } else if (s_vibe_style == 3) {       // rock / metal: headbang
      vibe_y = -fabsf(sinf(ph)) * s_vibe_amp;
      sway_x = sinf(ph) * 1.5f;           // tight aggressive shake
    } else if (s_vibe_style == 4) {       // lofi / chill: lazy sway
      sway_x = sinf(ph) * 5.0f;
      vibe_y = -fabsf(sinf(ph * 0.5f)) * s_vibe_amp;
    } else {                              // groove / hype: bounce on beat
      vibe_y = -fabsf(sinf(ph)) * s_vibe_amp;
      if (s_mood == PET_MOOD_DANCING) {
        sway_x = sinf(ph * 0.25f) * 3.0f; // slow side-to-side groove
      }
    }
  }

  // 4c. Shiver jitter (cold / frustrated / scared)
  float shiver_x = 0.0f;
  if (s_mood == PET_MOOD_COLD)       shiver_x = sinf((float)ms * 0.09f) * 1.4f;
  else if (s_mood == PET_MOOD_FRUSTRATED) shiver_x = sinf((float)ms * 0.05f) * 1.0f;
  else if (s_mood == PET_MOOD_SCARED)     shiver_x = sinf((float)ms * 0.07f) * 0.8f;

  // Eye geometry definition (160x128 screen)
  const int left_center_x  = 53 + (int)(s_curr_look_x + sway_x + shiver_x);
  const int right_center_x = 107 + (int)(s_curr_look_x + sway_x + shiver_x);
  const int center_y       = 64 + (int)(s_curr_look_y + bounce_y + vibe_y);

  uint16_t blush_col = pack565(s_tint_r, s_tint_g, s_tint_b);   // (atmosphere only)
  (void)blush_col;

  // Mood Rendering
  switch (s_mood) {
    case PET_MOOD_HAPPY: {
      drawHappyEyeAA(sprite, left_center_x, center_y);
      drawHappyEyeAA(sprite, right_center_x, center_y);
      break;
    }

    case PET_MOOD_WINK: {
      drawHappyEyeAA(sprite, left_center_x, center_y);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, 52 + (int)breath);
      break;
    }

    case PET_MOOD_SLEEPING: {
      // Peaceful closed horizontal pills
      drawCapsuleEyeAA(sprite, left_center_x, center_y + (int)breath, 34, 5);
      drawCapsuleEyeAA(sprite, right_center_x, center_y + (int)breath, 34, 5);
      break;
    }

    case PET_MOOD_SURPRISED: {
      // Wide circular alert eyes
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 40, 54);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 40, 54);
      break;
    }

    case PET_MOOD_SLEEPY: {
      // Drowsy heavy eyelids
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, 20 + (int)breath);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, 20 + (int)breath);
      break;
    }

    case PET_MOOD_ANGRY:
    case PET_MOOD_FRUSTRATED: {
      // Narrowed eyes + slanted brows slamming toward the center
      int eh = (int)(30.0f * blink_factor);
      if (eh < 6) eh = 6;
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, eh);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, eh);
      float brow_y = (s_mood == PET_MOOD_FRUSTRATED) ? 46.0f : 48.0f;
      drawSegAA(sprite, (float)(left_center_x - 16), 42.0f,
                       (float)(left_center_x + 13), brow_y, 2.0f);
      drawSegAA(sprite, (float)(right_center_x + 16), 42.0f,
                       (float)(right_center_x - 13), brow_y, 2.0f);
      // Gritted straight mouth; frustrated adds a tense zigzag
      if (s_mood == PET_MOOD_FRUSTRATED) {
        drawWavyMouth(sprite, 80.0f, 99.0f, 0);
      } else {
        drawSegAA(sprite, 72.0f, 99.0f, 88.0f, 101.0f, 1.8f);
      }
      break;
    }

    case PET_MOOD_SAD:
    case PET_MOOD_CRYING: {
      // Droopy lids, sad brows (inner ends raised) + frown
      int eh = (int)(26.0f * blink_factor);
      if (eh < 6) eh = 6;
      drawCapsuleEyeAA(sprite, left_center_x, center_y + 2, 34, eh);
      drawCapsuleEyeAA(sprite, right_center_x, center_y + 2, 34, eh);
      drawSegAA(sprite, (float)(left_center_x - 15), 47.0f,
                       (float)(left_center_x + 13), 43.0f, 1.8f);
      drawSegAA(sprite, (float)(right_center_x + 15), 47.0f,
                       (float)(right_center_x - 13), 43.0f, 1.8f);
      drawFrownAA(sprite, 80.0f, 99.0f);
      if (s_mood == PET_MOOD_CRYING) {
        drawTear(sprite, (float)(left_center_x - 14), 92.0f, ms, 0);
        drawTear(sprite, (float)(right_center_x + 14), 92.0f, ms, 650);
      }
      break;
    }

    case PET_MOOD_SICK:
    case PET_MOOD_FEVER: {
      // Queasy droopy lids + wavy mouth; fever adds forehead sweat
      int eh = (int)(22.0f * blink_factor);
      if (eh < 6) eh = 6;
      drawCapsuleEyeAA(sprite, left_center_x, center_y + 2, 34, eh);
      drawCapsuleEyeAA(sprite, right_center_x, center_y + 2, 34, eh);
      drawWavyMouth(sprite, 80.0f, 99.0f, (float)((ms / 900) % 2));
      if (s_mood == PET_MOOD_FEVER) {
        // Sweat drop sliding down the right temple
        float f = ((float)(ms % 2200)) / 2200.0f;
        drawSoftDiscAA(sprite, (float)(right_center_x + 18), 34.0f + f * 14.0f,
                       2.4f, pack565(96, 176, 255), 200);
      }
      break;
    }

    case PET_MOOD_COLD: {
      // Shivering squint + tight little mouth
      int eh = (int)(18.0f * blink_factor);
      if (eh < 6) eh = 6;
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, eh);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, eh);
      drawSegAA(sprite, 74.0f, 99.0f, 86.0f, 99.0f, 1.6f);
      break;
    }

    case PET_MOOD_HOT: {
      // Panting: little round mouth pulsing with breath + sweat drop
      int eh = (int)(36.0f * blink_factor);
      if (eh < 6) eh = 6;
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, eh);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, eh);
      float pr = 3.0f + (breath + 1.5f) * 1.2f;   // 3..6 px panting "o"
      drawSoftDiscAA(sprite, 80.0f, 98.0f, pr, pack565(60, 40, 40), 230);
      float f = ((float)(ms % 1800)) / 1800.0f;
      drawSoftDiscAA(sprite, (float)(left_center_x - 18), 36.0f + f * 12.0f,
                     2.4f, pack565(96, 176, 255), 200);
      break;
    }

    case PET_MOOD_EXCITED:
    case PET_MOOD_DANCING: {
      // Face follows the genre style: drill = half-lidded cool, rock =
      // angry brows headbang, everything else = happy bouncy arcs
      if (s_mood == PET_MOOD_DANCING && s_vibe_style == 1) {
        drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, 24 + (int)breath);
        drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, 24 + (int)breath);
      } else if (s_mood == PET_MOOD_DANCING && s_vibe_style == 3) {
        drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, 26);
        drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, 26);
        drawSegAA(sprite, (float)(left_center_x - 16), 42.0f,
                        (float)(left_center_x + 13), 48.0f, 2.0f);
        drawSegAA(sprite, (float)(right_center_x + 16), 42.0f,
                        (float)(right_center_x - 13), 48.0f, 2.0f);
      } else {
        drawHappyEyeAA(sprite, left_center_x, center_y);
        drawHappyEyeAA(sprite, right_center_x, center_y);
      }
      break;
    }

    case PET_MOOD_SCARED: {
      // Small trembling wide eyes + tiny "o" mouth
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 30, 42);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 30, 42);
      drawSoftDiscAA(sprite, 80.0f, 98.0f, 3.5f, pack565(60, 40, 40), 230);
      break;
    }

    case PET_MOOD_THINKING: {
      // Normal eyes with the slow faraway up-left glide
      int eh = (int)(48.0f * blink_factor);
      if (eh < 6) eh = 6;
      drawCapsuleEyeAA(sprite, left_center_x, center_y, 34, eh + (int)breath);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, 34, eh + (int)breath);
      break;
    }

    case PET_MOOD_NEUTRAL:
    default: {
      // Minimalist Dieter Rams vector capsule eyes
      int ew = 34;
      int base_h = 52 + (int)breath;
      int eh = (int)((float)base_h * blink_factor);
      if (eh < 5) eh = 5; // Clean 5px line during full blink

      drawCapsuleEyeAA(sprite, left_center_x, center_y, ew, eh);
      drawCapsuleEyeAA(sprite, right_center_x, center_y, ew, eh);
      break;
    }
  }

  // 5. Compact PC Hardware & Weather Pill (if PC stats stream is active)
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
