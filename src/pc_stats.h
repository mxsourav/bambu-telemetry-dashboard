#pragma once
#include <Arduino.h>

// =====================================================================
//  pc_stats.h - Real-Time PC Hardware Telemetry & Weather Data Model
// =====================================================================

struct PcStats {
  float cpu_percent = 0.0f;
  float ram_percent = 0.0f;
  float ram_used_gb = 0.0f;
  float ram_total_gb = 0.0f;
  float disk_percent = 0.0f;
  float disk_used_gb = 0.0f;
  float disk_total_gb = 0.0f;
  float gpu_percent = 0.0f;
  float gpu_temp_c = 0.0f;
  float net_down_kbs = 0.0f;
  float net_up_kbs = 0.0f;
  float weather_temp_c = 0.0f;
  char  weather_desc[16] = "";
  char  city[16] = "";
  float music_vibe = 0.5f;   // music energy 0..1 from the PC sync script
                             // (calm -> gentle sway, energetic -> big bounce)
  char  music_genre[24] = "";// track genre from iTunes lookup (Drill, Metal,
                             // Lo-Fi...) -> picks the pet's dance style
  float music_bpm = 120.0f;  // real-time tempo from audio FFT or metadata
  uint32_t last_beat_ms = 0; // timestamp of last detected beat pulse
  uint32_t last_update_ms = 0;
};

extern PcStats g_pc_stats;

inline bool pcStatsIsActive() {
  return (g_pc_stats.last_update_ms > 0 && (millis() - g_pc_stats.last_update_ms < 12000));
}
