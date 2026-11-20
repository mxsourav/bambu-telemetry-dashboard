#pragma once
#include <Arduino.h>

struct SpotifyTrackInfo {
  bool     is_playing       = false;
  char     title[96]        = {0};
  char     artist[64]       = {0};
  uint32_t progress_ms      = 0;
  uint32_t duration_ms      = 0;
  char     image_url[128]   = {0};
  char     genre[16]        = {0};   // artist genre from Spotify API ("drill",
                                      // "hip hop"...) -> desk pet dance style
  char     lyric_cur[80]    = {0};
  char     lyric_next[80]   = {0};
  uint32_t last_update_ms   = 0;
};

extern SpotifyTrackInfo g_spotify;

void spotifyServiceInit();
void spotifySetTrackInfo(const char* title, const char* artist, uint32_t prog_ms, uint32_t dur_ms, bool playing, const char* img_url = nullptr, const char* lyric_cur = nullptr, const char* lyric_next = nullptr);
bool spotifyIsActive();
void spotifySetRefreshToken(const char* token);
const char* spotifyGetRefreshToken();
void spotifyPollLoop();
void spotifySetEnabled(bool enabled);
bool spotifyIsEnabled();
void spotifySetCoverArt(const uint16_t* pixels160x128);
bool spotifyHasCoverArt();
const uint16_t* spotifyGetCoverArt();
uint8_t* spotifyGetCoverArtBuffer();
void spotifySetCoverArtSize(size_t len);
size_t spotifyGetCoverArtSize();
const uint8_t* spotifyGetCoverArtData(size_t& out_len);
void spotifySetCoverArtValid(bool valid);
void spotifyClearCoverArt();

// ---- on-device pairing (no PC scripts) ----
// Builds the Spotify authorize URL the browser should open
void spotifyBuildAuthUrl(char* buf, size_t len);
// Exchanges an authorization code for permanent tokens (persists NVS+SD)
bool spotifyExchangeCode(const char* code);
// Diagnostic: stage a JPEG (URL or SD path) into the SD art cache
bool spotifyTestCoverArt(const char* url);

// ---- cover art render pipeline (SD-cached, zero big heap blocks) ----
bool spotifyArtDirty();                  // new art staged, needs UI render
void spotifySetArtDirty();
void spotifyClearArtDirty();
// Decode /art.jpg into a 160-stride RGB565 framebuffer (UI task context)
bool spotifyRenderArtToBuffer(uint16_t* dst160);
