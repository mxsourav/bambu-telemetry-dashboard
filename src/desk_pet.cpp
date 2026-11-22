#include "desk_pet.h"
#include "config.h"
#include "pc_stats.h"
#include "settings_manager.h"
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
static uint32_t s_yawn_start_ms = 0;
static uint32_t s_last_idle_yawn_ms = 0;

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
static const uint32_t BLINK_TOTAL_MS = 230;

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
  if (mood == PET_MOOD_SLEEPING || mood == PET_MOOD_DEEP_SLEEP) {
    // Ensure idle timer reflects sleep state so background checks don't wake it
    uint32_t now = millis();
    if (now >= 60000 && (now - s_pet_active_since_ms < 60000)) {
      s_pet_active_since_ms = now - 60000;
    }
  } else {
    // Waking or active mood resets idle timer
    s_pet_active_since_ms = millis();
  }
}

PetMood deskPetGetMood() {
  return s_mood;
}

static uint8_t s_tint_r = 255;
static uint8_t s_tint_g = 255;
static uint8_t s_tint_b = 255;
static uint8_t s_tint_amount = 0;
static float   s_curr_tint_r = 255.0f;
static float   s_curr_tint_g = 255.0f;
static float   s_curr_tint_b = 255.0f;
static uint8_t s_bg_r = 255;
static uint8_t s_bg_g = 255;
static uint8_t s_bg_b = 255;

void deskPetSetTint(uint8_t r, uint8_t g, uint8_t b, uint8_t amount) {
  if (s_mood == PET_MOOD_ANGRY || s_mood == PET_MOOD_FRUSTRATED) {
    s_tint_r = r;
    s_tint_g = g;
    s_tint_b = b;
    s_tint_amount = amount;
  } else {
    s_tint_amount = 0;
  }
}

void deskPetSetBounce(float amp_px, float freq_hz) {
  s_vibe_amp = amp_px;
  s_vibe_hz  = (freq_hz > 0.5f && freq_hz < 8.0f) ? freq_hz : 2.0f;
}

float deskPetGetVibeHz() {
  return s_vibe_hz;
}

void deskPetSetVibeStyle(uint8_t style) {
  s_vibe_style = (style <= 5) ? style : 0;
}

bool deskPetIsTransient() {
  return millis() < s_transient_until_ms;
}

void deskPetReactClick() {
  if (s_mood == PET_MOOD_SLEEPING || s_mood == PET_MOOD_DEEP_SLEEP ||
      s_mood == PET_MOOD_SLEEPY || s_mood == PET_MOOD_YAWN) {
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

void deskPetYawn() {
  s_mood = PET_MOOD_YAWN;
  s_yawn_start_ms = millis();
  s_mood_until_ms = s_yawn_start_ms + 2800;
}

const char* deskPetMoodName(PetMood mood) {
  switch (mood) {
    case PET_MOOD_HAPPY:      return "happy";
    case PET_MOOD_WINK:       return "wink";
    case PET_MOOD_SLEEPY:     return "sleepy";
    case PET_MOOD_SLEEPING:   return "sleeping";
    case PET_MOOD_DEEP_SLEEP: return "deep_sleep";
    case PET_MOOD_YAWN:       return "yawn";
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
// LovyanGFX sprite buffer uses big-endian swap565_t for ST7735 SPI DMA transfer
static inline uint16_t grayToRgb565(uint8_t v) {
  uint16_t r5 = (v * 31) / 255;
  uint16_t g6 = (v * 63) / 255;
  uint16_t b5 = (v * 31) / 255;
  uint16_t c = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
  return (uint16_t)((c << 8) | (c >> 8));
}

// Convert 24-bit RGB to byte-swapped RGB565 for direct sprite buffer rasterization
static inline uint16_t makeRgb565(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t r5 = (r * 31) / 255;
  uint16_t g6 = (g * 63) / 255;
  uint16_t b5 = (b * 31) / 255;
  uint16_t c = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
  return (uint16_t)((c << 8) | (c >> 8));
}

// ---------------------------------------------------------------------
// Unified Living Vector Eye Anti-Aliased Rasterizer
// Inspired by Anki Vector & Pixar character design:
// - Solid rounded squircle geometry (ratio ~ 1:1.33)
// - Dual gleaming specular catchlights that shift organically with gaze
// - Parametric eyelid carving:
//     * top_lid_y & top_lid_slope: angular cuts for cool swagger, anger, sadness, sleep
//     * bot_lid_y & bot_lid_curve: upward organic curve for joyful/smiling squint
// - Subpixel Signed Distance Field (SDF) anti-aliasing against pure paper white (0xFFFF)
// ---------------------------------------------------------------------
static void drawLivingEyeAA(LGFX_Sprite& sprite, float cx, float cy,
                            float w, float h, float r_corner,
                            float top_lid_y = 999.0f, float top_lid_slope = 0.0f,
                            float bot_lid_y = -999.0f, float bot_lid_curve = 0.0f,
                            float look_x = 0.0f, float look_y = 0.0f,
                            bool glint = true) {
  if (w <= 0.0f || h <= 0.0f) return;
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  if (r_corner > w * 0.5f) r_corner = w * 0.5f;
  if (r_corner > h * 0.5f) r_corner = h * 0.5f;

  const float bx = (w * 0.5f) - r_corner;
  const float by = (h * 0.5f) - r_corner;

  // Specular catchlights shift slightly with gaze to create 3D cornea depth
  const float g1_x = cx - 5.5f + look_x * 0.35f;
  const float g1_y = cy - 6.5f + look_y * 0.35f;
  const float g1_r = 4.2f;

  const float g2_x = cx + 6.0f + look_x * 0.35f;
  const float g2_y = cy + 7.5f + look_y * 0.35f;
  const float g2_r = 2.2f;

  const int x_min = (int)fmaxf(0.0f, cx - (w * 0.5f) - 2.0f);
  const int x_max = (int)fminf(159.0f, cx + (w * 0.5f) + 2.0f);
  const int y_min = (int)fmaxf(0.0f, cy - (h * 0.5f) - 2.0f);
  const int y_max = (int)fminf(127.0f, cy + (h * 0.5f) + 2.0f);

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

      // Top eyelid cut
      float d_top = -999.0f;
      if (top_lid_y < 900.0f) {
        float brow_y = top_lid_y + top_lid_slope * dx;
        d_top = brow_y - dy; // positive when above eyelid (cut off)
      }

      // Bottom eyelid cut (parabolic smile curve)
      float d_bot = -999.0f;
      if (bot_lid_y > -900.0f) {
        float curve_y = bot_lid_y - (dx * dx) * bot_lid_curve;
        d_bot = dy - curve_y; // positive when below bottom eyelid (cut off)
      }

      float d_total = fmaxf(d_shape, fmaxf(d_top, d_bot));

      // Specular glint (catchlight) SDF
      float d_glint = 999.0f;
      if (glint && h > 14.0f) {
        float dg1 = sqrtf((px - g1_x) * (px - g1_x) + (py - g1_y) * (py - g1_y)) - g1_r;
        float dg2 = sqrtf((px - g2_x) * (px - g2_x) + (py - g2_y) * (py - g2_y)) - g2_r;
        d_glint = fminf(dg1, dg2);
      }

      if (d_total <= -0.5f) {
        if (d_glint <= -0.5f) {
          row[x] = 0xFFFF; // Pure glistening white catchlight
        } else if (d_glint < 0.5f) {
          // Anti-aliased white catchlight edge against black eye
          uint8_t v = (uint8_t)((0.5f - d_glint) * 255.0f);
          row[x] = grayToRgb565(v);
        } else {
          row[x] = 0x0000; // Solid inky black eye body
        }
      } else if (d_total < 0.5f) {
        // Anti-aliased eye boundary against dynamic background
        float alpha = 0.5f - d_total;
        uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
        uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
        uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
        uint16_t col = makeRgb565(cr, cg, cb);
        if (row[x] != 0x0000) row[x] = col;
      }
    }
  }
}

// ---------------------------------------------------------------------
// Stylized Closed Eyelid Seam Line (curved arc)
// Renders an elegant anime closed-eye crease when eyelids meet completely
// ---------------------------------------------------------------------
static void drawClosedEyeSeam(LGFX_Sprite& sprite, float cx, float cy, float w, float slope = 0.0f) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;
  const float half_w = w * 0.46f;
  const int x_min = (int)fmaxf(0.0f, cx - half_w - 2.0f);
  const int x_max = (int)fminf(159.0f, cx + half_w + 2.0f);
  const int y_min = (int)fmaxf(0.0f, cy - 8.0f);
  const int y_max = (int)fminf(127.0f, cy + 8.0f);

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float dx = px - cx;
      if (fabsf(dx) > half_w + 1.0f) continue;
      float norm_x = dx / half_w;
      if (fabsf(norm_x) > 1.0f) norm_x = (norm_x > 0.0f) ? 1.0f : -1.0f;
      // Gentle anime closed-eye arc: curves slightly downward in center
      float seam_y = cy + 1.2f + (1.0f - norm_x * norm_x) * 2.8f + slope * dx;
      float d = fabsf(py - seam_y) - 1.25f; // ~2.5px line thickness
      float d_cap = fabsf(dx) - half_w;
      float d_total = fmaxf(d, d_cap);
      if (d_total <= -0.5f) {
        row[x] = 0x0000;
      } else if (d_total < 0.5f) {
        uint8_t alpha = (uint8_t)((0.5f - d_total) * 255.0f);
        uint8_t r = (s_bg_r * (255 - alpha)) / 255;
        uint8_t g = (s_bg_g * (255 - alpha)) / 255;
        uint8_t b = (s_bg_b * (255 - alpha)) / 255;
        row[x] = makeRgb565(r, g, b);
      }
    }
  }
}

