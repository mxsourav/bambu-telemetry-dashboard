#include "spotify_service.h"
#include "settings_manager.h"
#include "ui.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

SpotifyTrackInfo g_spotify;

static char s_refresh_token[160] = {0};
static char s_access_token[256] = {0};
static uint32_t s_token_expires_ms = 0;
static uint32_t s_last_poll_ms = 0;

static const char* SPOTIFY_CLIENT_ID = "bc80d4154155425a939097ea6079c225";
static const char* SPOTIFY_CLIENT_SECRET = "";

static uint16_t* s_cover_art = nullptr;
static bool s_has_cover_art = false;

void spotifySetCoverArt(const uint16_t* pixels160x128) {
  if (!s_cover_art) {
    s_cover_art = (uint16_t*)malloc(160 * 128 * 2);
  }
  if (s_cover_art && pixels160x128) {
    memcpy(s_cover_art, pixels160x128, 160 * 128 * 2);
    s_has_cover_art = true;
  }
}

bool spotifyHasCoverArt() { return s_has_cover_art && (s_cover_art != nullptr); }
const uint16_t* spotifyGetCoverArt() { return s_cover_art; }

uint8_t* spotifyGetCoverArtBuffer() {
  if (!s_cover_art) {
    s_cover_art = (uint16_t*)malloc(160 * 128 * 2);
  }
  return (uint8_t*)s_cover_art;
}

void spotifySetCoverArtValid(bool valid) {
  s_has_cover_art = valid && (s_cover_art != nullptr);
}

void spotifyClearCoverArt() {
  s_has_cover_art = false;
  if (s_cover_art) {
    free(s_cover_art);
    s_cover_art = nullptr;
  }
}

void spotifyServiceInit() {
  strncpy(s_refresh_token, g_settings.spotify_refresh_token, sizeof(s_refresh_token) - 1);
}

void spotifySetTrackInfo(const char* title, const char* artist, uint32_t prog_ms, uint32_t dur_ms, bool playing, const char* img_url, const char* lyric_cur, const char* lyric_next) {
  g_spotify.is_playing = playing;
  if (title) strncpy(g_spotify.title, title, sizeof(g_spotify.title) - 1);
  if (artist) strncpy(g_spotify.artist, artist, sizeof(g_spotify.artist) - 1);
  g_spotify.progress_ms = prog_ms;
  g_spotify.duration_ms = dur_ms > 0 ? dur_ms : 1;
  if (img_url) strncpy(g_spotify.image_url, img_url, sizeof(g_spotify.image_url) - 1);
  if (lyric_cur) strncpy(g_spotify.lyric_cur, lyric_cur, sizeof(g_spotify.lyric_cur) - 1);
  if (lyric_next) strncpy(g_spotify.lyric_next, lyric_next, sizeof(g_spotify.lyric_next) - 1);
  g_spotify.last_update_ms = millis();
}

bool spotifyIsActive() {
  if (!g_spotify.is_playing) return false;
  // Active if updated in the last 15 seconds
  return (millis() - g_spotify.last_update_ms < 15000);
}

void spotifySetRefreshToken(const char* token) {
  if (!token) return;
  strncpy(s_refresh_token, token, sizeof(s_refresh_token) - 1);
  strncpy(g_settings.spotify_refresh_token, token, sizeof(g_settings.spotify_refresh_token) - 1);
  settingsSave();
  s_access_token[0] = 0; // force token refresh
}

const char* spotifyGetRefreshToken() {
  return s_refresh_token;
}

static bool refreshAccessToken() {
  if (!s_refresh_token[0] || WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4000);

  HTTPClient http;
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) {
    return false;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String body = "grant_type=refresh_token&refresh_token=" + String(s_refresh_token) +
                "&client_id=" + String(SPOTIFY_CLIENT_ID);
  if (SPOTIFY_CLIENT_SECRET && SPOTIFY_CLIENT_SECRET[0]) {
    body += "&client_secret=" + String(SPOTIFY_CLIENT_SECRET);
  }

  int code = http.POST(body);
  if (code == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    const char* at = doc["access_token"] | "";
    if (at && at[0]) {
      strncpy(s_access_token, at, sizeof(s_access_token) - 1);
      uint32_t expSec = doc["expires_in"] | 3600;
      s_token_expires_ms = millis() + (expSec * 1000);
      http.end();
      client.stop();
      return true;
    }
  }
  http.end();
  client.stop();
  return false;
}

void spotifyPollLoop() {
  if (!s_refresh_token[0] || WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if (now - s_last_poll_ms < 3000) return;
  s_last_poll_ms = now;

  if (!s_access_token[0] || now >= s_token_expires_ms - 60000) {
    if (!refreshAccessToken()) {
      s_last_poll_ms = now + 60000; // Backoff 60s on token refresh failure to keep web server fast
      return;
    }
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(3000);

  HTTPClient http;
  if (!http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing")) {
    return;
  }

  http.addHeader("Authorization", String("Bearer ") + s_access_token);
  int code = http.GET();

  if (code == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    bool is_playing = doc["is_playing"] | false;
    uint32_t prog = doc["progress_ms"] | 0;
    JsonObject item = doc["item"];
    if (!item.isNull()) {
      const char* name = item["name"] | "";
      uint32_t dur = item["duration_ms"] | 1;
      const char* artist = item["artists"][0]["name"] | "";
      spotifySetTrackInfo(name, artist, prog, dur, is_playing);
    }
  } else if (code == 204) {
    g_spotify.is_playing = false;
  } else if (code == 401) {
    s_access_token[0] = 0; // force token refresh on next cycle
    s_last_poll_ms = now + 15000; // Backoff 15s on auth expiry
  }
  http.end();
  client.stop();
}
