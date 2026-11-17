/* =====================================================================
 *  main.cpp  -  Bambu Lab Telemetry Dashboard (ESP32 + ST7735 1.8")
 * =====================================================================
 *  Task layout (FreeRTOS, symmetric multiprocessing):
 *    Core 0: mqttTask     - WiFi + TLS MQTT + JSON parse  ( prio 1, 8 KB stack)
 *    Core 1: uiTask       - 60 FPS LovyanGFX DMA rendering (prio 2, 4 KB)
 *            periTask     - buttons + buzzer state machines (prio 1, 3 KB)
 *    Shared: g_telemetry guarded by telemetryMutex (priority inheritance)
 *
 *  LED strip (APA102, DATA=27 CLK=22) intentionally not yet driven -
 *  its state machine hooks live in peripherals.cpp, strip comes later.
 * ===================================================================== */
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "telemetry.h"
#include "network.h"
#include "ui.h"
#include "peripherals.h"

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void ledcSetupCompat(int /*ch*/, uint32_t freq, int bits) {
  // v3.x: pin-based API
  ledcAttach(PIN_TFT_BL, 5000, bits);                       // backlight
  ledcAttachChannel(PIN_BUZZER, freq, 10, BUZZER_LEDC_CH);  // buzzer
  ledcAttachChannel(PIN_LED_R, LED_PWM_FREQ, LED_PWM_BITS, LEDC_CH_R);
  ledcAttachChannel(PIN_LED_G, LED_PWM_FREQ, LED_PWM_BITS, LEDC_CH_G);
  ledcAttachChannel(PIN_LED_B, LED_PWM_FREQ, LED_PWM_BITS, LEDC_CH_B);
}
#else
static void ledcSetupCompat(int ch, uint32_t freq, int bits) {
  ledcSetup(0, 5000, bits);            // ch0 = backlight
  ledcAttachPin(PIN_TFT_BL, 0);
  ledcSetup(ch, freq, 10);             // ch1 = buzzer
  ledcAttachPin(PIN_BUZZER, ch);
  ledcSetup(LEDC_CH_R, LED_PWM_FREQ, LED_PWM_BITS);  // ch2/3/4 = strip
  ledcAttachPin(PIN_LED_R, LEDC_CH_R);
  ledcSetup(LEDC_CH_G, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_G, LEDC_CH_G);
  ledcSetup(LEDC_CH_B, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_B, LEDC_CH_B);
}
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("====================================="));
  Serial.println(F("  Bambu Telemetry Dashboard  boot"));
  Serial.println(F("====================================="));

  // ---- kill Bluetooth: on arduino-esp32 2.0.17 the BT controller is NOT
  // started by default in this configuration, so there is nothing to stop;
  // calling btStop() pre-init wedged boot (no splash, no tasks). The
  // class remains unused -> memory stays free. (Verified: heap 271 KB.)
  Serial.println(F("[PWR] Bluetooth unused (never started)"));

  // LEDC: backlight (ch0) + buzzer (ch1) + RGB strip (ch2/3/4)
  ledcSetupCompat(BUZZER_LEDC_CH, BUZZER_FREQ_OK, 10);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_BL, 0);        // start dark; UI task fades in
#else
  ledcWrite(0, 0);
#endif
  ledcWrite(LEDC_CH_R, 0);         // strip off until first MQTT state
  ledcWrite(LEDC_CH_G, 0);
  ledcWrite(LEDC_CH_B, 0);

  telemetryInit();

  // ---- SD card (HSPI: CS=15, SCK=25, MOSI=13, MISO=12) ----
  // Separate SPI host from the TFT (VSPI) - no bus contention.
  // Mounted best-effort; absence is not fatal (card is optional).
  {
    SPIClass sdSpi(HSPI);
    sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (SD.begin(PIN_SD_CS, sdSpi)) {
      Serial.printf("[SD] mounted OK (CS=%d SCK=%d MOSI=%d MISO=%d)\n",
                    PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO);
      Serial.printf("[SD] size: %llu MB\n",
                    (unsigned long long)(SD.cardSize() / (1024 * 1024)));
    } else {
      Serial.println(F("[SD] no card / mount failed - continuing without"));
    }
  }

  // Start tasks: UI last so the sprite/mutex are guaranteed ready
  networkTaskStart();      // Core 0
  peripheralsTaskStart();  // Core 1 (low prio)
  uiTaskStart();           // Core 1 (high prio)

  Serial.printf("[BOOT] tasks started | heap=%u\n", ESP.getFreeHeap());
}

void loop() {
  // all work happens in FreeRTOS tasks; loop() idles
  vTaskDelay(pdMS_TO_TICKS(1000));

  // lightweight watchdog diagnostics every ~5 s
  static uint32_t last_diag = 0;
  if (millis() - last_diag >= 5000) {
    last_diag = millis();
    TelemetryData d;
    telemetryRead(d);
    Serial.printf("[DIAG] state=%s pct=%d noz=%.1f/%.1f bed=%.1f/%.1f "
                  "hms=%d heap=%u\n",
      printStateName(d.state), d.percent,
      d.nozzle_temp, d.nozzle_target, d.bed_temp, d.bed_target,
      d.hms_count, ESP.getFreeHeap());
  }
}