// ---------------------------------------------------------------------
// Heavy Blue Watering Eyes Effect (Emotional / Sad / Ballads / Crying)
// Modeled with Pixar-grade visual fidelity:
// - Dynamic liquid surface wave sloshing in the bottom half of the eyes
// - Vibrant electric cyan/blue water pool with deep azure gradient
// - Sparkling white tear glints glinting on the sloshing water surface
// - Heavy animated tear droplets swelling and trickling down the cheeks
// - Sympathetic puppy-dog emotional brow droop with subtle vulnerability quiver
// ---------------------------------------------------------------------
static void drawWateringEyes(LGFX_Sprite& sprite, float left_cx, float right_cx, float center_y,
                             float base_w, float base_h, float eye_r, uint32_t ms, bool heavy = true,
                             float blink_progress = 0.0f) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const float bx = (base_w * 0.5f) - eye_r;
  const float by = (base_h * 0.5f) - eye_r;

  // Emotional quiver physics (vulnerable trembling)
  float quiver_y = sinf((float)ms * 0.010f) * 0.6f;
  float quiver_x = cosf((float)ms * 0.012f) * 0.4f;
  float cy = center_y + quiver_y;

  const float wave_time = (float)ms * 0.003f;

  // Single organic teardrop trickle from left eye (matching reference art)
  float tear_cycle = fmodf((float)ms * 0.00065f, 1.0f);
  float drop_origin_x = left_cx - 10.5f + quiver_x;
  float drop_origin_y = cy + 18.0f;
  float drop_cy = drop_origin_y;
  float drop_r = 3.4f;
  if (tear_cycle >= 0.30f) {
    float drop_progress = (tear_cycle - 0.30f) / 0.70f;
    drop_cy = drop_origin_y + drop_progress * 24.0f;
    drop_r = 3.6f * (1.0f - drop_progress * 0.35f);
  } else {
    drop_r = 1.4f + (tear_cycle / 0.30f) * 2.2f;
  }

  const uint16_t col_water_crest = makeRgb565(65, 210, 255); // Vivid crystal cyan highlight
  const uint16_t col_water_body  = makeRgb565(0, 136, 245);  // Pure anime water blue (0% red, 53% green, 96% blue)
  const uint16_t col_drop_body   = makeRgb565(0, 160, 255);  // Pure electric blue droplet

  if (blink_progress >= 0.88f) {
    drawClosedEyeSeam(sprite, left_cx, cy, base_w, -0.15f);
    drawClosedEyeSeam(sprite, right_cx, cy, base_w, 0.15f);

    // Cheek teardrop trickle remains visible even during eye blinks
    if (heavy) {
      int tx_min = (int)fmaxf(0.0f, drop_origin_x - drop_r - 2.0f);
      int tx_max = (int)fminf(159.0f, drop_origin_x + drop_r + 2.0f);
      int ty_min = (int)fmaxf(0.0f, drop_cy - drop_r - 2.0f);
      int ty_max = (int)fminf(127.0f, drop_cy + drop_r + 2.0f);
      for (int y = ty_min; y <= ty_max; ++y) {
        float py = (float)y;
        uint16_t* row = &buf[y * 160];
        for (int x = tx_min; x <= tx_max; ++x) {
          float px = (float)x;
          float drop_dx = px - drop_origin_x;
          float drop_dy = (py - drop_cy) * 0.88f;
          float d_droplet = sqrtf(drop_dx * drop_dx + drop_dy * drop_dy) - drop_r;
          if (d_droplet <= 0.0f) {
            float d_dglint = sqrtf((px - (drop_origin_x - 0.9f)) * (px - (drop_origin_x - 0.9f)) +
                                   (py - (drop_cy - 0.9f)) * (py - (drop_cy - 0.9f))) - (drop_r * 0.35f);
            row[x] = (d_dglint <= 0.0f) ? 0xFFFF : col_drop_body;
          } else if (d_droplet < 0.6f) {
            float alpha = 1.0f - (d_droplet / 0.6f);
            uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
            uint8_t cg = (uint8_t)(160.0f * alpha + s_bg_g * (1.0f - alpha));
            uint8_t cb = (uint8_t)(255.0f * alpha + s_bg_b * (1.0f - alpha));
            row[x] = makeRgb565(cr, cg, cb);
          }
        }
      }
    }
    return;
  }

  for (int side = 0; side < 2; ++side) {
    float sign = (side == 0) ? 1.0f : -1.0f;
    float cx = (side == 0 ? left_cx : right_cx) + quiver_x;

    // Specular cornea glint in upper eye
    float g1_x = cx - 3.5f;
    float g1_y = cy - 5.5f;
    float g1_r = 4.2f;

    bool has_drop = (heavy && side == 0);

    const int x_min = (int)fmaxf(0.0f, cx - (base_w * 0.5f) - 6.0f);
    const int x_max = (int)fminf(159.0f, cx + (base_w * 0.5f) + 6.0f);
    const int y_min = (int)fmaxf(0.0f, cy - (base_h * 0.5f) - 2.0f);
    const int y_max = (int)fminf(127.0f, cy + (base_h * 0.5f) + (has_drop ? 28.0f : 2.0f));

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      float dy = py - cy;
      uint16_t* row = &buf[y * 160];

      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dx = px - cx;

        // Base squircle SDF
        float ox = fmaxf(0.0f, fabsf(dx) - bx);
        float oy = fmaxf(0.0f, fabsf(dy) - by);
        float d_outer = sqrtf(ox * ox + oy * oy) - eye_r;
        float d_inner = fminf(fmaxf(fabsf(dx) - bx, fabsf(dy) - by), 0.0f) - eye_r;
        float d_shape = (fabsf(dx) > bx || fabsf(dy) > by) ? d_outer : d_inner;

        // Emotional arched sad top eyelid droop (inner raised, outer low) with organic eyelid sweep during blink
        float brow_y = -7.5f - sign * 0.28f * dx - (dx * dx) * 0.012f;
        if (blink_progress > 0.0f) {
          float sweep_y = -(base_h * 0.5f) + blink_progress * (base_h * 1.05f);
          brow_y = fmaxf(brow_y, sweep_y);
        }
        float d_top = brow_y - dy;
        float d_eye = fmaxf(d_shape, d_top);

        // Tear droplet SDF (single drop from left eye)
        float d_droplet = 999.0f;
        if (has_drop) {
          float drop_dx = px - drop_origin_x;
          float drop_dy = (py - drop_cy) * 0.88f;
          d_droplet = sqrtf(drop_dx * drop_dx + drop_dy * drop_dy) - drop_r;
        }

        bool in_eye = (d_eye <= 0.0f);
        bool in_drop = (has_drop && d_droplet <= 0.0f);

        if (in_eye) {
          // Dynamic liquid wave meniscus (curved sloshing surface)
          float wave_h = sinf(wave_time + dx * 0.24f) * 1.5f + cosf(wave_time * 1.4f - dx * 0.16f) * 0.7f;
          float water_surface_y = 5.0f + wave_h;
          float d_water = water_surface_y - dy;

          if (d_water <= 0.0f) {
            // Inside the blue water pool!
            // Sparkle tear glints on water (two bright white reflections)
            float wg1_x = cx - 3.5f + sinf(wave_time) * 0.8f;
            float wg1_y = cy + 12.0f;
            float dwg1 = sqrtf((px - wg1_x) * (px - wg1_x) + (py - wg1_y) * (py - wg1_y)) - 2.2f;

            float wg2_x = cx + 6.0f;
            float wg2_y = cy + 14.5f;
            float dwg2 = sqrtf((px - wg2_x) * (px - wg2_x) + (py - wg2_y) * (py - wg2_y)) - 1.4f;

            if (dwg1 <= 0.0f || dwg2 <= 0.0f) {
              row[x] = 0xFFFF; // Bright glistening white reflection
            } else if (d_water > -1.8f) {
              row[x] = col_water_crest; // Crystalline cyan crest rim
            } else {
              // Solid pure water blue body: NO banding, NO green tint!
              row[x] = col_water_body;
            }
          } else {
            // Upper black cornea with large round white glint
            float dg1 = sqrtf((px - g1_x) * (px - g1_x) + (py - g1_y) * (py - g1_y)) - g1_r;
            if (dg1 <= -0.5f) {
              row[x] = 0xFFFF;
            } else if (dg1 < 0.5f) {
              uint8_t v = (uint8_t)((0.5f - dg1) * 255.0f);
              row[x] = grayToRgb565(v);
            } else {
              row[x] = 0x0000;
            }
          }
        } else if (d_eye < 1.0f) {
          // Anti-aliased outer eye boundary against dynamic background
          float alpha = 1.0f - d_eye;
          uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
          uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
          uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
          row[x] = makeRgb565(cr, cg, cb);
        } else if (in_drop) {
          // Tear droplet trickling on white cheek (pure crystal blue)
          float d_drop_glint = sqrtf((px - (drop_origin_x - 1.0f)) * (px - (drop_origin_x - 1.0f)) +
                                     (py - (drop_cy - 1.0f)) * (py - (drop_cy - 1.0f))) - 1.2f;
          if (d_drop_glint <= 0.0f) {
            row[x] = 0xFFFF; // White glint on droplet
          } else {
            row[x] = col_drop_body;
          }
        } else if (has_drop && d_droplet < 1.0f) {
          // Anti-aliased droplet edge against background
          float alpha = 1.0f - d_droplet;
          uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
          uint8_t cg = (uint8_t)(160.0f * alpha + s_bg_g * (1.0f - alpha));
          uint8_t cb = (uint8_t)(255.0f * alpha + s_bg_b * (1.0f - alpha));
          row[x] = makeRgb565(cr, cg, cb);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------
// Breakup Phase: Minimalist Booze Bottle & Drinking Sipping Animation
// Dieter Rams industrial aesthetic: crisp vector silhouette, warm amber liquor,
// cork stopper, tipping swig motion with drunken hiccup response.
// ---------------------------------------------------------------------
static void drawBreakupBottle(LGFX_Sprite& sprite, float mouth_x, float mouth_y, uint32_t ms) {
  float t_sec = (float)ms * 0.001f;
  float cycle = fmodf(t_sec, 6.0f); // 6-second drinking loop

  // Resting posture: tilted slightly beside right cheek, strictly inside screen bounds
  float bx = 136.0f;
  float by = 82.0f;
  bool is_sipping = false;

  if (cycle >= 3.5f && cycle < 5.3f) {
    // Tipping bottle up to mouth for a heartbroken sip
    is_sipping = true;
    float sip_phase = 0.0f;
    if (cycle < 4.1f) {
      sip_phase = (cycle - 3.5f) / 0.6f; // raising
    } else if (cycle < 4.7f) {
      sip_phase = 1.0f; // drinking at mouth
    } else {
      sip_phase = 1.0f - (cycle - 4.7f) / 0.6f; // lowering
    }
    float ease = 0.5f - 0.5f * cosf(sip_phase * 3.14159f);
    bx = 136.0f - ease * 46.0f; // moves towards mouth (around x=90)
    by = 82.0f - ease * 12.0f;  // raises up to lip level (y=70)
  }

  int x0 = (int)bx;
  int y0 = (int)by;

  const uint16_t col_glass_out   = 0x2104; // Smoked glass contour
  const uint16_t col_amber_booze = 0xFBE0; // Warm amber whiskey / rum
  const uint16_t col_amber_dark  = 0xB3E0; // Deep amber base
  const uint16_t col_cork        = 0x9A60; // Vintage cork
  const uint16_t col_label       = 0xFFFF; // Clean white label

  // Bottle Body (11w x 20h, r=2)
  sprite.fillRoundRect(x0 - 5, y0, 11, 20, 2, col_glass_out);
  // Amber liquid fill
  sprite.fillRect(x0 - 4, y0 + 5, 9, 13, col_amber_booze);
  sprite.fillRect(x0 - 4, y0 + 15, 9, 3, col_amber_dark);
  // Modern geometric white label with black center divider
  sprite.fillRect(x0 - 4, y0 + 8, 9, 5, col_label);
  sprite.drawFastHLine(x0 - 3, y0 + 10, 7, 0x0000);

  // Bottle Neck (5w x 7h)
  sprite.fillRect(x0 - 2, y0 - 7, 5, 7, col_glass_out);
  // Cork
  sprite.fillRect(x0 - 2, y0 - 9, 5, 3, col_cork);

  // Little drunken tear/amber droplet during the gulp
  if (is_sipping && cycle >= 4.2f && cycle < 4.7f) {
    sprite.fillCircle(x0 - 4, y0 - 8, 1, col_amber_booze);
  }
}

// ---------------------------------------------------------------------
// Bold Curved Peaceful Sleeping Eyes (︶ ︶) - Normal & Deep Sleep
// Cute Pixar/Anime aesthetic: curved downward arc with lifted ends and rounded caps
// ---------------------------------------------------------------------
static void drawSleepingEyesAA(LGFX_Sprite& sprite, float cx, float cy, float breath) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const float hw = 15.0f;
  const float th = 4.6f;
  const float half_th = th * 0.5f;
  const float eff_cy = cy + breath;

  const int x_min = (int)fmaxf(0.0f, cx - hw - half_th - 1.5f);
  const int x_max = (int)fminf(159.0f, cx + hw + half_th + 1.5f);
  const int y_min = (int)fmaxf(0.0f, eff_cy - 9.0f);
  const int y_max = (int)fminf(127.0f, eff_cy + 8.0f);

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float dx = px - cx;

      // Clamp dx to end-caps for rounded caps
      float cx_clamped = fmaxf(-hw, fminf(hw, dx));
      // Organic peaceful curve: dips down in center, curling gently UP at outer and inner ends (︶ ︶)
      // Screen y increases downwards: larger y = dip down (center), smaller y = curl up (ends)
      float ey = eff_cy + 2.8f - (cx_clamped * cx_clamped) * 0.032f;
      float ex = cx + cx_clamped;

      float d = sqrtf((px - ex) * (px - ex) + (py - ey) * (py - ey)) - half_th;

      if (d <= -0.5f) {
        row[x] = 0x0000;
      } else if (d < 0.5f) {
        float alpha = 0.5f - d;
        uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
        uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
        uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
        uint16_t col = makeRgb565(cr, cg, cb);
        if (row[x] != 0x0000) row[x] = col;
      }
    }
  }
}

