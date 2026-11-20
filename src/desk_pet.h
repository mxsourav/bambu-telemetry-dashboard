#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>

// =====================================================================
//  desk_pet.h - High-framerate Minimalist Procedural Desk Pet Engine
// =====================================================================
//  100 FPS procedural vector eyes with saccadic glances, fluid blinks,
//  natural breathing physics, and touch/click reactions.
//  Zero tacky "vibe/cyber" clutter; pure Dieter Rams / Braun minimalism.
// =====================================================================

enum PetMood : uint8_t {
  PET_MOOD_NEUTRAL = 0,
  PET_MOOD_HAPPY,       // Cheerful squint (^ ^)
  PET_MOOD_WINK,        // Playful wink (; ^)
  PET_MOOD_SLEEPY,      // Heavy drowsy eyelids
  PET_MOOD_SLEEPING,    // Resting horizontal lines (- -)
  PET_MOOD_SURPRISED    // Alert wide eyes (O O)
};

void deskPetInit();
void deskPetDrawFrame(LGFX_Sprite& sprite, uint32_t ms);
void deskPetReactClick();
void deskPetSetMood(PetMood mood, uint32_t duration_ms = 0);
PetMood deskPetGetMood();
