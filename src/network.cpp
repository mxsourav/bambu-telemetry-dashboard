#include "network.h"
#include "config.h"
#include "secrets.h"
#include "telemetry.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClientSecure tlsClient;
static PubSubClient mqtt(tlsClient);
bool network_ever_connected = false;   // set true after first MQTT connect

// ---------------- MQTT pause/resume (display image pushes) -------------
// While an image is pushed/shown the TLS session is dropped: telemetry
// stops overwriting the screen and ~45 KB of mbedTLS heap is freed for
// the HTTP upload. WiFi stays connected. Any button press on the device
// resumes MQTT (immediate reconnect).
static volatile bool s_mqtt_paused = false;
static uint32_t s_mqtt_last_attempt = 0;   // shared with mqttTask

void mqttPause() {
  if (s_mqtt_paused) return;
  s_mqtt_paused = true;
  mqtt.disconnect();
  tlsClient.stop();                      // free TLS context + lwIP socket
  TelemetryData d;
  telemetryRead(d);
  d.state = PST_DISCONNECTED;
  telemetryWrite(d);
  Serial.println(F("[MQTT] paused for display image (WiFi stays up)"));
}

void mqttResume() {
  if (!s_mqtt_paused) return;
  s_mqtt_paused = false;
  s_mqtt_last_attempt = 0;               // reconnect on the very next loop
  Serial.println(F("[MQTT] resume requested - reconnecting now"));
}

bool mqttIsPaused() { return s_mqtt_paused; }

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

static bool isPrinterReachable() {
  WiFiClient probe;
  probe.setTimeout(2);
  // 1.5s TCP probe to port 8883 - zero TLS allocation!
  bool ok = probe.connect(PRINTER_IP, MQTT_PORT, 1500);
  probe.stop(); // Always close probe socket immediately to prevent lwIP PCB descriptor leaks!
  return ok;
}

static void connectMQTT() {
  // If printer is turned off, skip expensive TLS completely!
  if (!isPrinterReachable()) {
    static uint32_t last_log_ms = 0;
    if (millis() - last_log_ms > 30000) {
      last_log_ms = millis();
      Serial.println(F("[MQTT] printer offline (port 8883 unreachable) - skipping TLS"));
    }
    TelemetryData d;
    telemetryRead(d);
    d.state = PST_DISCONNECTED;
    telemetryWrite(d);
    return;
  }

  char topic[80];
  snprintf(topic, sizeof(topic), "%s%s%s",
           MQTT_TOPIC_PREFIX, PRINTER_SN, MQTT_TOPIC_SUFFIX);

  char req_topic[80];
  snprintf(req_topic, sizeof(req_topic), "device/%s/request", PRINTER_SN);

  // Stop any previous hanging socket before starting a new connection
  tlsClient.stop();

  // Bambu broker uses a self-signed cert => setInsecure() skips CA
  // validation but keeps the TLS 1.2 tunnel encrypted.
  tlsClient.setInsecure();
  tlsClient.setHandshakeTimeout(10); // 10 seconds handshake timeout
  mqtt.setServer(PRINTER_IP, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(MQTT_MAX_PACKET_SIZE); // payloads exceed 2 KB
  mqtt.setSocketTimeout(10);                // 10 seconds socket timeout

  Serial.printf("[MQTT] connecting TLS to %s:%d ... (free=%u, 8bit_free=%u, max_block=%u)\n",
                PRINTER_IP, MQTT_PORT, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  uint32_t t_start = millis();
  bool conn = mqtt.connect("bambu-dash", MQTT_USER, PRINTER_LAN_CODE);
  Serial.printf("[MQTT] connect returned %s in %u ms (rc=%d)\n",
                conn ? "OK" : "FAIL", (unsigned)(millis() - t_start), mqtt.state());

  if (conn) {
    network_ever_connected = true;
    Serial.printf("[MQTT] connected, subscribing to %s\n", topic);
    mqtt.subscribe(topic, 1);
    // Request full initial telemetry payload immediately
    const char* pushall_cmd = "{\"pushing\":{\"sequence_id\":\"1\",\"command\":\"pushall\"}}";
    mqtt.publish(req_topic, pushall_cmd);
    Serial.println(F("[MQTT] sent pushall command to printer"));
  } else {
    Serial.printf("[MQTT] connect failed, rc=%d\n", mqtt.state());
    tlsClient.stop(); // CRITICAL: Release lwIP socket descriptor & free SSL context!
    TelemetryData d;
    telemetryRead(d);
    d.state = PST_DISCONNECTED;  // Accurate OFFLINE state when printer is off
    telemetryWrite(d);
  }
}

// ---------------- Task body -------------------------------------------
static void mqttTask(void* /*param*/) {
  static uint32_t last_wifi_attempt = 0;
  s_mqtt_last_attempt = 0;

  // fast-start: begin immediately, retry aggressively (1.2 s) until the
  // first association, then back off to 5 s cycles for stability.
  WiFi.mode(WIFI_STA);            // STA only - BT off, AP off
  WiFi.setSleep(false);           // Keep radio awake: phone and web always connect instantly
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // attempt #1 at t=0
  last_wifi_attempt = millis();

  while (true) {
    // --- WiFi (re)connection ---
    if (WiFi.status() != WL_CONNECTED) {
      uint32_t retry_ms = 8000; // 8 seconds gives router plenty of time for 4-way handshake & DHCP
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
      s_mqtt_last_attempt = 0; // Trigger immediate MQTT connection attempt!
      WiFi.setSleep(false);
      esp_wifi_set_ps(WIFI_PS_NONE);
      Serial.printf("[WiFi] connected, IP: %s (Radio sleep disabled)\n",
                    WiFi.localIP().toString().c_str());
      configTime(5 * 3600 + 1800, 0, "pool.ntp.org", "time.nist.gov");
      Serial.println(F("[NTP] configured (IST)"));
    }

    // --- MQTT (re)connection ---
    if (s_mqtt_paused) {
      // image mode: WiFi stays up, MQTT stays down, no retry churn
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!mqtt.connected()) {
      TelemetryData d;
      telemetryRead(d);
      if (d.state != PST_DISCONNECTED) {
        d.state = PST_DISCONNECTED;
        telemetryWrite(d);
      }
      uint32_t retry_interval = network_ever_connected ? 4000 : 5000;
      if (millis() - s_mqtt_last_attempt > retry_interval) {
        s_mqtt_last_attempt = millis();
        connectMQTT();
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // --- connected: pump client & check for silent telemetry stalls ---
    mqtt.loop();

    static uint32_t last_pushall_ms = 0;
    TelemetryData td;
    telemetryRead(td);

    // If no telemetry update received for >12s, refresh by sending pushall
    if (millis() - td.last_update_ms > 12000 && (millis() - last_pushall_ms > 10000)) {
      last_pushall_ms = millis();
      char req_topic[80];
      snprintf(req_topic, sizeof(req_topic), "device/%s/request", PRINTER_SN);
      mqtt.publish(req_topic, "{\"pushing\":{\"sequence_id\":\"1\",\"command\":\"pushall\"}}");
    }

    if (td.last_update_ms > 0 && (millis() - td.last_update_ms > 40000)) {
      Serial.println(F("[MQTT] telemetry stalled >40s, resetting connection..."));
      mqtt.disconnect();
      tlsClient.stop();
      td.state = PST_DISCONNECTED;
      telemetryWrite(td);
      s_mqtt_last_attempt = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void networkTaskStart() {
  xTaskCreatePinnedToCore(
      mqttTask, "mqttTask", TASK_STACK_NET, nullptr, 1, nullptr, 0 /* Core 0 */);
}