// ---------------------------------------------------------------------
// Anime Sleep Bubble (Deep Sleep Mode)
// Organic translucent liquid bubble inflating on inhale, wobbling at peak tension,
// and deflating smoothly on exhale, complete with white specular catchlight highlight.
// ---------------------------------------------------------------------
static void drawSleepBubble(LGFX_Sprite& sprite, float anchor_x, float anchor_y, uint32_t ms) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const uint32_t cycle = 3400; // 3.4s deep breathing period
  uint32_t t_ms = ms % cycle;
  float t = (float)t_ms / (float)cycle;

  float r = 2.0f;
  float cx = anchor_x;
  float cy = anchor_y;

  if (t < 0.55f) {
    // Phase 1: Inhale / Bubble Inflation (0 -> 1870ms)
    float p = t / 0.55f;
    float k = sinf(p * 1.5707963f); // ease-out
    r = 2.0f + k * 8.5f; // expands 2px -> 10.5px
    cx = anchor_x + k * 7.0f;
    cy = anchor_y - k * 5.0f;
  } else if (t < 0.80f) {
    // Phase 2: Peak surface-tension wobble (1870ms -> 2720ms)
    float wobble = sinf((float)(ms - 1870) * 0.022f) * 0.65f;
    r = 10.5f + wobble;
    cx = anchor_x + 7.0f + cosf((float)(ms - 1870) * 0.018f) * 0.35f;
    cy = anchor_y - 5.0f;
  } else {
    // Phase 3: Exhale / Bubble deflation (2720ms -> 3400ms)
    float p = (t - 0.80f) / 0.20f;
    float k = cosf(p * 1.5707963f);
    r = 2.0f + k * 8.5f;
    cx = anchor_x + k * 7.0f;
    cy = anchor_y - k * 5.0f;
  }

  if (r < 1.5f) return;

  const int x_min = (int)fmaxf(0.0f, cx - r - 2.5f);
  const int x_max = (int)fminf(159.0f, cx + r + 2.5f);
  const int y_min = (int)fmaxf(0.0f, cy - r - 2.5f);
  const int y_max = (int)fminf(127.0f, cy + r + 2.5f);

  // Specular catchlight on top-left of bubble
  float gx = cx - r * 0.38f;
  float gy = cy - r * 0.38f;
  float gr = fmaxf(1.0f, r * 0.26f);

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy)) - r;

      if (d > 0.5f) continue; // outside bubble

      float dg = sqrtf((px - gx) * (px - gx) + (py - gy) * (py - gy)) - gr;
      if (dg <= -0.5f) {
        row[x] = 0xFFFF; // Pure glistening white catchlight
      } else if (dg < 0.5f) {
        row[x] = 0xFFFF;
      } else if (d <= -1.4f) {
        // Translucent liquid sky-blue sheen
        row[x] = makeRgb565(212, 240, 255);
      } else if (d <= -0.5f) {
        // Subtle clean slate inner rim
        row[x] = makeRgb565(60, 95, 140);
      } else {
        // Anti-aliased outer rim against white
        float alpha = 0.5f - d;
        uint8_t v = (uint8_t)(255.0f * (1.0f - alpha));
        uint16_t col = grayToRgb565(v);
        if (row[x] != 0x0000) row[x] = col;
      }
    }
  }
}

// ---------------------------------------------------------------------
// Procedural Floating "Zzz" Vector Engine (Sleep Mode)
// Wide diagonal trajectory into top-right corner, staggered phases so they
// never stack into a vertical column or collide with the eyes.
// ---------------------------------------------------------------------
static void drawVectorZ(LGFX_Sprite& sprite, float cx, float cy, float w, float h, float th, float alpha) {
  if (alpha <= 0.02f) return;
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const float hw = w * 0.5f;
  const float hh = h * 0.5f;
  const float half_th = th * 0.5f;

  const int x_min = (int)fmaxf(0.0f, cx - hw - th - 1.0f);
  const int x_max = (int)fminf(159.0f, cx + hw + th + 1.0f);
  const int y_min = (int)fmaxf(0.0f, cy - hh - th - 1.0f);
  const int y_max = (int)fminf(127.0f, cy + hh + th + 1.0f);

  auto distToSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
    float dx = bx - ax;
    float dy = by - ay;
    float l2 = dx * dx + dy * dy;
    if (l2 < 0.001f) {
      float qx = px - ax, qy = py - ay;
      return sqrtf(qx * qx + qy * qy);
    }
    float t = ((px - ax) * dx + (py - ay) * dy) / l2;
    t = fmaxf(0.0f, fminf(1.0f, t));
    float qx = px - (ax + t * dx);
    float qy = py - (ay + t * dy);
    return sqrtf(qx * qx + qy * qy);
  };

  const float p1x = cx - hw, p1y = cy - hh;
  const float p2x = cx + hw, p2y = cy - hh;
  const float p3x = cx - hw, p3y = cy + hh;
  const float p4x = cx + hw, p4y = cy + hh;

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float d1 = distToSeg(px, py, p1x, p1y, p2x, p2y);
      float d2 = distToSeg(px, py, p2x, p2y, p3x, p3y);
      float d3 = distToSeg(px, py, p3x, p3y, p4x, p4y);
      float d = fminf(fminf(d1, d2), d3) - half_th;

      if (d <= -0.5f) {
        uint8_t v = (uint8_t)(255.0f * (1.0f - alpha));
        uint16_t col = grayToRgb565(v);
        if (row[x] != 0x0000) row[x] = col;
      } else if (d < 0.5f) {
        float a = (0.5f - d) * alpha;
        uint8_t v = (uint8_t)(255.0f * (1.0f - a));
        uint16_t col = grayToRgb565(v);
        if (row[x] != 0x0000) row[x] = col;
      }
    }
  }
}

static void drawFloatingZzz(LGFX_Sprite& sprite, uint32_t ms) {
  const uint32_t cycle = 3600;
  for (int i = 0; i < 3; ++i) {
    uint32_t offset = i * 1200; // 1.2s spacing = always perfectly separated
    uint32_t t_ms = (ms + offset) % cycle;
    float prog = (float)t_ms / (float)cycle; // 0.0 -> 1.0

    float alpha = 1.0f;
    if (prog < 0.15f) alpha = prog / 0.15f;
    else if (prog > 0.72f) alpha = (1.0f - prog) / 0.28f;

    // Graceful diagonal flight path: from (118, 52) to (152, 8)
    float cx = 118.0f + prog * 34.0f + sinf(prog * 6.283185f) * 3.2f;
    float cy = 52.0f - prog * 44.0f;

    float size_w = 5.5f + prog * 5.5f; // 5.5px -> 11.0px
    float size_h = size_w * 1.15f;
    float th = 1.8f + prog * 0.5f;     // 1.8px -> 2.3px

    drawVectorZ(sprite, cx, cy, size_w, size_h, th, alpha);
  }
}

