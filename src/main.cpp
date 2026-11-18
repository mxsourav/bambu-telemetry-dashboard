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
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "telemetry.h"
#include "network.h"
#include "ui.h"
#include "peripherals.h"
#include "settings_manager.h"
#include "web_server.h"

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void ledcSetupCompat(int /*ch*/, uint32_t freq, int bits) {
  ledcAttachChannel(PIN_BUZZER, freq, 10, BUZZER_LEDC_CH);  // buzzer
  ledcAttachChannel(PIN_LED_R, LED_PWM_FREQ, LED_PWM_BITS, LEDC_CH_R);
  ledcAttachChannel(PIN_LED_G, LED_PWM_FREQ, LED_PWM_BITS, LEDC_CH_G);
  ledcAttachChannel(PIN_LED_B, LED_PWM_FREQ, LED_PWM_BITS, LEDC_CH_B);
}
static void ledcSetupBacklight(int bits) {
  ledcAttach(PIN_TFT_BL, 5000, bits);                       // backlight
  ledcWrite(PIN_TFT_BL, 0);
}
#else
static void ledcSetupCompat(int ch, uint32_t freq, int bits) {
  ledcSetup(ch, freq, 10);             // ch1 = buzzer
  ledcAttachPin(PIN_BUZZER, ch);
  ledcSetup(LEDC_CH_R, LED_PWM_FREQ, LED_PWM_BITS);  // ch2/3/4 = strip
  ledcAttachPin(PIN_LED_R, LEDC_CH_R);
  ledcSetup(LEDC_CH_G, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_G, LEDC_CH_G);
  ledcSetup(LEDC_CH_B, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_B, LEDC_CH_B);
}
static void ledcSetupBacklight(int bits) {
  ledcSetup(0, 5000, bits);            // ch0 = backlight
  ledcAttachPin(PIN_TFT_BL, 0);
  ledcWrite(0, 0);
}
#endif

// Static SPI object for SD card (must not be destroyed on stack)
static SPIClass sdSpi(HSPI);
uint32_t g_sd_total_mb = 0;
uint32_t g_sd_used_mb = 0;
bool g_sd_mounted = false;

static bool tryMountSD(int sck, int miso, int mosi, int cs, uint32_t freq) {
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  pinMode(miso, INPUT_PULLUP);
  sdSpi.begin(sck, miso, mosi, cs);
  return SD.begin(cs, sdSpi, freq);
}

bool sdEnsureMounted() {
  if (SD.cardType() != CARD_NONE) return true;
  return tryMountSD(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, 4000000);
}

