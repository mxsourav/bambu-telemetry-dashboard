/* =====================================================================
 *  telemetry.h / telemetry.cpp  -  shared data + thread-safe access
 * =====================================================================
 *  MQTT task (Core 0) is the only WRITER of TelemetryData.
 *  UI task (Core 1) is the only READER.
 *  Both lock telemetryMutex for the duration of access; the mutex has
 *  priority inheritance so a slow network write can never starve the UI.
 * ===================================================================== */
#pragma once
#include <Arduino.h>

enum PrintState : uint8_t {
  PST_IDLE = 0,
  PST_PREPARE,
  PST_RUNNING,
  PST_PAUSED,
  PST_FINISH,
  PST_FAILED,
  PST_TIMELAPSE,   // overlay flag set by stg_cur snapshot detection
  PST_DISCONNECTED // MQTT down / never connected
};

struct TelemetryData {
  PrintState state      = PST_DISCONNECTED;
  int16_t    percent    = 0;      // mc_percent 0..100
  float      nozzle_temp = 0.0f;  // nozzle_temper
  float      nozzle_target = 0.0f;
  float      bed_temp    = 0.0f;  // bed_temper
  float      bed_target  = 0.0f;
  int16_t    remain_min = -1;     // mc_remaining_time (minutes)
  int16_t    hms_count  = 0;      // >0 => error alarm
  int16_t    stg_cur    = -1;     // stage id (timelapse detection)
  int16_t    layer_num  = -1;     // current layer
  int16_t    total_layers = -1;
  float      filament_g = -1.0f;  // filament used this print (grams; -1 unknown)
  uint32_t   last_update_ms = 0;  // millis() of last MQTT message
};

// Defined in telemetry.cpp
extern TelemetryData g_telemetry;
extern SemaphoreHandle_t telemetryMutex;

// Latched result overlay (0=none 1=complete 2=failed). Owned by UI task
// (set on MQTT FINISH/FAILED, cleared by Select=OK); read by peripherals
// task for the latch-driven LED. Simple volatile - single-byte atomic.
extern volatile uint8_t ui_latched_result;

void telemetryInit();                       // create mutex
void telemetryWrite(const TelemetryData& d);   // Core 0 only
void telemetryRead(TelemetryData& out);        // Core 1 (and anyone) only
const char* printStateName(PrintState s);