// ---------------------------------------------------------------------
// Manga Animated Forehead Anger Cross Vein
// Four curved outward-flaring arcs forming a 4-pointed pinched star of negative space.
// Pulses organically with a cardiac throb rhythm.
// ---------------------------------------------------------------------
static void drawAngerVein(LGFX_Sprite& sprite, float cx, float cy, float base_scale,
                          uint8_t bg_r, uint8_t bg_g, uint8_t bg_b, uint32_t ms) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  // Heartbeat pulse cycle: ~1.1 Hz throb rate (one organic pulse every 900ms)
  float phase = fmodf((float)ms * 0.007f, 6.283185f);
  float pulse_k = fmaxf(0.0f, sinf(phase));
  float pulse = 0.88f + powf(pulse_k, 2.0f) * 0.32f;
  float s = base_scale * pulse;

  // High-contrast vein pop:
  // When face is dark fiery red, draw blazing white-hot vein (0xFFFF);
  // When face is white or light pink, draw vivid anime crimson (makeRgb565(230, 20, 35)).
  uint16_t vein_col = (bg_g < 90) ? 0xFFFF : makeRgb565(230, 20, 35);

  auto drawThickSeg = [&](float ax, float ay, float bx, float by, float th) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    if (l2 < 0.001f) return;
    float half_th = th * 0.5f;

    int x0 = (int)fmaxf(0.0f, fminf(ax, bx) - half_th - 1.0f);
    int x1 = (int)fminf(159.0f, fmaxf(ax, bx) + half_th + 1.0f);
    int y0 = (int)fmaxf(0.0f, fminf(ay, by) - half_th - 1.0f);
    int y1 = (int)fminf(127.0f, fmaxf(ay, by) + half_th + 1.0f);

    for (int y = y0; y <= y1; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x0; x <= x1; ++x) {
        float px = (float)x;
        float t = fmaxf(0.0f, fminf(1.0f, ((px - ax) * dx + (py - ay) * dy) / l2));
        float qx = ax + t * dx, qy = ay + t * dy;
        float dist = sqrtf((px - qx) * (px - qx) + (py - qy) * (py - qy)) - half_th;
        if (dist <= 0.0f) {
          row[x] = vein_col;
        }
      }
    }
  };

  const float th = 2.4f * s;
  float p_top[5][2] = { {-11.0f*s, -15.0f*s}, {-5.0f*s, -11.0f*s}, {0.0f, -9.0f*s}, {5.0f*s, -11.0f*s}, {11.0f*s, -15.0f*s} };
  float p_bot[5][2] = { {-11.0f*s,  15.0f*s}, {-5.0f*s,  11.0f*s}, {0.0f,  9.0f*s}, {5.0f*s,  11.0f*s}, {11.0f*s,  15.0f*s} };
  float p_lef[5][2] = { {-15.0f*s, -11.0f*s}, {-11.0f*s, -5.0f*s}, {-9.0f*s, 0.0f}, {-11.0f*s, 5.0f*s}, {-15.0f*s, 11.0f*s} };
  float p_rig[5][2] = { { 15.0f*s, -11.0f*s}, { 11.0f*s, -5.0f*s}, { 9.0f*s, 0.0f}, { 11.0f*s, 5.0f*s}, { 15.0f*s, 11.0f*s} };

  for (int i = 0; i < 4; ++i) {
    drawThickSeg(cx + p_top[i][0], cy + p_top[i][1], cx + p_top[i+1][0], cy + p_top[i+1][1], th);
    drawThickSeg(cx + p_bot[i][0], cy + p_bot[i][1], cx + p_bot[i+1][0], cy + p_bot[i+1][1], th);
    drawThickSeg(cx + p_lef[i][0], cy + p_lef[i][1], cx + p_lef[i+1][0], cy + p_lef[i+1][1], th);
    drawThickSeg(cx + p_rig[i][0], cy + p_rig[i][1], cx + p_rig[i+1][0], cy + p_rig[i+1][1], th);
  }
}

// ---------------------------------------------------------------------
// Responsive Procedural Eyebrows
// Modeled with organic elasticity and subpixel anti-aliasing:
// - Height and slope dynamically react to emotion and music genre
// - Curved arch geometry (not straight sticks!) with rounded caps
// - Saccadic gaze integration: brows track with eye glances
// ---------------------------------------------------------------------
static void drawProceduralBrows(LGFX_Sprite& sprite, float left_cx, float right_cx,
                                float center_y, PetMood mood, uint8_t vibe_style,
                                uint32_t ms, float blink_progress = 0.0f) {
  // Pure sleeping face: no eyebrow clutter above curved sleeping crescents
  if (mood == PET_MOOD_SLEEPING || mood == PET_MOOD_DEEP_SLEEP) return;

  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const float brow_base_cy = center_y - 33.0f; // ~29px in standard frame
  const float brow_w = 24.0f;
  const float brow_th = 3.6f; // bold, expressive anime stroke
  const float half_cap = brow_w * 0.5f - brow_th * 0.5f;

  // Music beat phase calculation for dynamic rhythm animation (half-time downbeat groove)
  float beat_sin = 0.0f;
  float nod = 0.0f;
  if (mood == PET_MOOD_DANCING) {
    float eff_hz = s_vibe_hz;
    if (eff_hz > 1.35f) eff_hz *= 0.5f; // half-time downbeat groove (beats 1 & 3)
    if (eff_hz < 0.85f) eff_hz = 0.85f;
    if (eff_hz > 1.35f) eff_hz = 1.35f;
    float beat_dur = 1000.0f / eff_hz;
    float ph = fmodf((float)ms, beat_dur) / beat_dur * 6.283185f;
    beat_sin = sinf(ph);
    nod = powf(0.5f - 0.5f * cosf(ph), 1.35f);
  }

  for (int side = 0; side < 2; ++side) {
    float cx = (side == 0) ? left_cx : right_cx;
    float slope = 0.0f; // slope along inner_dx (positive = inner corner raised)
    float y_offset = 0.0f;
    float arch_k = 0.024f; // arch curvature

    switch (mood) {
      case PET_MOOD_HAPPY:
      case PET_MOOD_EXCITED:
        y_offset = -3.2f; // lifted cheerfully
        slope = 0.04f;
        arch_k = 0.030f;
        break;

      case PET_MOOD_DANCING:
        switch (vibe_style) {
          case 1: // Hip-Hop / Drill / Trap: cocked swagger brow with 808 bass pump
            if (side == 0) {
              y_offset = -4.5f - nod * 4.8f; // swagger arch pumps with 808 kick
              slope = 0.22f;
              arch_k = 0.030f;
            } else {
              y_offset = 1.0f + nod * 2.4f;  // furrowed attitude
              slope = -0.28f;
              arch_k = 0.015f;
            }
            break;
          case 2: // EDM / House: high bouncy alert hype
            y_offset = -4.5f - fmaxf(0.0f, beat_sin) * 3.5f;
            slope = 0.0f;
            arch_k = 0.028f;
            break;
          case 3: // Rock / Metal: aggressive determined furrow headbang
            y_offset = 1.5f + nod * 2.0f;
            slope = -0.42f;
            arch_k = 0.012f;
            break;
          case 4: // Lo-Fi / Chill: calm relaxed brow with gentle sway
            y_offset = -1.8f;
            slope = 0.04f;
            arch_k = 0.024f;
            break;
          case 5: // Emotional / Heartbreak Ballad: deep weeping sorrow arch, vulnerable tremble
            y_offset = 2.4f + sinf((float)ms * 0.012f) * 0.6f;
            slope = 0.56f; // dramatic heartbroken slant (inner corners lifted high in grief, outer ends drooped low)
            arch_k = 0.018f;
            break;
          default: // Pop / Upbeat Groove: rhythmic bounce
            y_offset = -3.0f - fmaxf(0.0f, beat_sin) * 2.8f;
            slope = 0.02f;
            arch_k = 0.026f;
            break;
        }
        break;

      case PET_MOOD_SAD:
      case PET_MOOD_CRYING:
        y_offset = 2.4f + sinf((float)ms * 0.012f) * 0.6f;
        slope = 0.56f; // inner corner lifted high, outer drooped low (authentic crying sadness)
        arch_k = 0.018f;
        break;

      case PET_MOOD_ANGRY:
        y_offset = 2.2f;
        slope = -0.48f; // fierce furrow (inner down, outer up)
        arch_k = 0.012f;
        break;

      case PET_MOOD_FRUSTRATED: {
        // Enraged intense furrow with subtle slow tension heave
        float rumble = sinf((float)ms * 0.012f) * 0.5f;
        y_offset = 3.2f + rumble;
        slope = -0.56f; // extreme furious furrow
        arch_k = 0.008f;
        break;
      }

      case PET_MOOD_SURPRISED:
      case PET_MOOD_SCARED:
        y_offset = -6.0f; // high arched shock
        slope = 0.0f;
        arch_k = 0.035f;
        break;

      case PET_MOOD_SLEEPY:
        y_offset = 1.5f;
        slope = 0.08f;
        arch_k = 0.020f;
        break;

      case PET_MOOD_SLEEPING:
      case PET_MOOD_DEEP_SLEEP:
        y_offset = 2.8f;  // peaceful relaxed outer droop
        slope = 0.10f;
        arch_k = 0.018f;
        break;

      case PET_MOOD_YAWN:
        y_offset = 3.5f;  // soft drowsy droop
        slope = -0.18f; // outward droop in sleepy exhaustion
        arch_k = 0.022f;
        break;

      default: // Neutral
        y_offset = 0.0f;
        slope = 0.0f;
        arch_k = 0.022f;
        break;
    }

    // Eyelids naturally pull eyebrows down slightly during biological blinks
    float cy = brow_base_cy + y_offset + blink_progress * 2.5f;
    // Strict boundary clamps: ensure eyebrows never clip the top or bottom of the screen
    if (cy < 14.0f) cy = 14.0f;
    if (cy > 46.0f) cy = 46.0f;
    const int x_min = (int)fmaxf(0.0f, cx - brow_w * 0.5f - 2.0f);
    const int x_max = (int)fminf(159.0f, cx + brow_w * 0.5f + 2.0f);
    const int y_min = (int)fmaxf(0.0f, cy - 8.0f);
    const int y_max = (int)fminf(127.0f, cy + 8.0f);

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dx = px - cx;
        float inner_dx = (side == 0) ? dx : -dx;
        // Curved arched eyebrow: natural dome curvature
        float ey = cy - inner_dx * slope - (dx * dx) * arch_k;
        float dy = py - ey;

        float ox = fmaxf(0.0f, fabsf(dx) - half_cap);
        float d = sqrtf(ox * ox + dy * dy) - (brow_th * 0.5f);

        if (d <= -0.5f) {
          row[x] = 0x0000;
        } else if (d < 0.5f) {
          float alpha = 0.5f - d;
          uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
          uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
          uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
          uint16_t col = makeRgb565(cr, cg, cb);
          if (row[x] != 0x0000) row[x] = col;
        }
      }
    }
  }
}

