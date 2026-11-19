#include "spotify_service.h"
#include "settings_manager.h"
#include "ui.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>
#include <JPEGDEC.h>
#include "web_server.h"   // sdEnsureMounted()

SpotifyTrackInfo g_spotify;

static char s_refresh_token[160] = {0};
static char s_access_token[256] = {0};
static uint32_t s_token_expires_ms = 0;
static uint32_t s_last_poll_ms = 0;

static const char* SPOTIFY_CLIENT_ID = "bc80d4154155425a939097ea6079c225";
static const char* SPOTIFY_CLIENT_SECRET = "eb42da8944b74b64be17997b22b7ed42";
// Redirect registered in the Spotify dashboard (Vercel mirror of the web UI).
// The device-embedded pairing page + the Vercel page's auto-forward script
// make the whole flow phone-browser-only: no PC scripts, ever.
static const char* SPOTIFY_REDIRECT_URI = "https://web-tau-five-60.vercel.app/";
static const char* SPOTIFY_SCOPES =
    "user-read-currently-playing user-read-playback-state user-modify-playback-state";

static bool s_has_cover_art = false;

// ---------------- Task 4: autonomous cover art (handover) --------------
// ARCHITECTURE: the art JPEG is cached on the SD card (/art.jpg) and
// decoded straight into the UI sprite buffers on demand. A 40 KB RAM
// cover buffer is impossible on this firmware - runtime fragmentation
// (TLS/JSON/RLE churn) shrinks the largest contiguous block below 20 KB,
// and JPEGDEC reports "success" even when its draw callback aborts,
// which previously poisoned the art flag and crashed the UI task.
#define ART_CACHE_PATH "/art.jpg"
static uint16_t s_last_cover_key = 0;   // hash of last cached art URL
static volatile bool s_art_dirty = false; // new art cached, UI must render

static uint16_t urlHash(const char* s) {
  uint16_t h = 5381;
  while (*s) h = (uint16_t)((h << 1) + (uint8_t)*s++);
  return h;
}

// Centering offsets applied in the callback (JPEGDEC must never receive
// negative decode coordinates - that corrupts its internal state).
static int s_art_ox = 0, s_art_oy = 0;
static uint16_t* s_art_dst = nullptr;

// JPEG draw callback: paste each MCU block into the target framebuffer
// (sprite RAM, 160 px stride) with clipping. Pixel type defaults to
// little endian = LovyanGFX native RGB565 byte order.
static int jpegDrawCb(JPEGDRAW* p) {
  uint16_t* dst = s_art_dst;
  if (!dst) return 0;
  for (int row = 0; row < p->iHeight; row++) {
    int dy = p->y + row + s_art_oy;
    if (dy < 0 || dy >= 128) continue;
    const uint16_t* src = p->pPixels + row * p->iWidth;
    for (int col = 0; col < p->iWidth; col++) {
      int dx = p->x + col + s_art_ox;
      if (dx < 0 || dx >= 160) continue;
      dst[dy * 160 + dx] = src[col];
    }
  }
  return 1;
}

bool spotifyArtDirty() { return s_art_dirty; }
void spotifyClearArtDirty() { s_art_dirty = false; }

// Decode /art.jpg (SD) into a caller-supplied 160-stride framebuffer.
// Runs in the UI task; no big heap allocation - JPEGDEC reads the file
// incrementally through its 2 KB internal buffer.
bool spotifyRenderArtToBuffer(uint16_t* dst160) {
  if (!sdEnsureMounted() || !SD.exists(ART_CACHE_PATH)) return false;
  File f = SD.open(ART_CACHE_PATH, FILE_READ);
  if (!f) return false;

  bool ok = false;
  JPEGDEC* jpeg = new (std::nothrow) JPEGDEC();
  if (jpeg) {
    if (jpeg->open(f, jpegDrawCb) == 1) {
      jpeg->setPixelType(RGB565_LITTLE_ENDIAN); // Little-Endian matches LovyanGFX sprite buffer byte order
      int w = jpeg->getWidth(), h = jpeg->getHeight();
      int opt = 0, div = 1;            // 0=1/1, 2=1/2, 4=1/4, 8=1/8
      if (w > 160) { opt = JPEG_SCALE_HALF;    div = 2; }
      if (w > 320) { opt = JPEG_SCALE_QUARTER; div = 4; }
      if (w > 640) { opt = JPEG_SCALE_EIGHTH;  div = 8; }
      int sw = w / div, sh = h / div;
      s_art_ox = (160 - sw) / 2;
      s_art_oy = (128 - sh) / 2;       // may be negative; callback clips
      s_art_dst = dst160;
      ok = (jpeg->decode(0, 0, opt) == 1);
      s_art_dst = nullptr;
      jpeg->close();
    }
    delete jpeg;
  }
  f.close();
  return ok;
}

