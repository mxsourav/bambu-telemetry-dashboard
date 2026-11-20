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
  float gpu_percent = 0.0f;
  float gpu_temp_c = 0.0f;
  float net_down_kbs = 0.0f;
  float net_up_kbs = 0.0f;
  float weather_temp_c = 0.0f;
  char  weather_desc[16] = "";
  char  city[16] = "";
  uint32_t last_update_ms = 0;
};

extern PcStats g_pc_stats;

inline bool pcStatsIsActive() {
  return (g_pc_stats.last_update_ms > 0 && (millis() - g_pc_stats.last_update_ms < 12000));
}