// ---------------------------------------------------------------------
// Sleek Minimal Lip / Musical Mouth
// Reacts dynamically to speech, emotions, and music rhythm:
// - Normal Sleep: sweet upturned peaceful resting smile (‿)
// - Deep Sleep: parted relaxed sleeping mouth aperture
// - Music Vibing: pulses open singing aperture with beat rhythm
// - Hip-Hop / Drill: cocky smirk pumping with 808 bass kick
// ---------------------------------------------------------------------
static void drawMinimalLip(LGFX_Sprite& sprite, float center_x, float center_y,
                           PetMood mood, uint8_t vibe_style, uint32_t ms) {
  uint16_t* buf = (uint16_t*)sprite.getBuffer();
  if (!buf) return;

  const float mouth_cy = center_y + 32.0f; // ~94px in standard frame

  if (mood == PET_MOOD_YAWN) {
    float yawn_prog = (s_yawn_start_ms > 0 && ms >= s_yawn_start_ms) ?
                      fminf(1.0f, (float)(ms - s_yawn_start_ms) / 2800.0f) : 0.5f;
    float k = 0.0f;
    if (yawn_prog < 0.35f) {
      k = sinf((yawn_prog / 0.35f) * 1.57079f);
    } else if (yawn_prog < 0.65f) {
      float quiver = sinf((float)ms * 0.04f) * 0.05f;
      k = 1.0f + quiver;
    } else {
      k = cosf(((yawn_prog - 0.65f) / 0.35f) * 1.57079f);
    }

    float rx = 2.8f + k * 4.2f;
    float ry = 1.8f + k * 7.6f;
    const float mouth_cy_yawn = mouth_cy + k * 1.5f;

    const int x_min = (int)fmaxf(0.0f, center_x - rx - 2.5f);
    const int x_max = (int)fminf(159.0f, center_x + rx + 2.5f);
    const int y_min = (int)fmaxf(0.0f, mouth_cy_yawn - ry - 2.5f);
    const int y_max = (int)fminf(127.0f, mouth_cy_yawn + ry + 2.5f);

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dx = (px - center_x) / rx;
        float dy = (py - mouth_cy_yawn) / ry;
        float norm_d = sqrtf(dx * dx + dy * dy) - 1.0f;
        float d = norm_d * ((rx + ry) * 0.5f);

        if (d <= -0.5f) {
          row[x] = 0x0000;
        } else if (d < 0.5f) {
          float alpha = 0.5f - d;
          uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
          uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
          uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
          uint16_t col = makeRgb565(cr, cg, cb);
          if (row[x] != 0x0000) row[x] = col;
        }
      }
    }
    return;
  }

  // Deep Sleep: parted relaxed sleeping mouth cavity (small oval)
  if (mood == PET_MOOD_DEEP_SLEEP) {
    const float rx = 3.6f;
    const float ry = 2.4f;
    const float my = mouth_cy + 1.0f;
    const int x_min = (int)fmaxf(0.0f, center_x - rx - 2.0f);
    const int x_max = (int)fminf(159.0f, center_x + rx + 2.0f);
    const int y_min = (int)fmaxf(0.0f, my - ry - 2.0f);
    const int y_max = (int)fminf(127.0f, my + ry + 2.0f);

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dx = (px - center_x) / rx;
        float dy = (py - my) / ry;
        float norm_d = sqrtf(dx * dx + dy * dy) - 1.0f;
        float d = norm_d * ((rx + ry) * 0.5f);

        if (d <= -0.5f) {
          row[x] = 0x0000;
        } else if (d < 0.5f) {
          float alpha = 0.5f - d;
          uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
          uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
          uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
          uint16_t col = makeRgb565(cr, cg, cb);
          if (row[x] != 0x0000) row[x] = col;
        }
      }
    }
    return;
  }

  // Clenched gritted teeth grimace for FRUSTRATED / Enraged mood
  if (mood == PET_MOOD_FRUSTRATED) {
    const float bw = 24.0f;
    const float bh = 11.0f;
    const float br = 3.5f;
    const float bx = (bw * 0.5f) - br;
    const float by = (bh * 0.5f) - br;

    const int x_min = (int)fmaxf(0.0f, center_x - (bw * 0.5f) - 2.0f);
    const int x_max = (int)fminf(159.0f, center_x + (bw * 0.5f) + 2.0f);
    const int y_min = (int)fmaxf(0.0f, mouth_cy - (bh * 0.5f) - 2.0f);
    const int y_max = (int)fminf(127.0f, mouth_cy + (bh * 0.5f) + 2.0f);

    for (int y = y_min; y <= y_max; ++y) {
      float py = (float)y;
      uint16_t* row = &buf[y * 160];
      for (int x = x_min; x <= x_max; ++x) {
        float px = (float)x;
        float dx = px - center_x;
        float dy = py - mouth_cy;

        float ox = fmaxf(0.0f, fabsf(dx) - bx);
        float oy = fmaxf(0.0f, fabsf(dy) - by);
        float d = sqrtf(ox * ox + oy * oy) - br;

        if (d <= -0.5f) {
          if (d >= -1.4f) {
            row[x] = 0x0000; // Outer black lip rim
          } else {
            // Horizontal bite seam line at dy = 0
            bool is_bite_seam = (fabsf(dy) <= 0.65f);
            // Vertical tooth divider lines at dx = -6.0, 0.0, +6.0
            bool is_vert_divider = (fabsf(dx - (-6.0f)) <= 0.55f ||
                                    fabsf(dx) <= 0.55f ||
                                    fabsf(dx - 6.0f) <= 0.55f);
            if (is_bite_seam || is_vert_divider) {
              row[x] = 0x0000; // Black tooth division line
            } else {
              row[x] = 0xFFFF; // Pure gleaming white enamel tooth
            }
          }
        } else if (d < 0.5f) {
          float alpha = 0.5f - d;
          uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
          uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
          uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
          row[x] = makeRgb565(cr, cg, cb);
        }
      }
    }
    return;
  }

  // Dynamic animated rapping/singing mouth for active Hip-Hop / Drill / Trap
  if (mood == PET_MOOD_DANCING && vibe_style == 1) {
    float eff_hz = s_vibe_hz;
    if (eff_hz > 1.35f) eff_hz *= 0.5f;
    if (eff_hz < 0.85f) eff_hz = 0.85f;
    if (eff_hz > 1.35f) eff_hz = 1.35f;
    float beat_dur = 1000.0f / eff_hz;
    float ph = fmodf((float)ms, beat_dur) / beat_dur * 6.283185f;
    float nod = powf(0.5f - 0.5f * cosf(ph), 1.35f);

    if (nod > 0.25f) {
      float rap_k = (nod - 0.25f) / 0.75f;
      float rx = 3.8f + rap_k * 3.4f;
      float ry = 1.6f + rap_k * 2.6f;
      const float my = mouth_cy + nod * 1.5f;
      float tilt = 0.20f;

      const int x_min = (int)fmaxf(0.0f, center_x - rx - 2.0f);
      const int x_max = (int)fminf(159.0f, center_x + rx + 2.0f);
      const int y_min = (int)fmaxf(0.0f, my - ry - 2.5f);
      const int y_max = (int)fminf(127.0f, my + ry + 2.5f);

      for (int y = y_min; y <= y_max; ++y) {
        float py = (float)y;
        uint16_t* row = &buf[y * 160];
        for (int x = x_min; x <= x_max; ++x) {
          float px = (float)x;
          float dx = px - center_x;
          float dy = py - (my - dx * tilt * 0.5f);
          float norm_d = sqrtf((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry)) - 1.0f;
          float d = norm_d * ((rx + ry) * 0.5f);

          if (d <= -0.5f) {
            row[x] = 0x0000;
          } else if (d < 0.5f) {
            float alpha = 0.5f - d;
            uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
            uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
            uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
            uint16_t col = makeRgb565(cr, cg, cb);
            if (row[x] != 0x0000) row[x] = col;
          }
        }
      }
      return;
    }
  }

  // Dynamic singing mouth for active music vibing (Pop, EDM, Dance)
  if (mood == PET_MOOD_DANCING && (vibe_style == 0 || vibe_style == 2)) {
    float eff_hz = s_vibe_hz;
    if (eff_hz > 1.35f) eff_hz *= 0.5f;
    if (eff_hz < 0.85f) eff_hz = 0.85f;
    if (eff_hz > 1.35f) eff_hz = 1.35f;
    float beat_dur = 1000.0f / eff_hz;
    float ph = fmodf((float)ms, beat_dur) / beat_dur * 6.283185f;
    float sing_k = fmaxf(0.0f, sinf(ph)) * 0.85f + fmaxf(0.0f, -sinf(ph)) * 0.35f;

    if (sing_k > 0.25f) {
      float rx = 3.2f + sing_k * 2.8f; // pulses 3.2px -> 6.0px
      float ry = 1.8f + sing_k * 4.0f; // pulses 1.8px -> 5.8px
      const float my = mouth_cy + sing_k * 1.2f;

      const int x_min = (int)fmaxf(0.0f, center_x - rx - 2.0f);
      const int x_max = (int)fminf(159.0f, center_x + rx + 2.0f);
      const int y_min = (int)fmaxf(0.0f, my - ry - 2.0f);
      const int y_max = (int)fminf(127.0f, my + ry + 2.0f);

      for (int y = y_min; y <= y_max; ++y) {
        float py = (float)y;
        uint16_t* row = &buf[y * 160];
        for (int x = x_min; x <= x_max; ++x) {
          float px = (float)x;
          float dx = (px - center_x) / rx;
          float dy = (py - my) / ry;
          float norm_d = sqrtf(dx * dx + dy * dy) - 1.0f;
          float d = norm_d * ((rx + ry) * 0.5f);

          if (d <= -0.5f) {
            row[x] = 0x0000;
          } else if (d < 0.5f) {
            float alpha = 0.5f - d;
            uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
            uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
            uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
            uint16_t col = makeRgb565(cr, cg, cb);
            if (row[x] != 0x0000) row[x] = col;
          }
        }
      }
      return;
    }
  }

  // 0 = neutral dash, 1 = happy smile, 2 = hiphop smirk, 3 = emotional droop, 4 = sleeping resting smile, 5 = angry scowl arc
  int style = 0;
  if (mood == PET_MOOD_HAPPY || mood == PET_MOOD_EXCITED) {
    style = 1;
  } else if (mood == PET_MOOD_SLEEPING) {
    style = 4; // peaceful curved resting smile (‿)
  } else if (mood == PET_MOOD_ANGRY) {
    style = 5; // downturned scowl arc (︵)
  } else if (mood == PET_MOOD_DANCING) {
    if (vibe_style == 1) style = 2; // hip-hop smirk
    else if (vibe_style == 5) style = 3; // emotional sad pout (only for heartbreak ballads)
    else if (vibe_style == 4) style = 4; // peaceful curved resting smile for lofi (‿)
    else if (vibe_style == 2 || vibe_style == 0) style = 1; // happy upbeat smile (‿)
    else style = 1;
  } else if (mood == PET_MOOD_SAD || mood == PET_MOOD_CRYING) {
    style = 3;
  }

  const int x_min = (int)(center_x - 8.0f);
  const int x_max = (int)(center_x + 8.0f);
  const int y_min = (int)(mouth_cy - 6.0f);
  const int y_max = (int)(mouth_cy + 6.0f);

  for (int y = y_min; y <= y_max; ++y) {
    float py = (float)y;
    uint16_t* row = &buf[y * 160];
    for (int x = x_min; x <= x_max; ++x) {
      float px = (float)x;
      float dx = px - center_x;
      float my = mouth_cy;

      if (style == 1) {
        // Uplifting joyful smile arc (corners raised)
        my = mouth_cy - (dx * dx) * 0.055f + 1.0f;
      } else if (style == 4) {
        // Peaceful curved resting smile for sleep mode (‿)
        my = mouth_cy - (dx * dx) * 0.045f + 0.8f;
      } else if (style == 2) {
        // Cocky swagger smirk: right corner tilts up with attitude, pulsing with 808 kick
        float beat_dur = (s_vibe_hz > 0.5f) ? (1000.0f / s_vibe_hz) : 500.0f;
        float ph = fmodf((float)ms, beat_dur) / beat_dur * 6.283185f;
        float kick = fmaxf(0.0f, -sinf(ph));
        float tilt = 0.24f + kick * 0.18f;
        my = mouth_cy - (dx > 0.0f ? dx * tilt : dx * 0.04f) + kick * 1.6f;
      } else if (style == 3) {
        // Vulnerable trembling droop (corners droop down into a sweet sad pout ︵)
        float tremble = sinf((float)ms * 0.02f) * 0.25f;
        my = mouth_cy + (dx * dx) * 0.075f - 1.1f + tremble;
      } else if (style == 5) {
        // Downturned clamped scowl arc for Angry mood (︵)
        my = mouth_cy + (dx * dx) * 0.065f - 1.2f;
      }

      float dy = py - my;
      float ox_limit = (style == 3) ? 3.0f : 4.5f;
      float ox = fmaxf(0.0f, fabsf(dx) - ox_limit);
      float d = sqrtf(ox * ox + dy * dy) - 1.2f; // clean smooth stroke

      if (d <= -0.5f) {
        row[x] = 0x0000;
      } else if (d < 0.5f) {
        float alpha = 0.5f - d;
        uint8_t cr = (uint8_t)(s_bg_r * (1.0f - alpha));
        uint8_t cg = (uint8_t)(s_bg_g * (1.0f - alpha));
        uint8_t cb = (uint8_t)(s_bg_b * (1.0f - alpha));
        uint16_t col = makeRgb565(cr, cg, cb);
        if (row[x] != 0x0000) row[x] = col;
      }
    }
  }
}

