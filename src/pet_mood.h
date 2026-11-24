#pragma once
#include <Arduino.h>
#include "telemetry.h"

// =====================================================================
//  pet_mood.h - The Desk Pet's emotional brain
// =====================================================================
//  Runs every pet frame (100 FPS) on the UI task, evaluates the mood
//  priority ladder (printer events > temperature > weather > sickness >
//  music > night > watching > idle) and drives desk_pet.h rendering:
//  mood + cheek tint + bounce. Also exposes the web API hooks.
// =====================================================================

void petMoodUpdate(uint32_t ms, const TelemetryData& d);

// Force a mood by name from the web/PC side ("angry", "happy", "sick",
// "fever", "cold", "hot", "dancing", ...). "treat" cures sickness.
void petMoodForce(const char* mood_name, uint32_t seconds);

// Called when the pet view is exited (restore LED / clear transient).
void petMoodOnLeave();

// Celebrate / react after the print overlay is ACKed:
// kind 1 = print complete (EXCITED party), 2 = failed (relieved sad).
void petMoodCelebrate(uint8_t kind);

// Wake the pet immediately from any sleep/drowsy state upon music playback
void petMoodWakeFromMusic();

// Status JSON for the web dashboard (mood, anger ramp, night, music).
void petMoodStatusJson(char* out, size_t len);

// Expose vibe style and genre signature colors for LED music sync
uint8_t petMoodGetVibeStyle();
void petMoodGetGenreColor(uint8_t& r, uint8_t& g, uint8_t& b);
