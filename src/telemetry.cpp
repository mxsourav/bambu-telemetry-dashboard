#include "telemetry.h"
#include "config.h"

TelemetryData g_telemetry;
SemaphoreHandle_t telemetryMutex = nullptr;
volatile uint8_t ui_latched_result = 0;   // 0=none 1=complete 2=failed

void telemetryInit() {
  telemetryMutex = xSemaphoreCreateMutex();
}

void telemetryWrite(const TelemetryData& d) {
  if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(TELEMETRY_MUTEX_TIMEOUT_MS)) == pdTRUE) {
    g_telemetry = d;
    xSemaphoreGive(telemetryMutex);
  }
}

void telemetryRead(TelemetryData& out) {
  if (xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(TELEMETRY_MUTEX_TIMEOUT_MS)) == pdTRUE) {
    out = g_telemetry;
    xSemaphoreGive(telemetryMutex);
  }
}

const char* printStateName(PrintState s) {
  switch (s) {
    case PST_IDLE:         return "IDLE";
    case PST_PREPARE:      return "PREPARE";
    case PST_RUNNING:      return "RUNNING";
    case PST_PAUSED:       return "PAUSED";
    case PST_FINISH:       return "FINISH";
    case PST_FAILED:       return "FAILED";
    case PST_TIMELAPSE:    return "TIMELAPSE";
    case PST_DISCONNECTED: return "OFFLINE";
  }
  return "?";
}