// Stream-download a JPEG (or copy an SD-local path) to the SD art cache.
// Chunked 1.5 KB buffer only - never needs a big contiguous heap block.
static bool cacheArtFromUrl(const char* url) {
  if (!sdEnsureMounted()) return false;
  if (strcmp(url, ART_CACHE_PATH) == 0) {
    s_art_dirty = true;
    return true;
  }
  SD.remove(ART_CACHE_PATH);
  File out = SD.open(ART_CACHE_PATH, FILE_WRITE);
  if (!out) return false;
  bool ok = false;
  if (url[0] == '/') {                       // local SD path (diagnostics)
    File in = SD.open(url, FILE_READ);
    if (in) {
      uint8_t chunk[1536];
      size_t n;
      ok = true;
      while ((n = in.read(chunk, sizeof(chunk))) > 0)
        if (out.write(chunk, n) != n) { ok = false; break; }
      in.close();
    }
  } else {                                   // https (i.scdn.co album art)
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(6000);
    HTTPClient http;
    if (http.begin(client, url)) {
      int code = http.GET();
      if (code == 200) {
        WiFiClient* stream = http.getStreamPtr();
        uint8_t chunk[1536];
        int len = http.getSize();
        uint32_t t0 = millis();
        ok = true;
        while (ok && (len < 0 || len > 0) && millis() - t0 < 8000) {
          size_t avail = stream->available();
          if (avail) {
            size_t want = sizeof(chunk);
            if (len > 0 && avail > (size_t)len) avail = len;
            if (avail > want) avail = want;
            int n = stream->readBytes(chunk, avail);
            if (n <= 0) { ok = false; break; }
            if (out.write(chunk, n) != n) { ok = false; break; }
            if (len > 0) len -= n;
          } else {
            vTaskDelay(1);
          }
        }
        if (len > 0) ok = false;             // incomplete download
      }
      http.end();
    }
  }
  out.close();
  if (ok) {
    s_art_dirty = true;                      // UI task renders on next frame
    Serial.println(F("[SPOTIFY] art cached to SD (/art.jpg)"));
  } else {
    SD.remove(ART_CACHE_PATH);
  }
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

// ---------------- on-device pairing (no PC scripts) --------------------
#include <mbedtls/base64.h>

static String b64encode(const String& in) {
  size_t olen = 0;
  size_t out_len = ((in.length() + 2) / 3) * 4 + 1;
  unsigned char* out = (unsigned char*)malloc(out_len);
  if (!out) return "";
  mbedtls_base64_encode(out, out_len, &olen,
                        (const unsigned char*)in.c_str(), in.length());
  String s((const char*)out, olen);
  free(out);
  return s;
}

static String urlEncode(const char* s) {
  String out;
  char hex[4];
  for (const char* p = s; *p; p++) {
    char c = *p;
    bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    if (safe) out += c;
    else { snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c); out += hex; }
  }
  return out;
}

void spotifyBuildAuthUrl(char* buf, size_t len) {
  snprintf(buf, len,
           "https://accounts.spotify.com/authorize?client_id=%s"
           "&response_type=code&redirect_uri=%s&scope=%s",
           SPOTIFY_CLIENT_ID,
           urlEncode(SPOTIFY_REDIRECT_URI).c_str(),
           urlEncode(SPOTIFY_SCOPES).c_str());
}

bool spotifyExchangeCode(const char* code) {
  if (!code || !code[0] || WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);
  HTTPClient http;
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) return false;

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization",
                 "Basic " + b64encode(String(SPOTIFY_CLIENT_ID) + ":" +
                                      SPOTIFY_CLIENT_SECRET));
  String body = "grant_type=authorization_code&code=" + urlEncode(code) +
                "&redirect_uri=" + urlEncode(SPOTIFY_REDIRECT_URI);
  int httpCode = http.POST(body);
  bool ok = false;
  if (httpCode == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getString());
    if (!err) {
      const char* rt = doc["refresh_token"] | "";
      const char* at = doc["access_token"] | "";
      if (rt[0]) {
        spotifySetRefreshToken(rt);       // persists NVS + SD, clears access token
        if (at[0]) {                      // cache the fresh access token too
          strncpy(s_access_token, at, sizeof(s_access_token) - 1);
          uint32_t expSec = doc["expires_in"] | 3600;
          s_token_expires_ms = millis() + (expSec * 1000);
        }
        Serial.println(F("[SPOTIFY] paired OK - refresh token saved (survives reboots)"));
        ok = true;
      }
    }
  } else {
    Serial.printf("[SPOTIFY] code exchange failed: HTTP %d\n", httpCode);
  }
  http.end();
  return ok;
}

bool spotifyTestCoverArt(const char* url) {
  // Stage the given JPEG (URL or SD path) into the art cache; the UI task
  // renders it into the sprites on the next Spotify frame.
  return cacheArtFromUrl(url);
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
  http.addHeader("Authorization",
                 "Basic " + b64encode(String(SPOTIFY_CLIENT_ID) + ":" +
                                      SPOTIFY_CLIENT_SECRET));
  
  String body = "grant_type=refresh_token&refresh_token=" + urlEncode(s_refresh_token) +
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
  } else {
    Serial.printf("[SPOTIFY] refresh token returned HTTP %d\n", code);
  }
  http.end();
  client.stop();
  return false;
}

static bool s_spotify_enabled = false; // default disabled on boot (Printer Mode is default)

void spotifySetEnabled(bool enabled) {
  s_spotify_enabled = enabled;
  if (enabled) {
    s_last_poll_ms = 0; // trigger immediate poll on enable
  }
  Serial.printf("[SPOTIFY] spotifySetEnabled(%s)\n", enabled ? "true" : "false");
}

bool spotifyIsEnabled() {
  return s_spotify_enabled;
}

void spotifyPollLoop() {
  if (!s_spotify_enabled) return;
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

      // cache art on SD when the track actually changed (UI renders it)
      if (is_playing && g_spotify.image_url[0]) {
        uint16_t key = urlHash(g_spotify.image_url);
        if (key != s_last_cover_key) {
          if (cacheArtFromUrl(g_spotify.image_url)) s_last_cover_key = key;
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
