#pragma once
#include <Arduino.h>

struct SpotifyTrackInfo {
  bool     is_playing       = false;
  char     title[64]        = {0};
  char     artist[48]       = {0};
  uint32_t progress_ms      = 0;
  uint32_t duration_ms      = 0;
  char     image_url[128]   = {0};
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
void spotifySetCoverArt(const uint16_t* pixels160x128);
bool spotifyHasCoverArt();
const uint16_t* spotifyGetCoverArt();
uint8_t* spotifyGetCoverArtBuffer();
void spotifySetCoverArtValid(bool valid);
void spotifyClearCoverArt();
