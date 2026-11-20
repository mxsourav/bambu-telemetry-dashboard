#include "desk_pet.h"
#include "config.h"
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
  s_pet_active_since_ms = millis();
}

static inline uint16_t grayToRgb565(uint8_t v) {
  uint16_t r5 = (v * 31) / 255;
  uint16_t g6 = (v * 63) / 255;
  uint16_t b5 = (v * 31) / 255;
  return (r5 << 11) | (g6 << 5) | b5;
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

void deskPetDrawFrame(LGFX_Sprite& sprite, uint32_t ms) {
  // Pure white paper background (Braun / Dieter Rams aesthetic)
  sprite.fillSprite(0xFFFF);

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

  // 1. Saccadic Glance State Machine
  if (ms >= s_next_glance_ms) {
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

  // Smooth exponential glide to glance target (100 FPS butter physics)
  s_curr_look_x += (s_target_look_x - s_curr_look_x) * 0.12f;
  s_curr_look_y += (s_target_look_y - s_curr_look_y) * 0.12f;

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

  // 3. Subtle Breathing Physics (sinusoidal pulse)
  float breath = sinf((float)ms * 0.0028f) * 1.5f;

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

  // Eye geometry definition (160x128 screen)
  const int left_center_x  = 53 + (int)s_curr_look_x;
  const int right_center_x = 107 + (int)s_curr_look_x;
  const int center_y       = 64 + (int)s_curr_look_y + (int)bounce_y;

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
}
