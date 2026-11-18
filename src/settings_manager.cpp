#include "settings_manager.h"
#include "peripherals.h"
#include "ui.h"

#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>

SystemSettings g_settings;
static Preferences prefs;
static const char* SETTINGS_FILE = "/settings.json";

void settingsInit() {
  prefs.begin("stan_cfg", false);
  settingsLoad();
}

void settingsLoad() {
  bool loaded_from_sd = false;

  // 1. Try reading from SD card if mounted
  if (SD.cardType() != CARD_NONE && SD.exists(SETTINGS_FILE)) {
    File f = SD.open(SETTINGS_FILE, FILE_READ);
    if (f) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err) {
        g_settings.theme_light  = doc["theme_light"] | false;
        g_settings.clock_12h    = doc["clock_12h"] | false;
        g_settings.buzzer_mode  = doc["buzzer_mode"] | 0;
        g_settings.brightness   = doc["brightness"] | 100;
        g_settings.led_override = doc["led_override"] | 0;
        g_settings.led_r        = doc["led_r"] | 255;
        g_settings.led_g        = doc["led_g"] | 255;
        g_settings.led_b        = doc["led_b"] | 255;
        const char* srt = doc["spotify_refresh_token"] | "";
        if (srt && srt[0]) strncpy(g_settings.spotify_refresh_token, srt, sizeof(g_settings.spotify_refresh_token) - 1);
        loaded_from_sd = true;
        Serial.println(F("[SETTINGS] Loaded configuration from SD card (/settings.json)"));
      } else {
        Serial.printf("[SETTINGS] SD JSON parse error: %s\n", err.c_str());
      }
    }
  }

  // 2. If SD file wasn't present, fall back to NVS flash preferences
  if (!loaded_from_sd) {
    g_settings.theme_light  = prefs.getBool("theme_light", false);
    g_settings.clock_12h    = prefs.getBool("clock_12h", false);
    g_settings.buzzer_mode  = prefs.getUChar("buzzer_mode", 0);
    g_settings.brightness   = prefs.getUChar("brightness", 100);
    g_settings.led_override = prefs.getUChar("led_override", 0);
    g_settings.led_r        = prefs.getUChar("led_r", 255);
    g_settings.led_g        = prefs.getUChar("led_g", 255);
    g_settings.led_b        = prefs.getUChar("led_b", 255);
    String srtNvs = prefs.getString("sp_rtoken", "");
    if (srtNvs.length() > 0) strncpy(g_settings.spotify_refresh_token, srtNvs.c_str(), sizeof(g_settings.spotify_refresh_token) - 1);
    Serial.println(F("[SETTINGS] Loaded configuration from NVS Preferences"));
  }

  // Apply loaded settings to hardware subsystems
  setBuzzerMode(g_settings.buzzer_mode);
  uiSetBacklight(g_settings.brightness);
  ledSetOverride(g_settings.led_override);
  if (g_settings.led_override == 8) {
    ledSetCustomRgb(g_settings.led_r, g_settings.led_g, g_settings.led_b);
  }

  Serial.printf("[SETTINGS] Applied: Theme=%s | Clock=%s | Buzzer=%s | Brightness=%d%% | LED=%d\n",
                g_settings.theme_light ? "LIGHT" : "DARK",
                g_settings.clock_12h ? "12H" : "24H",
                g_settings.buzzer_mode == 0 ? "Passive" : (g_settings.buzzer_mode == 1 ? "Active" : "Mute"),
                g_settings.brightness,
                g_settings.led_override);
}

void settingsSave() {
  // 1. Save to NVS flash
  prefs.putBool("theme_light", g_settings.theme_light);
  prefs.putBool("clock_12h", g_settings.clock_12h);
  prefs.putUChar("buzzer_mode", g_settings.buzzer_mode);
  prefs.putUChar("brightness", g_settings.brightness);
  prefs.putUChar("led_override", g_settings.led_override);
  prefs.putUChar("led_r", g_settings.led_r);
  prefs.putUChar("led_g", g_settings.led_g);
  prefs.putUChar("led_b", g_settings.led_b);
  prefs.putString("sp_rtoken", g_settings.spotify_refresh_token);

  // 2. Save to SD card if present
  if (SD.cardType() != CARD_NONE) {
    File f = SD.open(SETTINGS_FILE, FILE_WRITE);
    if (f) {
      JsonDocument doc;
      doc["theme_light"]  = g_settings.theme_light;
      doc["clock_12h"]    = g_settings.clock_12h;
      doc["buzzer_mode"]  = g_settings.buzzer_mode;
      doc["brightness"]   = g_settings.brightness;
      doc["led_override"] = g_settings.led_override;
      doc["led_r"]        = g_settings.led_r;
      doc["led_g"]        = g_settings.led_g;
      doc["led_b"]        = g_settings.led_b;
      if (g_settings.spotify_refresh_token[0]) {
        doc["spotify_refresh_token"] = g_settings.spotify_refresh_token;
      }
      serializeJsonPretty(doc, f);
      f.close();
      Serial.println(F("[SETTINGS] Configuration successfully written to SD card (/settings.json)"));
    } else {
      Serial.println(F("[SETTINGS] Warning: Failed to open /settings.json on SD for writing"));
    }
  }

  Serial.println(F("[SETTINGS] Configuration saved to NVS and SD."));
}