// ---------------------------------------------------------------------
// Primary Desk Pet Frame Drawing (100 FPS)
// ---------------------------------------------------------------------
void deskPetDrawFrame(LGFX_Sprite& sprite, uint32_t ms) {
  // Dynamic progressive background (Warm Kindle Paper Cream RGB 246, 242, 234 -> Progressive Fiery Red)
  const float base_bg_r = 246.0f;
  const float base_bg_g = 242.0f;
  const float base_bg_b = 234.0f;
  float target_bg_r = base_bg_r;
  float target_bg_g = base_bg_g;
  float target_bg_b = base_bg_b;
  if ((s_mood == PET_MOOD_ANGRY || s_mood == PET_MOOD_FRUSTRATED) && s_tint_amount > 0) {
    float a = (float)s_tint_amount / 255.0f;
    target_bg_r = base_bg_r - a * (base_bg_r - (float)s_tint_r);
    target_bg_g = base_bg_g - a * (base_bg_g - (float)s_tint_g);
    target_bg_b = base_bg_b - a * (base_bg_b - (float)s_tint_b);
  }
  // Smooth exponential glide (butter-smooth 100 FPS interpolation)
  s_curr_tint_r += (target_bg_r - s_curr_tint_r) * 0.035f;
  s_curr_tint_g += (target_bg_g - s_curr_tint_g) * 0.035f;
  s_curr_tint_b += (target_bg_b - s_curr_tint_b) * 0.035f;
  s_bg_r = (uint8_t)(s_curr_tint_r + 0.5f);
  s_bg_g = (uint8_t)(s_curr_tint_g + 0.5f);
  s_bg_b = (uint8_t)(s_curr_tint_b + 0.5f);

  uint16_t bg_val = makeRgb565(s_bg_r, s_bg_g, s_bg_b);
  uint16_t* pbuf = (uint16_t*)sprite.getBuffer();
  if (pbuf) {
    uint32_t bg32 = ((uint32_t)bg_val << 16) | bg_val;
    uint32_t* b32 = (uint32_t*)pbuf;
    for (int i = 0; i < (160 * 128) / 2; ++i) {
      b32[i] = bg32;
    }
  } else {
    sprite.fillSprite(bg_val);
  }

  if (s_pet_active_since_ms == 0) s_pet_active_since_ms = ms;

  // Expire temporary mood
  if (s_mood_until_ms > 0) {
    if (ms >= s_mood_until_ms) {
      s_mood_until_ms = 0;
      s_mood = PET_MOOD_NEUTRAL;
    }
  } else {
    // Only auto-progress if current mood is an idle mood (NEUTRAL, SLEEPY, YAWN)
    if (s_mood == PET_MOOD_NEUTRAL || s_mood == PET_MOOD_SLEEPY || s_mood == PET_MOOD_YAWN) {
      uint32_t idle_ms = ms - s_pet_active_since_ms;
      if (idle_ms >= 720000) {
        s_mood = PET_MOOD_DEEP_SLEEP; // Deep sleep after 12 min idle
      } else if (idle_ms >= 480000) {
        s_mood = PET_MOOD_SLEEPING;   // Normal sleep after 8 min idle
      } else if (idle_ms >= 420000) {
        if (s_mood != PET_MOOD_YAWN && s_last_idle_yawn_ms != s_pet_active_since_ms) {
          s_last_idle_yawn_ms = s_pet_active_since_ms;
          deskPetYawn();               // Yawn after 7 min idle
        }
      } else if (idle_ms >= 300000) {
        s_mood = PET_MOOD_SLEEPY;     // Drowsy squint after 5 min idle
      } else {
        s_mood = PET_MOOD_NEUTRAL;    // Awake, lively, observant companion
      }
    } else if (s_mood == PET_MOOD_SLEEPING) {
      // Naturally deepen sleep over time - never pop eyes open to neutral!
      uint32_t idle_ms = ms - s_pet_active_since_ms;
      if (idle_ms >= 720000) {
        s_mood = PET_MOOD_DEEP_SLEEP;
      }
    }
  }

  bool is_vibe = (s_mood == PET_MOOD_DANCING || s_mood == PET_MOOD_EXCITED);
  bool is_sleeping = (s_mood == PET_MOOD_SLEEPING || s_mood == PET_MOOD_DEEP_SLEEP);
  if (!is_vibe) s_vibe_amp *= 0.92f; // smooth decay out of music bounce

  // 1. Calm, Natural Biological Gaze (subtle, non-twitchy, serene)
  if (is_vibe || is_sleeping) {
    s_target_look_x = 0.0f;
    s_target_look_y = 0.0f;
  } else if (ms >= s_next_glance_ms) {
    // Hold calm gaze for 8 to 15 seconds before gentle micro-shift
    s_next_glance_ms = ms + 8000 + (esp_random() % 7000);
    uint32_t pick = esp_random() % 8;
    switch (pick) {
      case 0:
      case 1:
      case 2: s_target_look_x =  0.0f; s_target_look_y =  0.0f; break; // Calm Center (most common)
      case 3: s_target_look_x = -3.5f; s_target_look_y =  0.0f; break; // Gentle Left
      case 4: s_target_look_x =  3.5f; s_target_look_y =  0.0f; break; // Gentle Right
      case 5: s_target_look_x = -2.5f; s_target_look_y = -2.0f; break; // Soft Up-Left
      case 6: s_target_look_x =  2.5f; s_target_look_y = -2.0f; break; // Soft Up-Right
      default:s_target_look_x =  0.0f; s_target_look_y =  1.5f; break; // Soft Down
    }
  } else if (s_mood == PET_MOOD_THINKING) {
    s_target_look_x = -3.5f;
    s_target_look_y = -2.0f;
  }

  // Smooth, calm biological glide (eliminates any rapid eye twitching)
  float glide = 0.022f;
  s_curr_look_x += (s_target_look_x - s_curr_look_x) * glide;
  s_curr_look_y += (s_target_look_y - s_curr_look_y) * glide;

  // 2. Natural Organic Blink State Machine
  float blink_progress = 0.0f; // 0.0 = wide open, 1.0 = fully closed
  if (is_sleeping) {
    s_is_blinking = false; // NEVER blink during sleep!
    blink_progress = 0.0f;
  } else {
    if (!s_is_blinking && ms >= s_next_blink_ms) {
      s_is_blinking = true;
      s_blink_start_ms = ms;
    }

    if (s_is_blinking) {
      uint32_t elapsed = ms - s_blink_start_ms;
      const uint32_t T_CLOSE = 75;   // 75ms swift natural close sweep
      const uint32_t T_HOLD  = 45;   // 45ms closed seam hold
      const uint32_t T_OPEN  = 110;  // 110ms elastic ease-out opening sweep
      const uint32_t T_TOTAL = T_CLOSE + T_HOLD + T_OPEN; // 230ms total

      if (elapsed < T_CLOSE) {
        float t = (float)elapsed / (float)T_CLOSE;
        blink_progress = sinf(t * 1.5707963f); // 0.0 -> 1.0 (smooth deceleration close)
      } else if (elapsed < T_CLOSE + T_HOLD) {
        blink_progress = 1.0f; // fully closed
      } else if (elapsed < T_TOTAL) {
        float t = (float)(elapsed - (T_CLOSE + T_HOLD)) / (float)T_OPEN;
        blink_progress = 1.0f - sinf(t * 1.5707963f); // 1.0 -> 0.0 (smooth ease-out open)
      } else {
        s_is_blinking = false;
        blink_progress = 0.0f;
        // Natural randomized blink interval: 2.2s to 4.8s
        s_next_blink_ms = ms + 2200 + (esp_random() % 2600);
        // 20% chance of an authentic biological double-blink
        if ((esp_random() % 5) == 0) {
          s_next_blink_ms = ms + 180;
        }
      }
    }
  }

  // 3. Natural Breathing Physics (calm 3.8s respiration cycle)
  float breath_period = (s_mood == PET_MOOD_SLEEPING || s_mood == PET_MOOD_DEEP_SLEEP) ? 0.00105f :
                        (s_mood == PET_MOOD_SLEEPY) ? 0.00135f : 0.00165f;
  float breath = sinf((float)ms * breath_period) * 1.4f;

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

  // 5. Generative Mathematical Motion and Authentic Music Flow (Non-Repeating Multi-Harmonic Flow)
  float vibe_y = 0.0f;
  float sway_x = 0.0f;
  float eye_tilt_y = 0.0f;      // Asymmetric eye elevation delta for living human/pet head-tilt
  float squash_w = 0.0f;
  float squash_h = 0.0f;
  float nod = 0.0f;
  float lid_flow_left = 0.0f;   // Organic eyelid drift (left eye)
  float lid_flow_right = 0.0f;  // Organic eyelid drift (right eye)
  float brow_flow = 0.0f;       // Dynamic brow slope modulation

  if (is_vibe) {
    if (s_vibe_amp < 1.0f) s_vibe_amp = 2.5f; // Guarantee strong dance groove
    float eff_hz = s_vibe_hz;
    if (eff_hz > 1.45f) eff_hz *= 0.5f; // half-time downbeat groove (beats 1 & 3)
    if (eff_hz < 0.85f) eff_hz = 0.85f;
    if (eff_hz > 1.45f) eff_hz = 1.45f;

    // Incommensurate harmonic periods (Golden Ratio phi = 1.618034, e = 2.718282)
    // Guarantees authentic, non-repeating continuous flow across minutes of music
    float t_sec = (float)ms * 0.001f;
    float w0 = eff_hz * 6.283185f;                  // Primary beat angular frequency
    float w_phi = (eff_hz / 1.618034f) * 6.283185f; // Golden ratio sub-harmonic (swagger)
    float w_e   = (eff_hz / 2.718282f) * 6.283185f; // Macro emotional drift
    float w_fast= (eff_hz * 2.0f)      * 6.283185f; // Double-time upbeat
    float w_chop= (eff_hz * 4.0f)      * 6.283185f; // 16th-note chopper syncopation pulse

    // Beat phase calculation & organic cosine downbeat cushion
    float beat_dur = 1000.0f / eff_hz;
    float ph = fmodf((float)ms, beat_dur) / beat_dur * 6.283185f;

    // Primary downbeat cushion with multi-harmonic nuance
    float base_nod = powf(0.5f - 0.5f * cosf(ph), 1.35f);
    float sync_pulse = powf(0.5f - 0.5f * cosf(ph * 2.0f), 3.0f) * 0.25f;
    nod = base_nod + sync_pulse;

    // Head tilt asymmetry (cocking head into the musical pocket)
    eye_tilt_y = sinf(w_phi * t_sec) * 2.4f;

    // Organic eyelid and brow micro-modulations
    lid_flow_left  = sinf(w_e * t_sec + 0.35f) * 1.8f;
    lid_flow_right = sinf(w_e * t_sec + 1.95f) * 1.8f;
    brow_flow      = cosf(w_phi * t_sec) * 0.08f;

    // Energy from pc_stats / beat analyzer (0.35 to 1.20)
    float energy = (pcStatsIsActive() && g_pc_stats.music_vibe > 0.0f) ? g_pc_stats.music_vibe : 0.65f;
    if (energy < 0.35f) energy = 0.35f;
    if (energy > 1.20f) energy = 1.20f;

    switch (s_vibe_style) {
      case 1: { // Hip-Hop / Drill / Chopper Rap: punchy 808 drop, swagger lean & chopper micro-ticks
        // Base heavy downbeat nod:
        float heavy_nod = nod * (10.0f + energy * 3.5f);
        // Fast chopper 16th-note cadence when energy is high (fast flow lock):
        float chopper_cadence = (energy > 0.60f) ? (powf(0.5f - 0.5f * cosf(ph * 4.0f), 4.0f) * 2.5f) : 0.0f;
        vibe_y = heavy_nod + chopper_cadence;
        // Non-repeating multi-harmonic swagger sway:
        sway_x = (sinf(ph * 0.5f) * 4.8f + sinf(w_phi * t_sec) * 3.2f) * (0.8f + energy * 0.4f);
        squash_w = (nod * 4.0f + chopper_cadence * 0.6f);
        squash_h = -(nod * 6.8f + chopper_cadence * 1.2f);
        break;
      }
      case 2: { // EDM / House / Party: euphoric multi-stage pump, double-bounce & upbeat flight
        float upbeat_lift = -sinf(ph) * (9.0f + energy * 4.5f);
        float double_pump = sinf(ph * 2.0f) * (2.8f * energy);
        vibe_y = upbeat_lift + double_pump;
        sway_x = (sinf(ph * 0.5f) * 4.2f + cosf(w_phi * t_sec) * 2.8f);
        squash_w = -sinf(ph) * 3.8f;
        squash_h = sinf(ph) * 6.0f;
        break;
      }
      case 3: { // Rock / Metal: visceral headbang with syncopated double-kick rebound
        float bang = nod * (12.0f + energy * 4.5f);
        float kick_rebound = (powf(0.5f - 0.5f * cosf(ph * 2.0f), 3.0f) * 3.2f * energy);
        vibe_y = bang + kick_rebound;
        sway_x = sinf(ph * 0.5f) * 4.0f + sinf(w_fast * t_sec) * 1.4f;
        squash_w = nod * 5.0f;
        squash_h = -nod * 8.2f;
        break;
      }
      case 4: { // Lo-Fi / Chill: continuous Lissajous figure-8 flow, serene breath
        sway_x = sinf(t_sec * 1.8f) * 5.5f + sinf(t_sec * 0.7f) * 2.0f;
        vibe_y = cosf(t_sec * 3.6f) * 2.2f + sinf(t_sec * 1.1f) * 1.2f;
        squash_w = sinf(t_sec * 1.8f) * 1.5f;
        squash_h = -squash_w;
        break;
      }
      case 5: { // Breakup Phase / Heartbreak: drunken melancholic sway, sorrowful slump, periodic gulp hiccup
        float drink_cycle = fmodf(t_sec, 6.0f);
        float hiccup_y = 0.0f;
        if (drink_cycle >= 4.7f && drink_cycle < 5.1f) {
          // Drunken heartbreak gulp / hiccup!
          hiccup_y = -sinf((drink_cycle - 4.7f) / 0.4f * 3.14159f) * 4.2f;
        }
        sway_x = sinf(t_sec * 0.85f) * 3.8f + cosf(t_sec * 0.35f) * 1.5f;
        vibe_y = 5.5f + cosf(t_sec * 1.7f) * 1.6f + hiccup_y; // heavy dejected slump (head hanging low in sorrow)
        squash_w = -sinf(t_sec * 0.85f) * 1.5f;
        squash_h = -squash_w;
        eye_tilt_y = sinf(t_sec * 0.85f) * 0.5f; // feeble drunken wobble, NO swagger tilt!
        break;
      }
      default: { // Pop / Upbeat Groove: joyful bounce, organic syncopated downbeat
        vibe_y = nod * (8.5f + energy * 3.0f);
        sway_x = sinf(ph * 0.5f) * 4.2f + sinf(w_phi * t_sec) * 2.4f;
        squash_w = nod * 3.5f;
        squash_h = -nod * 5.2f;
        break;
      }
    }
  }

  // 6. Eye Coordinates (Centered on 160x128 display with strict screen clamping)
  // Base dimensions: 38w x 48h, 14r
  float raw_left_cx  = 53.0f + s_curr_look_x + sway_x;
  float raw_right_cx = 107.0f + s_curr_look_x + sway_x;
  float raw_left_cy  = 62.0f + s_curr_look_y + bounce_y + vibe_y + eye_tilt_y;
  float raw_right_cy = 62.0f + s_curr_look_y + bounce_y + vibe_y - eye_tilt_y;
  float raw_center_y = 62.0f + s_curr_look_y + bounce_y + vibe_y;

  // Strict boundary clamps: prevent eyebrows, eyes, or lips from ever leaving the screen (0..159, 0..127)
  const float center_y = fmaxf(52.0f, fminf(68.0f, raw_center_y));
  const float left_cx  = fmaxf(32.0f, fminf(64.0f, raw_left_cx));
  const float right_cx = fmaxf(96.0f, fminf(128.0f, raw_right_cx));
  const float left_cy  = fmaxf(48.0f, fminf(72.0f, raw_left_cy));
  const float right_cy = fmaxf(48.0f, fminf(72.0f, raw_right_cy));

  // Eye dimensions with squash-and-stretch:
  float base_w = 38.0f + squash_w;
  float base_h = 48.0f + breath + squash_h;
  if (base_h < 12.0f) base_h = 12.0f;
  float eye_r = 14.0f;
  bool show_glint = (blink_progress < 0.40f);

  // Eyelid sweep and closed anime seam lambda
  auto drawEyeWithBlink = [&](float cx, float cy, float w, float h, float r,
                              float top_lid_y = 999.0f, float top_slope = 0.0f,
                              float bot_lid_y = -999.0f, float bot_curve = 0.0f,
                              float slope_seam = 0.0f) {
    if (blink_progress >= 0.88f) {
      drawClosedEyeSeam(sprite, cx, cy, w, slope_seam);
    } else {
      float sweep_y = -(h * 0.5f) + blink_progress * (h * 1.05f);
      float eff_top = (top_lid_y < 900.0f) ? fmaxf(top_lid_y, sweep_y) : sweep_y;
      bool glint_on = (blink_progress < 0.40f) && show_glint;
      drawLivingEyeAA(sprite, cx, cy, w, h, r,
                      eff_top, top_slope, bot_lid_y, bot_curve,
                      s_curr_look_x, s_curr_look_y, glint_on);
    }
  };

  // 7. Expressive Mood Rendering
  switch (s_mood) {
    case PET_MOOD_HAPPY: {
      // Joyful squint: bottom lid curved up warmly
      drawEyeWithBlink(left_cx, center_y, base_w, base_h, eye_r,
                       -14.0f, 0.0f, 12.0f, 0.018f);
      drawEyeWithBlink(right_cx, center_y, base_w, base_h, eye_r,
                       -14.0f, 0.0f, 12.0f, 0.018f);
      break;
    }

    case PET_MOOD_DANCING: {
      // Vibing to the track based on genre!
      switch (s_vibe_style) {
        case 1: { // Hip-Hop / Drill / Chopper Rap: swagger half-lidded nod with attitude
          // Organic eyelid and brow micro-modulations:
          float drill_lid_l = -11.5f + nod * 1.5f + lid_flow_left;
          float drill_lid_r = -11.5f + nod * 1.5f + lid_flow_right;
          float slope_l = 0.22f + brow_flow;
          float slope_r = -0.22f - brow_flow;
          drawEyeWithBlink(left_cx, left_cy, base_w, base_h, eye_r,
                           drill_lid_l, slope_l, -999.0f, 0.0f, 0.15f);
          drawEyeWithBlink(right_cx, right_cy, base_w, base_h, eye_r,
                           drill_lid_r, slope_r, -999.0f, 0.0f, -0.15f);
          break;
        }
        case 2: { // EDM / House / Party: alert, wide, high energy joyful vibe
          float edm_h = 52.0f + breath + squash_h;
          drawEyeWithBlink(left_cx, left_cy, base_w + 2.0f, edm_h, 15.0f,
                           -16.0f, 0.0f, 14.0f, 0.015f);
          drawEyeWithBlink(right_cx, right_cy, base_w + 2.0f, edm_h, 15.0f,
                           -16.0f, 0.0f, 14.0f, 0.015f);
          break;
        }
        case 3: { // Rock / Metal: sharp determined brow headbang
          float rock_slope_l = 0.42f + brow_flow;
          float rock_slope_r = -0.42f - brow_flow;
          drawEyeWithBlink(left_cx, left_cy, base_w, base_h, eye_r,
                           -7.0f, rock_slope_l, -999.0f, 0.0f, 0.25f);
          drawEyeWithBlink(right_cx, right_cy, base_w, base_h, eye_r,
                           -7.0f, rock_slope_r, -999.0f, 0.0f, -0.25f);
          break;
        }
        case 4: { // Lo-Fi / Chill: mellow relaxed eyes, peaceful sway (no tears)
          drawEyeWithBlink(left_cx, left_cy, base_w, base_h, eye_r,
                           -5.0f + lid_flow_left, 0.0f, -999.0f, 0.0f);
          drawEyeWithBlink(right_cx, right_cy, base_w, base_h, eye_r,
                           -5.0f + lid_flow_right, 0.0f, -999.0f, 0.0f);
          break;
        }
        case 5: { // Breakup Phase: Crying, Watering Eyes, Drinking Alcohol, Drunken Heartbroken Sway
          // Heavy anime tears streaming down cheeks with shimmering liquid wave
          drawWateringEyes(sprite, left_cx, right_cx, center_y, base_w, base_h, eye_r, ms, true, blink_progress);
          // Sleek minimalist alcohol bottle sipping and resting animation
          drawBreakupBottle(sprite, 80.0f, center_y + 32.0f, ms);
          break;
        }
        default: { // Pop / Upbeat Groove: joyful smiling bounce
          drawEyeWithBlink(left_cx, left_cy, base_w, base_h, eye_r,
                           -15.0f, 0.0f, 13.0f, 0.015f);
          drawEyeWithBlink(right_cx, right_cy, base_w, base_h, eye_r,
                           -15.0f, 0.0f, 13.0f, 0.015f);
          break;
        }
      }
      break;
    }

    case PET_MOOD_EXCITED: {
      // Wide joyful eyes with happy bottom lid
      drawEyeWithBlink(left_cx, center_y, base_w, base_h, eye_r,
                       -15.0f, 0.0f, 14.0f, 0.015f);
      drawEyeWithBlink(right_cx, center_y, base_w, base_h, eye_r,
                       -15.0f, 0.0f, 14.0f, 0.015f);
      break;
    }

    case PET_MOOD_WINK: {
      // Left eye winks smoothly to a 5px slit, Right eye open alert with catchlights
      drawLivingEyeAA(sprite, left_cx, center_y, 34.0f, 5.0f, 2.5f,
                      999.0f, 0.0f, -999.0f, 0.0f,
                      0.0f, 0.0f, false);
      drawEyeWithBlink(right_cx, center_y, base_w, base_h, eye_r);
      break;
    }

    case PET_MOOD_SLEEPING: {
      // Normal sleep: peaceful curved crescents (︶ ︶) + resting smile + diagonal Zzz
      drawSleepingEyesAA(sprite, left_cx, center_y, breath);
      drawSleepingEyesAA(sprite, right_cx, center_y, breath);
      drawFloatingZzz(sprite, ms);
      break;
    }

    case PET_MOOD_DEEP_SLEEP: {
      // Deep sleep: peaceful crescents + parted mouth + animated nose bubble + diagonal Zzz
      drawSleepingEyesAA(sprite, left_cx, center_y, breath);
      drawSleepingEyesAA(sprite, right_cx, center_y, breath);
      drawSleepBubble(sprite, 86.5f, center_y + 24.0f, ms);
      drawFloatingZzz(sprite, ms);
      break;
    }

    case PET_MOOD_YAWN: {
      // Yawning eyes: tightly squished curved sleeping slits with peak tremor
      float yawn_prog = (s_yawn_start_ms > 0 && ms >= s_yawn_start_ms) ?
                        fminf(1.0f, (float)(ms - s_yawn_start_ms) / 2800.0f) : 0.5f;
      float peak_tremble = (yawn_prog >= 0.35f && yawn_prog <= 0.65f) ?
                           sinf((float)ms * 0.05f) * 0.6f : 0.0f;
      drawLivingEyeAA(sprite, left_cx, center_y + breath + peak_tremble, 34.0f, 5.0f, 2.5f,
                      -1.0f, 0.08f, 1.0f, 0.02f,
                      0.0f, 0.0f, false);
      drawLivingEyeAA(sprite, right_cx, center_y + breath + peak_tremble, 34.0f, 5.0f, 2.5f,
                      -1.0f, -0.08f, 1.0f, 0.02f,
                      0.0f, 0.0f, false);
      break;
    }

    case PET_MOOD_SLEEPY: {
      // Heavy drowsy eyelids
      drawEyeWithBlink(left_cx, center_y, base_w, base_h, eye_r,
                       -3.5f, 0.0f, -999.0f, 0.0f);
      drawEyeWithBlink(right_cx, center_y, base_w, base_h, eye_r,
                       -3.5f, 0.0f, -999.0f, 0.0f);
      break;
    }

    case PET_MOOD_SURPRISED:
    case PET_MOOD_SCARED: {
      // Dilated alert capsules
      float surp_h = 54.0f + breath;
      drawEyeWithBlink(left_cx, center_y, 38.0f, surp_h, 16.0f);
      drawEyeWithBlink(right_cx, center_y, 38.0f, surp_h, 16.0f);
      break;
    }

    case PET_MOOD_ANGRY:
    case PET_MOOD_FRUSTRATED: {
      // Sharp inward brow slant with slow tense smoldering heave for frustrated
      float tension_heave = (s_mood == PET_MOOD_FRUSTRATED) ? sinf((float)ms * 0.0075f) * 0.8f : 0.0f;
      float top_cut = (s_mood == PET_MOOD_FRUSTRATED) ? -2.5f : -4.5f;
      float slope_k = (s_mood == PET_MOOD_FRUSTRATED) ? 0.52f : 0.45f;
      drawEyeWithBlink(left_cx + tension_heave, center_y, base_w, base_h, eye_r,
                       top_cut, slope_k, -999.0f, 0.0f, 0.25f);
      drawEyeWithBlink(right_cx + tension_heave, center_y, base_w, base_h, eye_r,
                       top_cut, -slope_k, -999.0f, 0.0f, -0.25f);
      break;
    }

    case PET_MOOD_SAD:
    case PET_MOOD_CRYING: {
      // Heavy blue watering eyes effect: sloshing liquid meniscus + cheek trickle on crying
      drawWateringEyes(sprite, left_cx, right_cx, center_y, base_w, base_h, eye_r, ms, (s_mood == PET_MOOD_CRYING), blink_progress);
      break;
    }

    case PET_MOOD_SICK:
    case PET_MOOD_FEVER:
    case PET_MOOD_COLD:
    case PET_MOOD_HOT: {
      float shiver = (s_mood == PET_MOOD_COLD) ? sinf((float)ms * 0.09f) * 1.5f : 0.0f;
      drawEyeWithBlink(left_cx + shiver, center_y, base_w, base_h * 0.65f, eye_r * 0.65f,
                       -2.0f, 0.0f, -999.0f, 0.0f);
      drawEyeWithBlink(right_cx + shiver, center_y, base_w, base_h * 0.65f, eye_r * 0.65f,
                       -2.0f, 0.0f, -999.0f, 0.0f);
      break;
    }

    case PET_MOOD_THINKING:
    case PET_MOOD_NEUTRAL:
    default: {
      // Pure living squircle eyes with dual specular catchlights
      drawEyeWithBlink(left_cx, center_y, base_w, base_h, eye_r);
      drawEyeWithBlink(right_cx, center_y, base_w, base_h, eye_r);
      break;
    }
  }

  // 8. Responsive Procedural Eyebrows
  drawProceduralBrows(sprite, left_cx, right_cx, center_y, s_mood, s_vibe_style, ms, blink_progress);

  // 9. Sleek Minimal Lip (sways cohesively with head and eyes)
  drawMinimalLip(sprite, 80.0f + s_curr_look_x * 0.5f + sway_x, center_y, s_mood, s_vibe_style, ms);

  // 10. Forehead Anger Vein for Angry and Frustrated moods
  if (s_mood == PET_MOOD_ANGRY) {
    drawAngerVein(sprite, 124.0f, 24.0f, 0.72f, s_bg_r, s_bg_g, s_bg_b, ms);
  } else if (s_mood == PET_MOOD_FRUSTRATED) {
    // Dual pulsating anger pop marks: primary right temple + secondary left temple
    drawAngerVein(sprite, 124.0f, 24.0f, 0.78f, s_bg_r, s_bg_g, s_bg_b, ms);
    drawAngerVein(sprite, 36.0f, 26.0f, 0.62f, s_bg_r, s_bg_g, s_bg_b, ms + 450);
  }

  // 11. Compact PC Hardware & Weather Pill (if PC stats stream is active AND user has enabled it)
  if (g_settings.pet_show_stats && pcStatsIsActive()) {
    const int pill_x = 12;
    const int pill_y = 106;
    const int pill_w = 136;
    const int pill_h = 17;
    const int pill_r = 8;

    // Subtle warm paper capsule with carbon ink typography
    sprite.fillRoundRect(pill_x, pill_y, pill_w, pill_h, pill_r, 0xEF59);
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
    sprite.setTextColor(0x1082, 0xEF59);
    sprite.drawString(pill_str, 80, pill_y + pill_h / 2);
  }
}
