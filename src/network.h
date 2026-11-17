/* =====================================================================
 *  network.h / network.cpp  -  WiFi + TLS MQTT task (pinned to Core 0)
 * ===================================================================== */
#pragma once
#include <Arduino.h>

void networkTaskStart();   // spawns mqttTask on Core 0
