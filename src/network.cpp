#include "network.h"
#include "config.h"
#include "secrets.h"
#include "telemetry.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);
bool network_ever_connected = false;   // set true after first MQTT connect

// ---------------- state-transition beeps / alarms --------------------
// Peripherals task polls g_telemetry.state, so transitions are handled
// there; this file only WRITES telemetry.

static PrintState mapGcodeState(const char* s) {
  if (!s) return PST_IDLE;
  if (!strcmp(s, "RUNNING"))  return PST_RUNNING;
  if (!strcmp(s, "PREPARE"))  return PST_PREPARE;
  if (!strcmp(s, "PAUSE"))    return PST_PAUSED;
  if (!strcmp(s, "FINISH"))   return PST_FINISH;
  if (!strcmp(s, "FAILED"))   return PST_FAILED;
  return PST_IDLE;
}

// Filament usage: only the direct `filament_used` field (mm) if the
// firmware reports it. 1.75 mm PLA: 2.405 mg/mm -> grams.
static void updateFilament(JsonObject print_, TelemetryData& d) {
  if (!print_["filament_used"].isNull()) {
    double mm = print_["filament_used"];
    d.filament_g = (float)(mm * 0.002405 * 1000.0);   // mm -> grams
  }
}

// ---------------- MQTT payload parsing --------------------------------
// Bambu payloads are ~1-3 KB nested JSON. Parse in-place with a reused
// JsonDocument (no per-message allocation => no heap fragmentation).
static void parseTelemetry(const char* payload, uint32_t len) {
  JsonDocument doc;   // ArduinoJson v7: elastic, preallocated capacity
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[MQTT] JSON parse failed: %s\n", err.c_str());
    return;
  }

  TelemetryData d;
  telemetryRead(d);   // carry forward last_update etc. as baseline

  JsonObject print_ = doc["print"];
  if (!print_.isNull()) {
    d.state       = mapGcodeState(print_["gcode_state"] | (const char*)nullptr);
    d.percent     = print_["mc_percent"] | d.percent;
    d.nozzle_temp = print_["nozzle_temper"] | d.nozzle_temp;
    d.nozzle_target = print_["nozzle_target"] | d.nozzle_target;
    d.bed_temp    = print_["bed_temper"] | d.bed_temp;
    d.bed_target  = print_["bed_target"] | d.bed_target;
    d.remain_min  = print_["mc_remaining_time"] | d.remain_min;
    d.stg_cur     = print_["stg_cur"] | d.stg_cur;
    d.layer_num   = print_["layer_num"] | d.layer_num;
    d.total_layers = print_["total_layers"] | d.total_layers;

    JsonArray hms = print_["hms"];
    d.hms_count   = hms.isNull() ? 0 : (int16_t)hms.size();

    updateFilament(print_, d);
  }
  d.last_update_ms = millis();
  telemetryWrite(d);
}

// ---------------- MQTT callbacks / reconnect --------------------------
static void mqttCallback(char* topic, byte* payload, uint32_t length) {
  parseTelemetry((const char*)payload, length);
}

static void connectMQTT() {
  char topic[80];
  snprintf(topic, sizeof(topic), "%s%s%s",
           MQTT_TOPIC_PREFIX, PRINTER_SN, MQTT_TOPIC_SUFFIX);

  // Bambu broker uses a self-signed cert => setInsecure() skips CA
  // validation but keeps the TLS 1.2 tunnel encrypted.
  tlsClient.setInsecure();
  tlsClient.setTimeout(10);        // seconds (WiFiClientSecure API) - was 12000ms read as 12000s!
  mqtt.setServer(PRINTER_IP, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE);   // payloads exceed 2 KB
  mqtt.setSocketTimeout(10);       // MQTT-level timeout, seconds

  Serial.printf("[MQTT] connecting to %s:%d ...\n", PRINTER_IP, MQTT_PORT);
  if (mqtt.connect("bambu-dash", MQTT_USER, PRINTER_LAN_CODE)) {
    network_ever_connected = true;
    Serial.printf("[MQTT] connected, subscribing to %s\n", topic);
    mqtt.subscribe(topic, 1);
  } else {
    Serial.printf("[MQTT] connect failed, rc=%d\n", mqtt.state());
  }
}

// ---------------- Task body -------------------------------------------
static void mqttTask(void* /*param*/) {
  static uint32_t last_wifi_attempt = 0;
  static uint32_t last_mqtt_attempt = 0;

  // fast-start: begin immediately, retry aggressively (1.2 s) until the
  // first association, then back off to 5 s cycles for stability.
  WiFi.mode(WIFI_STA);            // STA only - BT off, AP off
  WiFi.setSleep(true);            // modem sleep: -20..30 mA avg draw
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // attempt #1 at t=0
  last_wifi_attempt = millis();

  while (true) {
    // --- WiFi (re)connection ---
    if (WiFi.status() != WL_CONNECTED) {
      uint32_t retry_ms = network_ever_connected ? 5000 : 1200;
      if (millis() - last_wifi_attempt > retry_ms) {
        last_wifi_attempt = millis();
        Serial.printf("[WiFi] retry %s ...\n", WIFI_SSID);
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // fresh begin (reconnect can stall)
      }
      TelemetryData d;
      telemetryRead(d);
      d.state = PST_DISCONNECTED;
      telemetryWrite(d);
      vTaskDelay(pdMS_TO_TICKS(100));           // snappy retry loop
      continue;
    }
    // one-time IP print + NTP (IST, Asia/Kolkata = UTC+5:30)
    static bool ip_logged = false;
    if (!ip_logged) {
      ip_logged = true;
      Serial.printf("[WiFi] connected, IP: %s\n",
                    WiFi.localIP().toString().c_str());
      configTime(5 * 3600 + 1800, 0, "pool.ntp.org", "time.nist.gov");
      Serial.println(F("[NTP] configured (IST)"));
    }

    // --- MQTT (re)connection ---
    if (!mqtt.connected()) {
      uint32_t retry_ms = network_ever_connected ? 5000 : 1200;
      if (millis() - last_mqtt_attempt > retry_ms) {
        last_mqtt_attempt = millis();
        connectMQTT();
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // --- connected: pump the client (blocking read w/ 50 ms yield) ---
    mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void networkTaskStart() {
  xTaskCreatePinnedToCore(
      mqttTask, "mqttTask", TASK_STACK_NET, nullptr, 1, nullptr, 0 /* Core 0 */);
}
