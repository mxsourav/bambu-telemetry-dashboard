#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>

// =====================================================================
//  desk_pet.h - High-framerate Minimalist Procedural Desk Pet Engine
// =====================================================================
//  100 FPS procedural vector eyes with saccadic glances, fluid blinks,
//  natural breathing physics, and touch/click reactions.
//  Zero tacky "vibe/cyber" clutter; pure Dieter Rams / Braun minimalism.
//
//  v2 (Mood Engine): extended emotion set, cheek-blush tinting, sweat /
//  tear particles, shiver & dance physics. The brain lives in
//  pet_mood.cpp; this engine only renders what it is told.
// =====================================================================

enum PetMood : uint8_t {
  PET_MOOD_NEUTRAL = 0,
  PET_MOOD_HAPPY,       // Cheerful squint (^ ^)
  PET_MOOD_WINK,        // Playful wink (; ^)
  PET_MOOD_SLEEPY,      // Heavy drowsy eyelids
  PET_MOOD_SLEEPING,    // Resting horizontal lines (- -)
  PET_MOOD_SURPRISED,   // Alert wide eyes (O O)
  // ---- v2 emotions (driven by pet_mood.cpp / web API) ----
  PET_MOOD_ANGRY,       // Slanted brows, narrowed eyes, red blush ramp
  PET_MOOD_FRUSTRATED,  // Anger kept too long: deeper frown + shudder
  PET_MOOD_SAD,         // Droopy eyes + frown mouth
  PET_MOOD_CRYING,      // Sad + falling tear drops
  PET_MOOD_SICK,        // Wavy mouth, greenish blush, droopy lids
  PET_MOOD_FEVER,       // Sick + deep red blush + forehead sweat
  PET_MOOD_COLD,        // Shivering jitter, blue blush, squinted eyes
  PET_MOOD_HOT,         // Panting, orange blush, sweat drop
  PET_MOOD_EXCITED,     // Big bouncy happy arcs + pink blush
  PET_MOOD_DANCING,     // Music vibe: bounce to the beat + happy arcs
  PET_MOOD_SCARED,      // Tiny wide eyes + tremble + small "o" mouth
  PET_MOOD_THINKING     // Eyes drift up-left, slow thoughtful breathing
};

void deskPetInit();
void deskPetDrawFrame(LGFX_Sprite& sprite, uint32_t ms);
void deskPetReactClick();
void deskPetSetMood(PetMood mood, uint32_t duration_ms = 0);
PetMood deskPetGetMood();

// ---- v2 mood-engine hooks ----
// Cheek blush: RGB tint + 0..255 amount (0 = invisible). Also drives a
// faint background tint at high intensity (fever / rage).
void deskPetSetTint(uint8_t r, uint8_t g, uint8_t b, uint8_t amount);
// Music vibe bounce: amplitude px and beat frequency Hz (0 amp = off).
void deskPetSetBounce(float amp_px, float freq_hz);
// Vibe STYLE from the track's genre: 0=groove, 1=cool drill nod,
// 2=hype rave, 3=rock headbang, 4=chill sway.
void deskPetSetVibeStyle(uint8_t style);
// True while a transient user-reaction (click bounce / startled wake)
// owns the face - the mood engine yields to it.
bool deskPetIsTransient();
// Human-readable mood name for the web dashboard.
const char* deskPetMoodName(PetMood mood);
