#include "spotify_service.h"
#include "settings_manager.h"
#include "ui.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <JPEGDEC.h>

SpotifyTrackInfo g_spotify;

static char s_refresh_token[160] = {0};
static char s_access_token[256] = {0};
static uint32_t s_token_expires_ms = 0;
static uint32_t s_last_poll_ms = 0;

static const char* SPOTIFY_CLIENT_ID = "bc80d4154155425a939097ea6079c225";
static const char* SPOTIFY_CLIENT_SECRET = "";

static bool s_has_cover_art = false;

// ---------------- Task 4: autonomous cover art (handover) --------------
// The decoder is heap-allocated per fetch (new/delete) instead of a static
// class instance: JPEGIMAGE weighs ~16 KB of .bss which would permanently
// cut the free heap the Bambu TLS connection depends on.
static uint16_t s_last_cover_key = 0;   // hash of last successfully fetched art URL

static uint16_t urlHash(const char* s) {
  uint16_t h = 5381;
  while (*s) h = (uint16_t)((h << 1) + (uint8_t)*s++);
  return h;
}

// JPEG draw callback: paste each MCU block into the 160x128 RGB565 cover
// buffer with clipping (decoded image may be offset/overflow the screen).
// Pixel type defaults to little endian = LovyanGFX native byte order; if
// album colors render wrong on the panel, call setPixelType(RGB565_BIG_ENDIAN).
static int jpegDrawCb(JPEGDRAW* p) {
  uint16_t* dst = (uint16_t*)uiGetCustomImageBuffer();
  if (!dst) return 0;
  for (int row = 0; row < p->iHeight; row++) {
    int dy = p->y + row;
    if (dy < 0 || dy >= 128) continue;
    const uint16_t* src = p->pPixels + row * p->iWidth;
    for (int col = 0; col < p->iWidth; col++) {
      int dx = p->x + col;
      if (dx < 0 || dx >= 160) continue;
      dst[dy * 160 + dx] = src[col];
    }
  }
  return 1;
}

static bool fetchCoverArt(const char* url) {
  // Heap guard: never risk the persistent Bambu TLS connection for art.
  // Steady-state is ~80 KB; the fetch peaks at ~50 KB transient (TLS
  // handshake + 300px JPEG), so 75 KB keeps a comfortable margin.
  if (ESP.getFreeHeap() < 75000) {
    Serial.printf("[SPOTIFY] skip cover fetch (heap %u too low)\n",
                  (unsigned)ESP.getFreeHeap());
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(6000);
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  int len = http.getSize();
  if (len <= 0 || len > 65536) { http.end(); return false; }

  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) { http.end(); return false; }
  size_t got = 0;
  WiFiClient* stream = http.getStreamPtr();
  uint32_t t0 = millis();
  while (got < (size_t)len && millis() - t0 < 8000) {
    size_t avail = stream->available();
    if (avail) {
      size_t want = (avail < (size_t)(len - got)) ? avail : (size_t)(len - got);
      int n = stream->readBytes(buf + got, want);
      if (n <= 0) break;
      got += n;
    } else {
      vTaskDelay(1);
    }
  }
  http.end();                          // TLS context freed BEFORE decode
  if (got != (size_t)len) { free(buf); return false; }

  bool ok = false;
  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (jpeg) {
    if (jpeg->openRAM(buf, len, jpegDrawCb) == 1) {
      int w = jpeg->getWidth(), h = jpeg->getHeight();
      // scale via decode() options (JPEGDEC 1.x API): smallest divisor so
      // the width fits the 160px panel
      int opt = 0, div = 1;            // 0=1/1, 2=1/2, 4=1/4, 8=1/8
      if (w > 160) { opt = JPEG_SCALE_HALF;    div = 2; }
      if (w > 320) { opt = JPEG_SCALE_QUARTER; div = 4; }
      if (w > 640) { opt = JPEG_SCALE_EIGHTH;  div = 8; }
      int sw = w / div, sh = h / div;
      int ox = (160 - sw) / 2, oy = (128 - sh) / 2;   // center; cb clips
      spotifySetCoverArtValid(false);  // hide stale art while re-decoding
      if (jpeg->decode(ox, oy, opt) == 1) ok = true;
      jpeg->close();
    }
    delete jpeg;
  }
  free(buf);
  if (ok) spotifySetCoverArtValid(true);
  return ok;
}

void spotifySetCoverArt(const uint16_t* pixels160x128) {
  if (pixels160x128) {
    memcpy(uiGetCustomImageBuffer(), pixels160x128, 160 * 128 * 2);
    s_has_cover_art = true;
  }
}

bool spotifyHasCoverArt() { return s_has_cover_art; }
const uint16_t* spotifyGetCoverArt() { return uiGetCustomImageBuffer(); }

uint8_t* spotifyGetCoverArtBuffer() {
  return (uint8_t*)uiGetCustomImageBuffer();
}

void spotifySetCoverArtValid(bool valid) {
  s_has_cover_art = valid;
}

void spotifyClearCoverArt() {
  s_has_cover_art = false;
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
  // handover Task 2 wanted setBufferSizes(2048,1024) but the pinned core
  // (arduino-esp32 2.0.17) does not expose it; the 75 KB heap guard in
  // fetchCoverArt + sequential connections cover the same hazard.
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
  // Adaptive cadence (handover Task 2): 3s while playing (responsive
  // track changes), 5s when idle (less radio + heap churn)
  uint32_t interval = g_spotify.is_playing ? 3000 : 5000;
  if (now - s_last_poll_ms < interval) return;
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
      // album art: pick the smallest image >= 160px wide (typically 300px)
      const char* art = "";
      int best_w = INT32_MAX;
      JsonArray imgs = item["album"]["images"];
      if (!imgs.isNull()) {
        for (JsonObject im : imgs) {
          int iw = im["width"] | 0;
          const char* iu = im["url"] | "";
          if (iw >= 160 && iw < best_w && iu[0]) { best_w = iw; art = iu; }
        }
      }
      spotifySetTrackInfo(name, artist, prog, dur, is_playing, art);

      // fetch + decode art only when the track actually changed
      if (is_playing && g_spotify.image_url[0]) {
        uint16_t key = urlHash(g_spotify.image_url);
        if (key != s_last_cover_key && fetchCoverArt(g_spotify.image_url)) {
          s_last_cover_key = key;
          Serial.println(F("[SPOTIFY] cover art decoded"));
        }
      }
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