static void checkSDCard() {
  Serial.println(F("[SD] --- Diagnostic Check ---"));
  // Read raw pin states before init (with internal pullups enabled)
  pinMode(PIN_SD_CS, INPUT_PULLUP);
  pinMode(PIN_SD_SCK, INPUT_PULLUP);
  pinMode(PIN_SD_MOSI, INPUT_PULLUP);
  pinMode(PIN_SD_MISO, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  delay(5);
  Serial.printf("[SD_PINS] CS(G%d)=%d SCK(G%d)=%d MOSI(G%d)=%d MISO(G%d)=%d | Alt MISO(G12)=%d\n",
                PIN_SD_CS, digitalRead(PIN_SD_CS),
                PIN_SD_SCK, digitalRead(PIN_SD_SCK),
                PIN_SD_MOSI, digitalRead(PIN_SD_MOSI),
                PIN_SD_MISO, digitalRead(PIN_SD_MISO),
                digitalRead(12));

  bool ok = false;
  int used_miso = PIN_SD_MISO;

  // Attempt 1: Configured pins (CS=15, SCK=25, MOSI=13, MISO=26)
  Serial.printf("[SD] Probing CS=%d SCK=%d MOSI=%d MISO=%d @ 4MHz...\n",
                PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO);
  if (tryMountSD(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, 4000000)) {
    ok = true;
    used_miso = PIN_SD_MISO;
  } else {
    SD.end();
    sdSpi.end();
    // Attempt 2: Native HSPI MISO on GPIO 12
    Serial.printf("[SD] Probing CS=%d SCK=%d MOSI=%d MISO=12 @ 4MHz...\n",
                  PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI);
    if (tryMountSD(PIN_SD_SCK, 12, PIN_SD_MOSI, PIN_SD_CS, 4000000)) {
      ok = true;
      used_miso = 12;
    } else {
      SD.end();
      sdSpi.end();
      // Attempt 3: Lower frequency 1MHz on configured pins
      if (tryMountSD(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, 1000000)) {
        ok = true;
        used_miso = PIN_SD_MISO;
      }
    }
  }

  if (ok) {
    uint8_t cardType = SD.cardType();
    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC)  typeStr = "MMC";
    else if (cardType == CARD_SD)   typeStr = "SDSC";
    else if (cardType == CARD_SDHC) typeStr = "SDHC/SDXC";

    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes  = SD.usedBytes();
    uint64_t cardSize   = SD.cardSize();
    g_sd_mounted  = true;
    g_sd_total_mb = (uint32_t)(totalBytes / (1024 * 1024));
    g_sd_used_mb  = (uint32_t)(usedBytes / (1024 * 1024));

    Serial.println(F("[SD] ========================================="));
    Serial.println(F("[SD] >>> SD CARD IS WORKING! <<<"));
    Serial.printf("[SD] Mounted on CS=%d SCK=%d MOSI=%d MISO=%d\n",
                  PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI, used_miso);
    Serial.printf("[SD] Type: %s | Card Size: %llu MB\n",
                  typeStr, (unsigned long long)(cardSize / (1024 * 1024)));
    Serial.printf("[SD] FAT Total: %llu MB | Used: %llu MB\n",
                  (unsigned long long)(totalBytes / (1024 * 1024)),
                  (unsigned long long)(usedBytes / (1024 * 1024)));

    File root = SD.open("/");
    if (root) {
      Serial.println(F("[SD] Root directory contents:"));
      int count = 0;
      File file = root.openNextFile();
      while (file && count < 10) {
        if (file.isDirectory()) {
          Serial.printf("  [DIR]  %s\n", file.name());
        } else {
          Serial.printf("  [FILE] %s (%u bytes)\n", file.name(), file.size());
        }
        file = root.openNextFile();
        count++;
      }
      if (count == 0) Serial.println(F("  (card is empty)"));
      root.close();

      File testFile = SD.open("/test.txt");
      if (testFile) {
        Serial.println(F("[SD] --- Reading /test.txt ---"));
        while (testFile.available()) {
          Serial.write(testFile.read());
        }
        testFile.close();
        Serial.println(F("[SD] ---------------------------"));
      }
    }
    Serial.println(F("[SD] ========================================="));
  } else {
    Serial.println(F("[SD] ========================================="));
    Serial.println(F("[SD] >>> SD CARD NOT DETECTED / NOT WORKING <<<"));
    Serial.println(F("[SD] Probable causes:"));
    Serial.println(F("[SD] 1. No micro-SD card is inserted in the slot."));
    Serial.println(F("[SD] 2. Card is not formatted as FAT32 / FAT16."));
    Serial.println(F("[SD] 3. SPI wires (CS=15, SCK=25, MOSI=13, MISO=26/12) are not connected."));
    Serial.println(F("[SD] ========================================="));
  }
}

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

  // Button pins on GPIO 4 (Menu) and GPIO 16 (Select) with internal pullup
  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)PIN_BTN_MENU);
  gpio_pulldown_dis((gpio_num_t)PIN_BTN_MENU);
  gpio_pullup_en((gpio_num_t)PIN_BTN_SELECT);
  gpio_pulldown_dis((gpio_num_t)PIN_BTN_SELECT);
  delay(10);
  Serial.printf("[BTN_INIT] Menu(G%d)=%d Select(G%d)=%d\n",
                PIN_BTN_MENU, digitalRead(PIN_BTN_MENU),
                PIN_BTN_SELECT, digitalRead(PIN_BTN_SELECT));

  // LEDC: buzzer (ch1) + RGB strip (ch2/3/4)
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  ledcSetupCompat(BUZZER_LEDC_CH, BUZZER_FREQ_OK, 10);
  ledcWrite(LEDC_CH_R, 0);         // strip off until first MQTT state
  ledcWrite(LEDC_CH_G, 0);
  ledcWrite(LEDC_CH_B, 0);

  telemetryInit();

  // ---- SD card diagnostic & mount ----
  checkSDCard();

  // ---- Persistent Settings Manager (SD Card & NVS) ----
  settingsInit();

  // Note: GPIO 26 is dedicated to SD SPI MISO. Backlight is jumpered to 3.3V.

  // Start tasks: UI last so the sprite/mutex are guaranteed ready
  networkTaskStart();      // Core 0 (WiFi + MQTT)
  webServerTaskStart();    // Core 0 (STΔN studio Web Server & REST)
  peripheralsTaskStart();  // Core 1 (low prio)
  uiTaskStart();           // Core 1 (high prio)

  Serial.printf("[BOOT] tasks started | heap=%u\n", ESP.getFreeHeap());
}

void loop() {
  // all work happens in FreeRTOS tasks; loop() idles
  vTaskDelay(pdMS_TO_TICKS(1000));

  // watchdog diagnostics every 3 s
  static uint32_t last_diag = 0;
  if (millis() - last_diag >= 3000) {
    last_diag = millis();
    TelemetryData d;
    telemetryRead(d);
    Serial.printf("[DIAG] IP=%s state=%s pct=%d noz=%.1f/%.1f bed=%.1f/%.1f "
                  "hms=%d heap=%u | G4=%d G16=%d\n",
      WiFi.localIP().toString().c_str(),
      printStateName(d.state), d.percent,
      d.nozzle_temp, d.nozzle_target, d.bed_temp, d.bed_target,
      d.hms_count, ESP.getFreeHeap(),
      digitalRead(4), digitalRead(16));
  }
}
