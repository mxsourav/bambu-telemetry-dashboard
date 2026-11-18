/* =====================================================================
 *  network.h / network.cpp  -  WiFi + TLS MQTT task (pinned to Core 0)
 * ===================================================================== */
#pragma once
#include <Arduino.h>

void networkTaskStart();   // spawns mqttTask on Core 0

// MQTT pause/resume for full-screen image pushes: pausing drops the TLS
// session (frees ~45 KB heap, stops telemetry overwrites) while WiFi
// stays up; resuming reconnects immediately.
void mqttPause();
void mqttResume();
bool mqttIsPaused();
