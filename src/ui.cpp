/* =====================================================================
 *  ui.cpp  -  Apple dark-mode dashboard, 160x128 landscape (Core 1)
 * =====================================================================
 *  RENDER CONTRACT: draw into BACK sprite (RAM) -> if changed vs front,
 *  ONE full-frame DMA burst; if identical, ZERO SPI traffic.
 *
 *  Views:
 *   BOOT       STAN studio. splash (5 s, connects during it)
 *   HOME       status bar / arc gauge / metric cards / footer
 *   MENU       main list menu (LED, sound, brightness, clock, timer,
 *              reset, back) - table-style rows
 *   LEDLIST    LED control list (auto/off/rainbow/colors/back)
 *   CLOCK      full-screen clock (IST via NTP)
 *   TIMER      timer picker + countdown
 *   OVERLAY    latched PRINT COMPLETE / PRINT FAILED (Select = OK)
 *
 *  Buttons (from peripherals.cpp, single-fire, no ghost):
 *   1 menu press (move down)   2 select press (activate/OK)
 *   3 menu long (back one)     4 menu double (home)   5 select long (OK)
 * ===================================================================== */
#include "ui.h"
#include "config.h"
#include <WiFi.h>
#include "telemetry.h"
#include "peripherals.h"
#include "settings_manager.h"
#include "spotify_service.h"
#include "network.h"
#include "boot_splash.h"
#include "status_logo.h"
#include "desk_pet.h"
#include "pc_stats.h"
#include "pet_mood.h"

#include <LovyanGFX.hpp>
#include <SD.h>
#include "web_server.h"
#include <time.h>

volatile uint8_t ui_button_event = 0;
volatile uint32_t ui_button_hold_start_ms = 0;
volatile bool g_tft_spi_busy = false;

// ---------------- Display driver (ST7735S, landscape) -----------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel;
  lgfx::Bus_SPI       _bus;
public:
  LGFX() {
    auto bc = _bus.config();
    bc.spi_host = VSPI_HOST;
    bc.spi_mode = 0;
    bc.freq_write = TFT_SPI_MHZ * 1000000;
    bc.freq_read  = 16000000;
    bc.spi_3wire  = false;
    bc.use_lock   = true;
    bc.dma_channel = SPI_DMA_CH_AUTO;
    bc.pin_sclk = PIN_TFT_SCLK;
    bc.pin_mosi = PIN_TFT_MOSI;
    bc.pin_miso = -1;
    bc.pin_dc   = PIN_TFT_DC;
    _bus.config(bc);
    _panel.setBus(&_bus);

    auto pc = _panel.config();
    pc.pin_cs   = PIN_TFT_CS;
    pc.pin_rst  = PIN_TFT_RST;
    pc.pin_busy = -1;
    pc.panel_width  = 128;
    pc.panel_height = 160;
    pc.memory_width = 128;
    pc.memory_height = 160;
    pc.offset_x = 0;  // BLACKTAB: colstart = 0, rowstart = 0 (clean edges, no top glitch)
    pc.offset_y = 0;
    pc.offset_rotation = 0;
    pc.dummy_read_pixel = 8;
    pc.dummy_read_bits  = 1;
    pc.readable  = false;
    pc.invert    = false;
    pc.rgb_order = 1;  // 1 = MAD_RGB (Physical panel native RGB order)
    pc.dlen_16bit = false;
    pc.bus_shared = false;
    _panel.config(pc);
    setPanel(&_panel);
  }
};

static LGFX tft;
static LGFX_Sprite sprite(&tft);
#define W 160
#define H 128
#define BOOT_MS 5000

// ---------------- Palette (Adaptive Dark / Light mode) ----------------
bool uiGetThemeLight() { return g_settings.theme_light; }
void uiSetThemeLight(bool light) {
  g_settings.theme_light = light;
  settingsSave();
}

bool uiGetClock12h() { return g_settings.clock_12h; }
void uiSetClock12h(bool is12h) {
  g_settings.clock_12h = is12h;
  settingsSave();
}

// Paper Ink E-Paper Palette for Light Mode (Kindle / Remarkable warm cream paper & carbon ink)
// Dark Mode stays as it is (OLED carbon black & vibrant HUD elements)
#define COL_BLACK     (uint16_t)(uiGetThemeLight() ? 0xF77D : 0x0000) // 0xF77D = RGB(246, 242, 234) Kindle Carta paper
#define COL_CARD      (uint16_t)(uiGetThemeLight() ? 0xEF59 : 0x18E3) // 0xEF59 = RGB(238, 234, 226) textured paper card
#define COL_SEP       (uint16_t)(uiGetThemeLight() ? 0xD69A : 0x1082) // 0xD69A = RGB(216, 212, 204) paper fiber border
#define COL_TXT       (uint16_t)(uiGetThemeLight() ? 0x1082 : 0xFFFF) // 0x1082 = RGB(16, 16, 18) matte carbon ink
#define COL_TXT_BLACK (uint16_t)(uiGetThemeLight() ? 0xF77D : 0x0000)
#define COL_MUTED     (uint16_t)(uiGetThemeLight() ? 0x632C : 0x6B4D) // 0x632C = charcoal pencil ink
#define COL_ONLINE    (uint16_t)0x07E0
#define COL_OFFLINE   (uint16_t)0xF800
#define COL_CONNECT   (uint16_t)0xFFE0
#define COL_CYAN      (uint16_t)(uiGetThemeLight() ? 0x11B3 : 0x04FF) // deep midnight navy ink in light mode
#define COL_AMBER     (uint16_t)(uiGetThemeLight() ? 0xB340 : 0xFD20) // warm terracotta ink in light mode
#define COL_SALMON    (uint16_t)(uiGetThemeLight() ? 0xB1E2 : 0xFB68)
#define COL_LBLUE     (uint16_t)(uiGetThemeLight() ? 0x2213 : 0x7DDF)
#define COL_YELLOW    (uint16_t)0xFF00   // paused overlay
#define COL_RED       (uint16_t)0xF800
#define COL_SPOTIFY_GREEN (uint16_t)0x1DCA

// ---------------- Connection state ------------------------------------
enum ConnState : uint8_t { CONN_OFFLINE = 0, CONN_CONNECTING, CONN_ONLINE };
static ConnState connStateOf(const TelemetryData& d) {
  if (d.state == PST_DISCONNECTED || (d.last_update_ms > 0 && millis() - d.last_update_ms > 15000)) {
    extern bool network_ever_connected;
    return network_ever_connected ? CONN_CONNECTING : CONN_OFFLINE;
  }
  return CONN_ONLINE;
}

// ---------------- View state ------------------------------------------
enum UiView : uint8_t { VIEW_BOOT = 0, VIEW_HOME, VIEW_MENU, VIEW_LEDLIST,
                        VIEW_CLOCK, VIEW_TIMER, VIEW_OVERLAY, VIEW_CUSTOM_IMAGE, VIEW_ABOUT,
                        VIEW_SPOTIFY, VIEW_SPOTIFY_LYRICS, VIEW_DESK_PET, VIEW_PC_HUD,
                        VIEW_WEATHER, VIEW_AI_STATS };
static UiView view = VIEW_BOOT;

static uint32_t s_last_user_activity_ms = 0;
static bool s_pet_print_woken = false;  // pet->HOME wake fired once per print
static bool s_pc_hud_latched = false;   // persistent latch: when true, never auto-timeout to pet
static int8_t menu_focus = 0;       // row in current list

bool uiIsPetMode() {
  return (view == VIEW_DESK_PET);
}

void uiActivateDeskPet() {
  s_pc_hud_latched = false;
  view = VIEW_DESK_PET;
  s_pet_print_woken = true; // User explicitly summoned the pet: lock wake-to-home
  deskPetSetMood(PET_MOOD_NEUTRAL);
  s_last_user_activity_ms = millis();
}

bool uiIsPcHud() {
  return (view == VIEW_PC_HUD);
}

void uiActivatePcHud() {
  view = VIEW_PC_HUD;
  s_last_user_activity_ms = millis();
}

bool uiIsPcHudLatched() {
  return s_pc_hud_latched;
}

void uiSetPcHudLatched(bool latched) {
  s_pc_hud_latched = latched;
}

void uiSwitchView(const char* name) {
  if (!name || !name[0]) return;
  s_last_user_activity_ms = millis();
  if (!strcmp(name, "pet") || !strcmp(name, "desk_pet")) {
    s_pc_hud_latched = false;
    uiActivateDeskPet();
  } else if (!strcmp(name, "printer") || !strcmp(name, "home")) {
    s_pc_hud_latched = false;
    uiSetAppMode(MODE_PRINTER);
    view = VIEW_HOME;
  } else if (!strcmp(name, "pc_hud") || !strcmp(name, "stats") || !strcmp(name, "vitals")) {
    s_pc_hud_latched = true;
    view = VIEW_PC_HUD;
  } else if (!strcmp(name, "weather")) {
    s_pc_hud_latched = false;
    view = VIEW_WEATHER;
  } else if (!strcmp(name, "ai") || !strcmp(name, "ai_stats") || !strcmp(name, "cortex")) {
    s_pc_hud_latched = false;
    view = VIEW_AI_STATS;
  } else if (!strcmp(name, "spotify")) {
    s_pc_hud_latched = false;
    uiSetAppMode(MODE_SPOTIFY);
    view = VIEW_SPOTIFY;
  } else if (!strcmp(name, "lyrics") || !strcmp(name, "spotify_lyrics")) {
    s_pc_hud_latched = false;
    uiSetAppMode(MODE_SPOTIFY);
    view = VIEW_SPOTIFY_LYRICS;
  } else if (!strcmp(name, "image") || !strcmp(name, "custom_image")) {
    s_pc_hud_latched = false;
    view = VIEW_CUSTOM_IMAGE;
  } else if (!strcmp(name, "clock")) {
    s_pc_hud_latched = false;
    view = VIEW_CLOCK;
  } else if (!strcmp(name, "timer")) {
    s_pc_hud_latched = false;
    view = VIEW_TIMER;
  } else if (!strcmp(name, "menu") || !strcmp(name, "settings")) {
    s_pc_hud_latched = false;
    view = VIEW_MENU;
    menu_focus = 0;
  } else if (!strcmp(name, "ledlist") || !strcmp(name, "led")) {
    s_pc_hud_latched = false;
    view = VIEW_LEDLIST;
    menu_focus = 0;
  } else if (!strcmp(name, "about")) {
    s_pc_hud_latched = false;
    view = VIEW_ABOUT;
  }
}

const char* uiGetCurrentViewName() {
  switch (view) {
    case VIEW_DESK_PET: return "pet";
    case VIEW_HOME: return "printer";
    case VIEW_PC_HUD: return "pc_hud";
    case VIEW_WEATHER: return "weather";
    case VIEW_AI_STATS: return "ai";
    case VIEW_SPOTIFY: return "spotify";
    case VIEW_SPOTIFY_LYRICS: return "lyrics";
    case VIEW_CUSTOM_IMAGE: return "image";
    case VIEW_CLOCK: return "clock";
    case VIEW_TIMER: return "timer";
    case VIEW_MENU: return "menu";
    case VIEW_LEDLIST: return "ledlist";
    case VIEW_ABOUT: return "about";
    default: return "home";
  }
}
static uint32_t boot_start_ms = 0;
static uint16_t* s_custom_image_buf = nullptr;
static uint8_t*  s_custom_jpeg_buf = nullptr;
static size_t    s_custom_jpeg_len = 0;
static bool      s_custom_jpeg_dirty = false;
static bool custom_image_active = false;
static bool custom_image_rendered = false;
static bool s_had_active_print = false;

// Live Notification Overlay state
static char notif_title[24] = {0};
static char notif_msg[48] = {0};
static uint32_t notif_until_ms = 0;

void uiShowNotification(const char* title, const char* msg, uint32_t duration_ms) {
  if (title) strncpy(notif_title, title, sizeof(notif_title) - 1);
  else notif_title[0] = 0;
  if (msg) strncpy(notif_msg, msg, sizeof(notif_msg) - 1);
  else notif_msg[0] = 0;
  notif_until_ms = millis() + (duration_ms > 0 ? duration_ms : 4000);
  startBeepPattern(1); // pleasant notification chime
}

static SemaphoreHandle_t s_fb_mutex = NULL;

void uiLockFramebuffer() {
  if (!s_fb_mutex) s_fb_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_fb_mutex, pdMS_TO_TICKS(100));
}

void uiUnlockFramebuffer() {
  if (s_fb_mutex) xSemaphoreGive(s_fb_mutex);
}

const uint16_t* uiGetFramebuffer() {
  return (const uint16_t*)sprite.getBuffer();
}
void uiGetFrameSize(int& w, int& h) { w = W; h = H; }

bool uiIsCustomImageActive() { return custom_image_active; }

uint16_t* uiGetCustomImageBuffer() {
  if (!s_custom_image_buf) {
    s_custom_image_buf = (uint16_t*)malloc(W * H * sizeof(uint16_t));
    if (s_custom_image_buf) {
      memset(s_custom_image_buf, 0, W * H * sizeof(uint16_t));
    }
  }
  return s_custom_image_buf;
}

uint8_t* uiGetCustomJpegBuffer(size_t max_len) {
  if (!s_custom_jpeg_buf) {
    s_custom_jpeg_buf = (uint8_t*)malloc(49152);
    if (s_custom_jpeg_buf) {
      memset(s_custom_jpeg_buf, 0, 49152);
    }
  }
  return s_custom_jpeg_buf;
}

void uiSetCustomJpeg(const uint8_t* data, size_t len) {
  if (!data || len == 0 || len > 49152) return;
  uint8_t* buf = uiGetCustomJpegBuffer(len);
  if (buf) {
    if (buf != data) memcpy(buf, data, len);
    s_custom_jpeg_len = len;
    s_custom_jpeg_dirty = true;
    custom_image_active = true;
    custom_image_rendered = false;
    view = VIEW_CUSTOM_IMAGE;
  }
}

void uiActivateCustomImage() {
  custom_image_active = true;
  custom_image_rendered = false;
  view = VIEW_CUSTOM_IMAGE;
}

void uiShowCustomImage(const uint16_t* pixels) {
  if (pixels) {
    uint16_t* dst = uiGetCustomImageBuffer();
    if (dst) {
      memcpy(dst, pixels, W * H * 2);
      uiActivateCustomImage();
    }
  }
}

void uiClearCustomImage() {
  custom_image_active = false;
  custom_image_rendered = false;
  s_custom_jpeg_dirty = false;
  s_custom_jpeg_len = 0;
  if (s_custom_jpeg_buf) {
    free(s_custom_jpeg_buf);
    s_custom_jpeg_buf = nullptr;
  }
  if (s_custom_image_buf) {
    free(s_custom_image_buf);
    s_custom_image_buf = nullptr;
  }
  if (view == VIEW_CUSTOM_IMAGE) view = VIEW_HOME;
}

static AppMode s_app_mode = MODE_PRINTER;

AppMode uiGetAppMode() {
  return s_app_mode;
}

void uiSetAppMode(AppMode mode) {
  s_app_mode = mode;
  if (mode == MODE_SPOTIFY) {
    Serial.println(F("[MODE] Active Mode -> SPOTIFY (Stopping MQTT TLS, starting Spotify)"));
    networkSetEnabled(false);
    spotifySetEnabled(true);
    view = VIEW_SPOTIFY;
  } else {
    Serial.println(F("[MODE] Active Mode -> 3D PRINTER (Stopping Spotify, starting MQTT TLS)"));
    spotifySetEnabled(false);
    networkSetEnabled(true);
    view = VIEW_HOME;
  }
}

void uiActivateSpotify() {
  custom_image_active = false;
  custom_image_rendered = false;
  uiSetAppMode(MODE_SPOTIFY);
}

// menu state
static int8_t timer_sel = 0;        // index into TIMER_OPTS
static int8_t reset_arm = 0;        // 0=normal 1=armed confirm
static const int8_t TIMER_OPTS[] = {5, 10, 15, 30, 60};
#define TIMER_OPT_COUNT 5
static uint32_t timer_end_ms = 0;   // 0 = timer off
static bool     timer_fired = false;

// ---------------- Button dispatch helpers -----------------------------
// list row counts
#define MENU_ROWS      14
#define LEDLIST_ROWS   10

static const char* MENU_LABELS[MENU_ROWS] = {
  "Desk Pet", "Pet Stats", "Pet Music", "Spotify Mode", "PC Vitals",
  "Brightness", "LED Control", "Sound", "Siren", "Clock",
  "Timer", "Theme", "About", "Back" };

static const char* LEDLIST_LABELS[LEDLIST_ROWS] = {
  "Auto-Sync", "Music Sync", "Rainbow", "OFF", "RED", "GREEN", "BLUE", "WHITE",
  "AMBER", "Back" };

static void applySoundToggle();

// ============================================================
//  BOOT SCREEN
// ============================================================
static void drawBootFrame(uint32_t ms) {
  sprite.fillSprite(COL_TXT);
  sprite.pushImage((W - STAN_W) / 2, 28, STAN_W, STAN_H, stan_bits);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.setTextColor(COL_TXT_BLACK, COL_TXT);
  int32_t word_w = sprite.textWidth("studio.");
  sprite.drawString("studio.", W / 2, 74);
  int32_t stem_w = sprite.textWidth("studio");
  sprite.fillCircle(W / 2 + stem_w - word_w / 2 + 4, 74, 2, 0xF800);

  sprite.setFont(&fonts::Font0);
  if (WiFi.isConnected()) {
    sprite.setTextColor(0x03E0, COL_TXT); // green
    sprite.drawString(WiFi.localIP().toString().c_str(), W / 2, 94);
  } else {
    sprite.setTextColor(0x8410, COL_TXT); // gray
    sprite.drawString("Connecting WiFi...", W / 2, 94);
  }

  if (ms > 400) {
    int pw = (int)((long)(W - 80) * (ms - 400) / (BOOT_MS - 600));
    if (pw > W - 80) pw = W - 80;
    if (pw > 0) sprite.fillRect(40, H - 14, pw, 2, 0xBDF7);
  }
}

// ============================================================
//  LIST RENDERER (shared by MENU + LEDLIST)
// ============================================================
// draws a list with focus bar; labelFn supplies per-row right-hand value
static void drawList(const char* title, const char* const* labels,
                     int rows, int focus,
                     const char* (*valueFn)(int), bool (*activeFn)(int)) {
  sprite.fillSprite(COL_BLACK);
  // header
  sprite.setFont(&fonts::Font2);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  sprite.drawString(title, 8, 5);
  sprite.setTextColor(COL_LBLUE, COL_BLACK);
  sprite.drawString("v", W - 14, 5);      // hint: menu btn cycles
  sprite.drawFastHLine(0, 18, W, COL_SEP);

  const int row_h = (112 - 20) / 6;       // 6 visible rows, 15px each
  int scroll = 0;
  if (focus >= 6) scroll = focus - 5;     // simple scroll window
  for (int r = 0; r < 6; r++) {
    int idx = scroll + r;
    if (idx >= rows) break;
    int y = 20 + r * row_h;
    bool focused = (idx == focus);
    if (focused) {
      sprite.fillRoundRect(3, y, W - 6, row_h - 1, 4, 0x2129);  // cyan-dark
      sprite.drawRect(3, y, W - 6, row_h - 1, COL_CYAN);
    }
    bool active = activeFn ? activeFn(idx) : false;
    sprite.setFont(&fonts::Font0);
    sprite.setTextDatum(textdatum_t::middle_left);
    sprite.setTextColor(active ? COL_CYAN : COL_TXT,
                        focused ? 0x2129 : COL_BLACK);
    sprite.drawString(labels[idx], 10, y + row_h / 2);
    // right-hand value/indicator
    const char* val = valueFn ? valueFn(idx) : nullptr;
    sprite.setTextDatum(textdatum_t::middle_right);
    if (val) {
      sprite.setTextColor(focused ? COL_CYAN : COL_MUTED,
                          focused ? 0x2129 : COL_BLACK);
      sprite.drawString(val, W - 10, y + row_h / 2);
    } else if (active) {
      sprite.setTextColor(COL_CYAN, focused ? 0x2129 : COL_BLACK);
      sprite.drawString("[x]", W - 10, y + row_h / 2);
    }
  }
  sprite.drawFastHLine(0, 112, W, COL_SEP);
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString(WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "STAN studio OS", W / 2, 119);
}

// ---------------- menu row values / active marks ----------------------
static bool timerRunning() { return timer_end_ms != 0; }

static const char* menuValue(int i) {
  static char buf[16];
  switch (i) {
    case 0: return "100 FPS";
    case 1: return g_settings.pet_show_stats ? "ON" : "OFF";
    case 2: return g_settings.pet_show_music ? "ON" : "OFF";
    case 3: return (s_app_mode == MODE_SPOTIFY) ? "Active" : "Switch ->";
    case 4: return pcStatsIsActive() ? "Live" : "Ready";
    case 5: {
      snprintf(buf, sizeof(buf), "%d%%", uiGetBacklight());
      return buf;
    }
    case 6: return ">";
    case 7: {
      uint8_t m = getBuzzerMode();
      return (m == 0) ? "Passive" : (m == 1) ? "Active" : "Off";
    }
    case 8:  // siren policy
      return g_settings.siren_mode ? "Till OK" : "Auto";
    case 9: {  // clock preview
      struct tm t;
      if (getLocalTime(&t, 0)) {
        if (uiGetClock12h()) {
          int h12 = t.tm_hour % 12;
          if (h12 == 0) h12 = 12;
          snprintf(buf, sizeof(buf), "%d:%02d %s", h12, t.tm_min, (t.tm_hour >= 12) ? "PM" : "AM");
        } else {
          snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        }
        return buf;
      }
      return "--:--";
    }
    case 10: {  // timer status
      if (!timerRunning()) return "Off";
      int left = (timer_end_ms - millis()) / 1000;
      snprintf(buf, sizeof(buf), "%dm%02ds", left / 60, left % 60);
      return buf;
    }
    case 11: return uiGetThemeLight() ? "Light" : "Dark";
    case 12: return ">";
    case 13: return "Exit";
    default: return nullptr;
  }
}

static bool menuActive(int i) {
  switch (i) {
    case 0: return (view == VIEW_DESK_PET);
    case 1: return g_settings.pet_show_stats != 0;
    case 2: return g_settings.pet_show_music != 0;
    case 3: return (s_app_mode == MODE_SPOTIFY);
    case 4: return pcStatsIsActive();
    case 7: return options_sound_on();
    case 8: return g_settings.siren_mode != 0;   // highlighted = till-OK
    case 10: return timerRunning();
    case 11: return uiGetThemeLight();
    default: return false;
  }
}

static const char* ledListValue(int i) {
  switch (i) {
    case 0: return "auto";
    case 1: return "beat";
    case 2: return "fx";
    default: return nullptr;
  }
}

static bool ledListActive(int i) {
  uint8_t ov = ledGetOverride();
  switch (i) {
    case 0: return ov == 0;
    case 1: return ov == 9;
    case 2: return ov == 2;
    case 3: return ov == 1;
    case 4: return ov == 3;
    case 5: return ov == 4;
    case 6: return ov == 5;
    case 7: return ov == 6;
    case 8: return ov == 7;
    default: return false;
  }
}

// ============================================================
//  HOME
// ============================================================
static void drawHomeFrame(const TelemetryData& d, uint32_t ms) {
  char buf[16];
  sprite.fillSprite(COL_BLACK);

  // ---- status bar (centerline y=9) ----
  bool wifi_ok = WiFi.isConnected();
  ConnState conn = connStateOf(d);
  for (int i = 0; i < 4; i++) {
    int bh = 2 + i * 2;
    uint16_t c = (wifi_ok && i < 3) ? COL_CYAN : COL_MUTED;
    sprite.fillRect(7 + i * 3, 9 + 4 - bh, 2, bh, c);
  }
  uint16_t st_col = (conn == CONN_ONLINE) ? COL_ONLINE
                    : (wifi_ok ? COL_CYAN : COL_OFFLINE);
  const char* st = (conn == CONN_ONLINE) ? printStateName(d.state)
                   : (wifi_ok ? "STANDBY" : "NO WIFI");
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(st_col, COL_BLACK);
  sprite.drawString(st, 24, 9);

  // Normal sans-serif non-bold brand with signature red dot
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_right);
  sprite.setTextColor(COL_RED, COL_BLACK);
  sprite.drawString(".", W - 8, 9);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  sprite.drawString("STAN studio", W - 14, 9);

  sprite.drawFastHLine(0, 18, W, COL_SEP);

  // ---- PAUSED overlay (non-latched): yellow full-screen ----
  if (d.state == PST_PAUSED) {
    sprite.fillSprite(0x4240);            // dark yellow tint
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setFont(&fonts::FreeSansBold12pt7b);
    sprite.setTextColor(COL_YELLOW, 0x4240);
    sprite.drawString("PAUSED", W / 2, H / 2 - 10);
    sprite.setFont(&fonts::Font0);
    sprite.setTextColor(COL_TXT, 0x4240);
    sprite.drawString("print paused by printer", W / 2, H / 2 + 14);
    sprite.setFont(&fonts::Font0);
    sprite.drawString("Printer Job Paused", W / 2, H - 20);
    return;                               // skip dashboard below
  }

  // ---- left progress column ----
  int cx = 40, cy = 65, r = 30;
  const float a0 = 135.0f, sweep = 270.0f;
  sprite.fillArc(cx, cy, r, r - 3, a0, a0 + sweep, COL_CARD);
  int pct = d.percent;
  if (pct > 0) {
    const int segs = 16;
    for (int i = 0; i < segs; i++) {
      float f0 = i / (float)segs, f1 = (i + 1) / (float)segs;
      if (f0 * 100 >= pct) break;
      float cl = (f0 + f1) / 2;
      uint8_t gg = (uint8_t)(0xD2 + (0x50 - 0xD2) * cl);
      uint8_t bb = (uint8_t)(0xFF + (0xD0 - 0xFF) * cl);
      sprite.fillArc(cx, cy, r, r - 3,
                     a0 + sweep * f0 * pct / 100.0f,
                     a0 + sweep * f1 * pct / 100.0f,
                     ((gg >> 2) << 5) | (bb >> 3));
    }
  }
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setFont(&fonts::FreeSansBold9pt7b);   // 12pt "100%" (61px) overflows r=30 ring
  sprite.setTextColor(COL_TXT, COL_BLACK);
  snprintf(buf, sizeof(buf), "%d%%", pct);
  sprite.drawString(buf, cx, cy - 8);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  // "PROGRESS" at cy+15 sat ON the ring band (text spans x16..64; the ring
  // crosses x29..34 at y91..99) - moved below the ring (bottom y=95)
  sprite.drawString("PROGRESS", cx, 104);

  // ---- right metric cards ----
  const char* tags[3]   = {"NOZ", "BED", "TIME"};
  const uint16_t tcol[3] = {COL_AMBER, COL_SALMON, COL_LBLUE};
  snprintf(buf, sizeof(buf), "%d/%d", (int)d.nozzle_temp, (int)d.nozzle_target);
  static char v0[14], v1[14], v2[14];
  strncpy(v0, buf, sizeof(v0));
  snprintf(v1, sizeof(v1), "%d/%d", (int)d.bed_temp, (int)d.bed_target);
  if (d.remain_min >= 0) snprintf(v2, sizeof(v2), "%dh%02dm",
                                  d.remain_min / 60, d.remain_min % 60);
  else                   snprintf(v2, sizeof(v2), "--");
  const char* vals[3] = {v0, v1, v2};

  for (int i = 0; i < 3; i++) {
    int y = 20 + i * 31;
    // Cards 29px tall (was 27): the 9pt value's glyph box top reaches
    // ~17px above its bottom-datum anchor, so 27px cards forced the value
    // into the tag row ("NOZ"/"TIM" were overwritten by the big digits).
    // bottom datum (150, y+27) => digit top ~y+11, tag caps end y+9 -> clear.
    sprite.fillRoundRect(80, y, 74, 29, 6, COL_CARD);
    sprite.setFont(&fonts::Font0);
    sprite.setTextDatum(textdatum_t::top_left);
    sprite.setTextColor(tcol[i], COL_CARD);
    sprite.drawString(tags[i], 84, y + 3);
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.setTextDatum(textdatum_t::bottom_right);
    sprite.setTextColor(COL_TXT, COL_CARD);
    sprite.drawString(vals[i], 150, y + 27);
  }

  sprite.drawFastHLine(0, 112, W, COL_SEP);

  // ---- footer: job + filament + timer + hint ----
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  // When the footer center is occupied (timer countdown or WiFi IP), the
  // job label must stay under 30px wide or it collides with the centered
  // text ("Printer Idle" ran x8..80 into the IP at x41..119).
  bool center_used = timerRunning() || wifi_ok;
  const char* job = (d.state == PST_RUNNING || d.state == PST_PREPARE)
                      ? (center_used ? "Print" : "Printing...")
                      : (center_used ? ((conn == CONN_ONLINE) ? "Idle" : "Off")
                                     : ((conn == CONN_ONLINE) ? "Printer Idle"
                                                              : "Printer Off"));
  sprite.drawString(job, 8, 120);

  // timer countdown (left-center of footer when armed) or IP address
  if (timerRunning()) {
    int left = (int)((timer_end_ms - ms) / 1000);
    if (left < 0) left = 0;
    snprintf(buf, sizeof(buf), "T-%dm%02ds", left / 60, left % 60);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setTextColor(timer_fired ? ((ms / 300) % 2 ? COL_AMBER : COL_TXT)
                                    : COL_LBLUE, COL_BLACK);
    sprite.drawString(buf, W / 2, 120);
  } else if (wifi_ok) {
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setTextColor(COL_MUTED, COL_BLACK);
    sprite.drawString(WiFi.localIP().toString().c_str(), W / 2, 120);
  }

  // filament bottom-right
  char fbuf[12] = "--";
  if (d.filament_g >= 0)
    snprintf(fbuf, sizeof(fbuf), "%dg", (int)(d.filament_g + 0.5f));
  sprite.setTextDatum(textdatum_t::middle_right);
  sprite.setTextColor(d.filament_g >= 0 ? COL_CYAN : COL_MUTED, COL_BLACK);
  sprite.drawString(fbuf, W - 8, 120);
}

// ============================================================
//  OVERLAY (latched FINISH / FAILED)
// ============================================================
static void drawOverlayFrame(const TelemetryData& d, uint32_t ms) {
  bool finish = (ui_latched_result == 1);
  uint16_t bg = finish ? 0x05E0 : COL_OFFLINE;   // green / red
  sprite.fillSprite(bg);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setFont(&fonts::FreeSansBold12pt7b);   // 18pt "COMPLETE" (197px) > screen
  sprite.setTextColor(COL_TXT_BLACK, bg);
  sprite.drawString(finish ? "COMPLETE" : "FAILED", W / 2, 44);
  sprite.setFont(&fonts::FreeSansBold9pt7b);
  if (finish) {
    // total filament consumed ("filament: 12345 g" 12pt = 194px > screen)
    char fbuf[20];
    if (d.filament_g >= 0) snprintf(fbuf, sizeof(fbuf), "filament: %d g",
                                    (int)(d.filament_g + 0.5f));
    else                   snprintf(fbuf, sizeof(fbuf), "print finished");
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.drawString(fbuf, W / 2, 74);
  } else {
    char ebuf[20];
    snprintf(ebuf, sizeof(ebuf), "HMS errors: %d", d.hms_count);
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.drawString(ebuf, W / 2, 74);
  }
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(COL_TXT_BLACK, bg);
  sprite.drawString("STAN studio", W / 2, 104);
}

// ============================================================
//  CLOCK / TIMER / ABOUT screens
// ============================================================
static void drawClockFrame(uint32_t ms) {
  (void)ms;
  sprite.fillSprite(COL_BLACK);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char buf[16];
    char dateBuf[24];
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Date at top - minimal, elegant, centered
    snprintf(dateBuf, sizeof(dateBuf), "%s, %02d %s", days[t.tm_wday], t.tm_mday, months[t.tm_mon]);
    sprite.setFont(&fonts::Font2);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setTextColor(COL_CYAN, COL_BLACK);
    sprite.drawString(dateBuf, W / 2, 32);

    // Large Digits in Center
    if (uiGetClock12h()) {
      int h12 = t.tm_hour % 12;
      if (h12 == 0) h12 = 12;
      snprintf(buf, sizeof(buf), "%d:%02d:%02d", h12, t.tm_min, t.tm_sec);
      const char* ampm = (t.tm_hour >= 12) ? "PM" : "AM";

      // Dynamically measure both to center the entire time+AM/PM block
      sprite.setFont(&fonts::FreeSansBold18pt7b);
      int32_t time_w = sprite.textWidth(buf);
      sprite.setFont(&fonts::Font2);
      int32_t ampm_w = sprite.textWidth(ampm);

      const int gap = 6;
      int32_t total_w = time_w + gap + ampm_w;
      int32_t start_x = (W - total_w) / 2;
      int32_t baseline_y = 74;

      // Draw Time digits
      sprite.setFont(&fonts::FreeSansBold18pt7b);
      sprite.setTextDatum(textdatum_t::middle_left);
      sprite.setTextColor(COL_TXT, COL_BLACK);
      sprite.drawString(buf, start_x, baseline_y);

      // Draw AM/PM with clean size and breathing room
      sprite.setFont(&fonts::Font2);
      sprite.setTextDatum(textdatum_t::middle_left);
      sprite.setTextColor((t.tm_hour >= 12) ? COL_AMBER : COL_CYAN, COL_BLACK);
      sprite.drawString(ampm, start_x + time_w + gap, baseline_y + 2);
    } else {
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
      sprite.setFont(&fonts::FreeSansBold18pt7b);
      sprite.setTextDatum(textdatum_t::middle_center);
      sprite.setTextColor(COL_TXT, COL_BLACK);
      sprite.drawString(buf, W / 2, 74);
    }
    // Subtle divider and network footer
    sprite.drawFastHLine(8, 102, 144, COL_SEP);
    sprite.setFont(&fonts::Font0);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setTextColor(COL_MUTED, COL_BLACK);
    char foot[36];
    if (WiFi.isConnected()) {
      snprintf(foot, sizeof(foot), "IP: %s", WiFi.localIP().toString().c_str());
    } else {
      strncpy(foot, "STAN studio OS", sizeof(foot));
    }
    sprite.drawString(foot, W / 2, 115);
  } else {
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.setTextColor(COL_MUTED, COL_BLACK);
    sprite.drawString("Syncing Time...", W / 2, H / 2);
  }
}

static void drawAboutFrame(uint32_t ms) {
  (void)ms;
  sprite.fillSprite(COL_BLACK);

  // Top header with visionOS styling
  sprite.setFont(&fonts::Font2);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  sprite.drawString("ABOUT & DEV", 8, 4);
  sprite.setTextColor(COL_CYAN, COL_BLACK);
  sprite.drawString("v2.4", W - 32, 4);
  sprite.drawFastHLine(0, 18, W, COL_SEP);

  // Card 1: System info
  sprite.fillRoundRect(4, 22, W - 8, 25, 4, COL_CARD);
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(COL_MUTED, COL_CARD);
  sprite.drawString("OS / Hardware", 8, 29);
  sprite.setTextColor(COL_TXT, COL_CARD);
  sprite.drawString("STAN studio OS (ESP32)", 8, 40);

  // Card 2: Developer data
  sprite.fillRoundRect(4, 50, W - 8, 42, 4, COL_CARD);
  sprite.setTextColor(COL_MUTED, COL_CARD);
  sprite.drawString("Developer Data", 8, 57);

  sprite.setTextColor(COL_CYAN, COL_CARD);
  sprite.drawString("GitHub:", 8, 69);
  sprite.setTextColor(COL_TXT, COL_CARD);
  sprite.drawString("mxsourav.dev", 52, 69);

  sprite.setTextColor(COL_SALMON, COL_CARD);
  sprite.drawString("Insta:", 8, 81);
  sprite.setTextColor(COL_TXT, COL_CARD);
  sprite.drawString("@mxsourav.dev", 52, 81);

  // Network IP
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString("IP:", 8, 98);
  sprite.setTextColor(COL_ONLINE, COL_BLACK);
  sprite.drawString(WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "No Wi-Fi", 28, 98);

  // Footer
  sprite.drawFastHLine(0, 108, W, COL_SEP);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  char dev_foot[36];
  uint32_t up_s = millis() / 1000;
  snprintf(dev_foot, sizeof(dev_foot), "BUILD 2026.11 | UP %02lu:%02lu:%02lu",
           (unsigned long)(up_s / 3600), (unsigned long)((up_s % 3600) / 60), (unsigned long)(up_s % 60));
  sprite.drawString(dev_foot, W / 2, 118);
}

static void drawPcHudFrame(uint32_t ms) {
  (void)ms;
  bool is_light = g_settings.theme_light;
  uint16_t bg_col = is_light ? 0xEFBC : 0x0000;
  sprite.fillSprite(bg_col);

  const uint16_t HUD_INK    = is_light ? 0x18C3 : 0xFFFF;
  const uint16_t HUD_MUTED  = is_light ? 0x7BCF : 0x9CD3;
  const uint16_t HUD_LINE   = is_light ? 0xD635 : 0x2965;
  const uint16_t HUD_BAR_BG = is_light ? 0xE6F8 : 0x18E3;
  // Tangy highlighted neon in darkmode; same accent but faded paper ink in lightmode
  const uint16_t HUD_BAR_FG = is_light ? 0xC346 : 0xFC40;

  // Header
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(HUD_INK, bg_col);
  sprite.drawString("HW VITALS", 8, 5);

  char r_hdr[20];
  if (pcStatsIsActive()) {
    if (g_pc_stats.weather_temp_c > -50.0f && g_pc_stats.weather_temp_c < 60.0f) {
      snprintf(r_hdr, sizeof(r_hdr), "%.0fC %.8s", g_pc_stats.weather_temp_c, g_pc_stats.weather_desc);
    } else {
      strncpy(r_hdr, "LIVE 100Hz", sizeof(r_hdr));
    }
  } else {
    strncpy(r_hdr, "TELEMETRY", sizeof(r_hdr));
  }
  sprite.setTextDatum(textdatum_t::top_right);
  sprite.setTextColor(HUD_MUTED, bg_col);
  sprite.drawString(r_hdr, 152, 5);

  sprite.drawFastHLine(8, 16, 144, HUD_LINE);

  // Row 1: CPU (y=21)
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(HUD_INK, bg_col);
  sprite.drawString("CPU", 8, 21);
  char cpu_str[12];
  snprintf(cpu_str, sizeof(cpu_str), "%.0f%%", g_pc_stats.cpu_percent);
  sprite.drawString(cpu_str, 36, 21);
  int cpu_w = (int)((84.0f * constrain(g_pc_stats.cpu_percent, 0.0f, 100.0f)) / 100.0f);
  sprite.fillRect(68, 23, 84, 5, HUD_BAR_BG);
  if (cpu_w > 0) sprite.fillRect(68, 23, cpu_w, 5, HUD_BAR_FG);

  // Row 2: GPU (y=36)
  sprite.drawString("GPU", 8, 36);
  char gpu_str[16];
  snprintf(gpu_str, sizeof(gpu_str), "%.0f%% %.0fC", g_pc_stats.gpu_percent, g_pc_stats.gpu_temp_c);
  sprite.drawString(gpu_str, 36, 36);
  int gpu_w = (int)((64.0f * constrain(g_pc_stats.gpu_percent, 0.0f, 100.0f)) / 100.0f);
  sprite.fillRect(88, 38, 64, 5, HUD_BAR_BG);
  if (gpu_w > 0) sprite.fillRect(88, 38, gpu_w, 5, HUD_BAR_FG);

  // Row 3: RAM (y=51)
  sprite.drawString("RAM", 8, 51);
  char ram_str[20];
  snprintf(ram_str, sizeof(ram_str), "%.1f/%.1fG", g_pc_stats.ram_used_gb, g_pc_stats.ram_total_gb);
  sprite.drawString(ram_str, 36, 51);
  int ram_w = (int)((46.0f * constrain(g_pc_stats.ram_percent, 0.0f, 100.0f)) / 100.0f);
  sprite.fillRect(106, 53, 46, 5, HUD_BAR_BG);
  if (ram_w > 0) sprite.fillRect(106, 53, ram_w, 5, HUD_BAR_FG);

  // Row 4: DISK C: (y=66)
  sprite.drawString("DISK", 8, 66);
  char disk_str[20];
  if (g_pc_stats.disk_total_gb > 0.0f) {
    snprintf(disk_str, sizeof(disk_str), "%.0f/%.0fG", g_pc_stats.disk_used_gb, g_pc_stats.disk_total_gb);
  } else {
    snprintf(disk_str, sizeof(disk_str), "%.0f%%", g_pc_stats.disk_percent);
  }
  sprite.drawString(disk_str, 36, 66);
  int disk_w = (int)((46.0f * constrain(g_pc_stats.disk_percent, 0.0f, 100.0f)) / 100.0f);
  sprite.fillRect(106, 68, 46, 5, HUD_BAR_BG);
  if (disk_w > 0) sprite.fillRect(106, 68, disk_w, 5, HUD_BAR_FG);

  // Row 5: NET (y=81)
  sprite.drawString("NET", 8, 81);
  char net_str[28];
  if (g_pc_stats.net_down_kbs >= 1024.0f || g_pc_stats.net_up_kbs >= 1024.0f) {
    snprintf(net_str, sizeof(net_str), "D:%.1fM  U:%.1fM",
             g_pc_stats.net_down_kbs / 1024.0f, g_pc_stats.net_up_kbs / 1024.0f);
  } else {
    snprintf(net_str, sizeof(net_str), "D:%.0fK  U:%.0fK",
             g_pc_stats.net_down_kbs, g_pc_stats.net_up_kbs);
  }
  sprite.drawString(net_str, 36, 81);

  // Footer divider
  sprite.drawFastHLine(8, 97, 144, HUD_LINE);

  // Footer clean vitals: WiFi signal + system uptime
  sprite.setTextColor(HUD_MUTED, bg_col);
  char sys_str[36];
  uint32_t up_s = millis() / 1000;
  snprintf(sys_str, sizeof(sys_str), "WIFI %ddBm | UP %02lu:%02lu:%02lu",
           WiFi.RSSI(), (unsigned long)(up_s / 3600), (unsigned long)((up_s % 3600) / 60), (unsigned long)(up_s % 60));
  sprite.drawString(sys_str, 8, 105);
}

static void drawWeatherFrame(uint32_t ms) {
  (void)ms;
  bool is_light = g_settings.theme_light;
  uint16_t bg_col = is_light ? 0xFFFF : 0x0000;
  sprite.fillSprite(bg_col);

  const uint16_t TXT_INK   = is_light ? 0x0000 : 0xFFFF;
  const uint16_t TXT_MUTED = is_light ? 0x7BEF : 0x9CD3;
  const uint16_t LINE_COL  = is_light ? 0xD69A : 0x2965;
  const uint16_t ACCENT    = is_light ? 0x03E0 : 0x3E07;

  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(TXT_INK, bg_col);
  sprite.drawString("WEATHER VITALS", 8, 5);

  sprite.setTextDatum(textdatum_t::top_right);
  sprite.setTextColor(TXT_MUTED, bg_col);
  char city_hdr[16];
  snprintf(city_hdr, sizeof(city_hdr), "%.12s", g_pc_stats.city[0] ? g_pc_stats.city : "LOCAL");
  sprite.drawString(city_hdr, 152, 5);
  sprite.drawFastHLine(8, 17, 144, LINE_COL);

  // Big temperature display
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setFont(&fonts::FreeSansBold18pt7b);
  sprite.setTextColor(TXT_INK, bg_col);
  char temp_buf[16];
  if (g_pc_stats.weather_temp_c > -50.0f && g_pc_stats.weather_temp_c < 60.0f) {
    snprintf(temp_buf, sizeof(temp_buf), "%.0f C", g_pc_stats.weather_temp_c);
  } else {
    snprintf(temp_buf, sizeof(temp_buf), "-- C");
  }
  sprite.drawString(temp_buf, 12, 28);
  int32_t t_w = sprite.textWidth(temp_buf);
  int deg_x = 12 + t_w - 24;
  if (deg_x > 12) {
    sprite.drawCircle(deg_x, 33, 2, TXT_INK);
  }

  // Weather condition
  sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.setTextColor(ACCENT, bg_col);
  char desc_buf[20];
  snprintf(desc_buf, sizeof(desc_buf), "%.16s", g_pc_stats.weather_desc[0] ? g_pc_stats.weather_desc : "Clear");
  sprite.drawString(desc_buf, 12, 66);

  // Sub vitals
  sprite.setFont(&fonts::Font0);
  sprite.drawFastHLine(8, 92, 144, LINE_COL);
  sprite.setTextColor(TXT_MUTED, bg_col);
  char sub_buf[36];
  snprintf(sub_buf, sizeof(sub_buf), "CITY: %.16s", g_pc_stats.city[0] ? g_pc_stats.city : "Unknown");
  sprite.drawString(sub_buf, 10, 98);
  sprite.drawString(pcStatsIsActive() ? "SYNC: Real-time Live" : "SYNC: Standby / Offline", 10, 112);
}

static void drawAiStatsFrame(uint32_t ms) {
  (void)ms;
  bool is_light = g_settings.theme_light;
  uint16_t bg_col = is_light ? 0xFFFF : 0x0000;
  sprite.fillSprite(bg_col);

  const uint16_t TXT_INK   = is_light ? 0x0000 : 0xFFFF;
  const uint16_t TXT_MUTED = is_light ? 0x7BEF : 0x9CD3;
  const uint16_t LINE_COL  = is_light ? 0xD69A : 0x2965;
  const uint16_t ACCENT    = is_light ? 0x001F : 0x64BD;

  // Header: AI STATS | LIVE / IDLE
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(TXT_INK, bg_col);
  sprite.drawString("AI STATS", 8, 5);

  sprite.setTextDatum(textdatum_t::top_right);
  bool active = pcStatsIsActive();
  sprite.setTextColor(active ? (is_light ? 0x0400 : 0x07E0) : TXT_MUTED, bg_col);
  sprite.drawString(active ? "LIVE" : "IDLE", 152, 5);
  sprite.drawFastHLine(8, 17, 144, LINE_COL);

  sprite.setTextDatum(textdatum_t::top_left);

  // Row 1: GENRE
  sprite.setTextColor(TXT_MUTED, bg_col);
  sprite.drawString("GENRE", 8, 24);
  sprite.setTextColor(ACCENT, bg_col);
  char g_buf[24];
  snprintf(g_buf, sizeof(g_buf), "%.16s", g_pc_stats.music_genre[0] ? g_pc_stats.music_genre : "None");
  sprite.drawString(g_buf, 52, 24);

  // Row 2: ENERGY
  sprite.setTextColor(TXT_MUTED, bg_col);
  sprite.drawString("ENERGY", 8, 40);
  char v_str[16];
  snprintf(v_str, sizeof(v_str), "%.0f%%", g_pc_stats.music_vibe * 100.0f);
  sprite.setTextColor(TXT_INK, bg_col);
  sprite.drawString(v_str, 52, 40);
  int v_w = (int)(60.0f * constrain(g_pc_stats.music_vibe, 0.0f, 1.0f));
  sprite.fillRect(84, 42, 60, 5, is_light ? 0xEF7D : 0x18E3);
  if (v_w > 0) sprite.fillRect(84, 42, v_w, 5, ACCENT);

  // Row 3: MOOD
  sprite.setTextColor(TXT_MUTED, bg_col);
  sprite.drawString("MOOD", 8, 56);
  sprite.setTextColor(TXT_INK, bg_col);
  sprite.drawString(deskPetMoodName(deskPetGetMood()), 52, 56);

  // Row 4: TRACK
  sprite.setTextColor(TXT_MUTED, bg_col);
  sprite.drawString("TRACK", 8, 72);
  sprite.setTextColor(TXT_INK, bg_col);
  char t_buf[24];
  const char* track = g_spotify.title[0] ? g_spotify.title : "No Track";
  snprintf(t_buf, sizeof(t_buf), "%.16s", track);
  sprite.drawString(t_buf, 52, 72);

  // Divider
  sprite.drawFastHLine(8, 90, 144, LINE_COL);

  // Footer: Real system info (WiFi signal + uptime + IP)
  sprite.setTextColor(TXT_MUTED, bg_col);
  char f_buf1[36], f_buf2[36];
  uint32_t up_s = millis() / 1000;
  snprintf(f_buf1, sizeof(f_buf1), "WIFI %ddBm | UP %02lu:%02lu:%02lu",
           WiFi.RSSI(), (unsigned long)(up_s / 3600), (unsigned long)((up_s % 3600) / 60), (unsigned long)(up_s % 60));
  snprintf(f_buf2, sizeof(f_buf2), "IP %s", WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "No Wi-Fi");
  sprite.drawString(f_buf1, 8, 98);
  sprite.drawString(f_buf2, 8, 112);
}

static void drawTimerFrame(uint32_t ms) {
  sprite.fillSprite(COL_BLACK);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setFont(&fonts::FreeSansBold18pt7b);
  char buf[12];
  if (timerRunning()) {
    int left = (int)((timer_end_ms - ms) / 1000);
    if (left < 0) left = 0;
    snprintf(buf, sizeof(buf), "%02d:%02d", left / 60, left % 60);
    sprite.setTextColor(timer_fired ? ((ms / 300) % 2 ? COL_AMBER : COL_TXT)
                                    : COL_LBLUE, COL_BLACK);
  } else {
    snprintf(buf, sizeof(buf), "%d min", TIMER_OPTS[timer_sel]);
    sprite.setTextColor(COL_TXT, COL_BLACK);
  }
  sprite.drawString(buf, W / 2, 46);

  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  if (timer_fired)         sprite.drawString("TIME EXPIRED", W / 2, 76);
  else if (timerRunning()) sprite.drawString("COUNTDOWN ACTIVE", W / 2, 76);
  else                     sprite.drawString("TIMER READY", W / 2, 76);

  sprite.drawFastHLine(8, 98, 144, 0x2965);
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString(WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "STAN studio", W / 2, 114);
}

static bool s_art_in_frame = false;   // sprite buffer currently holds art
static uint16_t s_art_card_bg[152 * 34]; // raw background pixels preserved under liquid glass card

static void drawSpotifyFrame(uint32_t ms) {
  const int cardX = 4;
  const int cardY = 90;
  const int cardW = 152;
  const int cardH = 34;
  const int cardR = 9;

  // 1. Cover Artwork (Full 160x128 screen)
  // Only attempt read when new art is signaled (dirty), avoiding stale art resurrection
  if (spotifyArtDirty()) {
    spotifyClearArtDirty();
    s_art_in_frame = false;

    // 1. Primary: RAM Cover buffer (instant, zero SPI SD bus contention or ticking)
    if (spotifyHasCoverArt()) {
      size_t art_len = 0;
      const uint8_t* art_data = spotifyGetCoverArtData(art_len);
      if (art_data && art_len > 0) {
        if (art_len >= 4 && art_data[0] == 0xFF && art_data[1] == 0xD8) {
          s_art_in_frame = sprite.drawJpg(art_data, art_len, 0, 0, W, H);
        } else if (art_len >= W * H * 2) {
          sprite.pushImage(0, 0, W, H, (const uint16_t*)art_data);
          s_art_in_frame = true;
        }
      }
    }

    // 2. Secondary fallback: SD card /art.jpg (if mounted and freshly written)
    if (!s_art_in_frame && sdEnsureMounted() && SD.exists("/art.jpg")) {
      File f = SD.open("/art.jpg", FILE_READ);
      if (f) {
        s_art_in_frame = sprite.drawJpg(&f, 0, 0, W, H);
        f.close();
      }
    }
    // If artwork successfully loaded, save the clean background slice under the card
    if (s_art_in_frame) {
      uint16_t* fb = (uint16_t*)sprite.getBuffer();
      if (fb) {
        for (int y = 0; y < cardH; y++) {
          memcpy(&s_art_card_bg[y * cardW], &fb[(cardY + y) * W + cardX], cardW * sizeof(uint16_t));
        }
      }
    }
  }

  uint16_t* fb = (uint16_t*)sprite.getBuffer();

  if (!s_art_in_frame) {
    sprite.fillSprite(COL_BLACK);
    // Stylized vinyl record in upper center
    int cx = 80, cy = 44;
    sprite.fillCircle(cx, cy, 34, 0x1082);
    sprite.drawCircle(cx, cy, 28, 0x2129);
    sprite.drawCircle(cx, cy, 20, 0x2965);
    sprite.drawCircle(cx, cy, 12, 0x318A);
    sprite.fillCircle(cx, cy, 8, COL_SPOTIFY_GREEN);
    sprite.fillCircle(cx, cy, 3, COL_BLACK);
    // Solid card if no art
    sprite.fillRoundRect(cardX, cardY, cardW, cardH, cardR, 0x0841);
  } else if (fb) {
    // 2. Translucent Liquid Glass Effect:
    // Restore clean background under card, then blend with glass tint
    for (int y = 0; y < cardH; y++) {
      int fb_row = (cardY + y) * W + cardX;
      int bg_row = y * cardW;
      for (int x = 0; x < cardW; x++) {
        // Mask out 4 rounded corners of radius cardR (9px)
        int dx = 0, dy = 0;
        if (x < cardR && y < cardR) {
          dx = cardR - x; dy = cardR - y;
        } else if (x >= cardW - cardR && y < cardR) {
          dx = x - (cardW - 1 - cardR); dy = cardR - y;
        } else if (x < cardR && y >= cardH - cardR) {
          dx = cardR - x; dy = y - (cardH - 1 - cardR);
        } else if (x >= cardW - cardR && y >= cardH - cardR) {
          dx = x - (cardW - 1 - cardR); dy = y - (cardH - 1 - cardR);
        }
        if (dx * dx + dy * dy > cardR * cardR) continue;

        uint16_t raw = s_art_card_bg[bg_row + x];
        // Convert swap565_t (big-endian) to native 16-bit
        uint16_t c = (raw << 8) | (raw >> 8);
        uint16_t r = (c >> 11) & 0x1F;
        uint16_t g = (c >> 5)  & 0x3F;
        uint16_t b =  c        & 0x1F;

        // Translucent liquid glass: 30% background + elegant dark slate tint
        r = (r * 3) / 10 + 1;
        g = (g * 3) / 10 + 2;
        b = (b * 3) / 10 + 3;

        uint16_t tinted = (r << 11) | (g << 5) | b;
        // Store back in sprite buffer as swap565_t
        fb[fb_row + x] = (tinted << 8) | (tinted >> 8);
      }
    }
  }

  // 3. Subtle Liquid Glass Border (no harsh white specular reflection)
  sprite.drawRoundRect(cardX, cardY, cardW, cardH, cardR, 0x2988); // subtle cyan-gray glass edge

  // 4. Equalizer bars in card top-right
  int eqX = cardX + cardW - 18;
  int eqY = cardY + 10;
  if (g_spotify.is_playing) {
    for (int b = 0; b < 3; b++) {
      int h = 2 + (int)(abs(sin((ms / 130.0) + b * 1.3)) * 6);
      sprite.fillRect(eqX + b * 4, eqY - h, 2, h, COL_SPOTIFY_GREEN);
    }
  } else {
    sprite.fillRect(eqX + 2, eqY - 6, 2, 6, COL_MUTED);
    sprite.fillRect(eqX + 6, eqY - 6, 2, 6, COL_MUTED);
  }

  // 5. Track Title (with smooth Marquee scrolling if too long)
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setFont(&fonts::Font2);
  sprite.setTextColor(COL_TXT);

  const char* rawTitle = g_spotify.title[0] ? g_spotify.title : "No Track";
  int textW = sprite.textWidth(rawTitle);

  const int titleX = cardX + 8;
  const int titleY = cardY + 2;
  const int titleMaxW = (cardX + cardW - 20) - titleX; // ~124px (leaves space for equalizer bars)
  const int titleH = 14;

  if (textW <= titleMaxW) {
    // Fits completely: draw statically
    sprite.drawString(rawTitle, titleX, titleY);
  } else {
    // Does not fit: animate smoothly right-to-left (Marquee)
    static char s_last_marquee_title[96] = {0};
    static uint32_t s_marquee_start_ms = 0;

    if (strcmp(s_last_marquee_title, rawTitle) != 0) {
      strncpy(s_last_marquee_title, rawTitle, sizeof(s_last_marquee_title) - 1);
      s_last_marquee_title[sizeof(s_last_marquee_title) - 1] = 0;
      s_marquee_start_ms = ms;
    }

    const int gap = 28;
    const int scrollDist = textW + gap;
    const int speedPxPerSec = 26; // smooth, readable marquee speed (~1px per frame at 30 FPS)
    const uint32_t pauseAtStartMs = 1500; // pause 1.5s so user can read beginning
    const uint32_t scrollTimeMs = (scrollDist * 1000) / speedPxPerSec;
    const uint32_t totalPeriodMs = pauseAtStartMs + scrollTimeMs;

    uint32_t elapsed = (ms - s_marquee_start_ms) % totalPeriodMs;
    int offset = 0;
    if (elapsed > pauseAtStartMs) {
      offset = ((elapsed - pauseAtStartMs) * speedPxPerSec) / 1000;
    }

    sprite.setClipRect(titleX, titleY, titleMaxW, titleH);
    sprite.drawString(rawTitle, titleX - offset, titleY);
    if (offset > (textW - titleMaxW)) {
      sprite.drawString(rawTitle, titleX - offset + scrollDist, titleY);
    }
    sprite.clearClipRect();
  }

  // 6. Artist & Time (Row 2)
  uint32_t prog = g_spotify.progress_ms;
  uint32_t dur = g_spotify.duration_ms > 0 ? g_spotify.duration_ms : 1;
  char timeBuf[18];
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d / %d:%02d",
           (prog / 1000) / 60, (prog / 1000) % 60,
           (dur / 1000) / 60, (dur / 1000) % 60);

  sprite.setFont(&fonts::Font0);
  int timeWidth = sprite.textWidth(timeBuf);
  int maxArtistW = (cardW - 18) - timeWidth;
  if (maxArtistW < 24) maxArtistW = 24;

  char artistBuf[32];
  const char* rawArtist = g_spotify.artist[0] ? g_spotify.artist : "Spotify";
  strncpy(artistBuf, rawArtist, sizeof(artistBuf) - 1);
  artistBuf[sizeof(artistBuf) - 1] = 0;
  while (strlen(artistBuf) > 2 && sprite.textWidth(artistBuf) > maxArtistW) {
    int l = strlen(artistBuf);
    artistBuf[l - 1] = 0;
    if (l >= 4) { artistBuf[l - 3] = '.'; artistBuf[l - 2] = '.'; }
  }

  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(COL_CYAN);
  sprite.drawString(artistBuf, cardX + 8, cardY + 17);

  sprite.setTextDatum(textdatum_t::top_right);
  sprite.setTextColor(0xAD55);
  sprite.drawString(timeBuf, cardX + cardW - 8, cardY + 17);

  // 7. Sleek Progress Bar
  float pct = (float)prog / (float)dur;
  if (pct > 1.0f) pct = 1.0f;
  if (pct < 0.0f) pct = 0.0f;

  int bx = cardX + 8, by = cardY + 27, bw = cardW - 16, bh = 2;
  sprite.fillRoundRect(bx, by, bw, bh, 1, 0x2129); // track background
  int fillW = (int)(bw * pct);
  if (fillW > 0) {
    sprite.fillRoundRect(bx, by, fillW, bh, 1, COL_SPOTIFY_GREEN);
  }
  sprite.fillCircle(bx + fillW, by + 1, 2, COL_TXT); // glowing white knob
}

static void drawSpotifyLyricsFrame(uint32_t ms) {
  (void)ms;
  sprite.fillSprite(COL_BLACK);

  // Header: Track & Artist
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::top_center);
  sprite.setTextColor(COL_CYAN, COL_BLACK);
  char headBuf[36];
  snprintf(headBuf, sizeof(headBuf), "%s - %s", g_spotify.title, g_spotify.artist);
  if (strlen(headBuf) > 26) { headBuf[23] = '.'; headBuf[24] = '.'; headBuf[25] = 0; }
  sprite.drawString(headBuf, W / 2, 8);
  sprite.drawFastHLine(8, 20, W - 16, COL_SEP);

  // Center: Current Lyric line (Large & Bold, Glowing)
  sprite.setFont(&fonts::Font2);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  const char* curL = g_spotify.lyric_cur[0] ? g_spotify.lyric_cur : "Music Playing";
  sprite.drawString(curL, W / 2, 46);

  // Next Lyric line (Muted)
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  const char* nextL = g_spotify.lyric_next[0] ? g_spotify.lyric_next : "";
  sprite.drawString(nextL, W / 2, 70);

  // Mini Timeline bar
  uint32_t prog = g_spotify.progress_ms;
  uint32_t dur = g_spotify.duration_ms > 0 ? g_spotify.duration_ms : 1;
  float pct = (float)prog / (float)dur;
  if (pct > 1.0f) pct = 1.0f;
  sprite.fillRoundRect(10, 88, 140, 3, 1, COL_CARD);
  if (pct > 0.0f) sprite.fillRoundRect(10, 88, (int)(140 * pct), 3, 1, COL_SPOTIFY_GREEN);

  // Time & exit hint
  char curTime[12];
  snprintf(curTime, sizeof(curTime), "%d:%02d", (prog / 1000) / 60, (prog / 1000) % 60);
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.drawString(curTime, 10, 96);

  sprite.drawFastHLine(8, 108, 144, COL_SEP);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  char art_buf[28];
  snprintf(art_buf, sizeof(art_buf), "%.22s", g_spotify.artist[0] ? g_spotify.artist : "Spotify Lyrics");
  sprite.drawString(art_buf, W / 2, 118);
}

// ============================================================
//  BUTTON DISPATCH
// ============================================================
static void dispatchButtons(uint8_t ev) {
  if (!ev) return;

  // custom image: any button exits back to home and resumes MQTT
  // (image mode had paused the telemetry connection)
  if (view == VIEW_CUSTOM_IMAGE) {
    uiClearCustomImage();
    mqttResume();
    return;
  }

  // overlay: select = OK (clear latch), any menu action also exits
  if (view == VIEW_OVERLAY) {
    if (ev == 1 || ev == 2 || ev == 3 || ev == 4 || ev == 5) {
      uint8_t ack_kind = ui_latched_result;   // 1 = complete, 2 = failed
      ui_latched_result = 0;
      s_had_active_print = false;
      view = VIEW_HOME;
      petMoodCelebrate(ack_kind == 2 ? 2 : 0); // pet reacts after the ACK
    }
    return;
  }

  // HOME event (10): return to main dashboard or spotify player
  if (ev == 10) {
    ui_latched_result = 0;
    s_had_active_print = false;
    view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME;
    reset_arm = 0;
    return;
  }

  // menu double-press (ev == 4), long-press (ev == 3), or LEFT (ev == 8) = back one level
  if (ev == 4 || ev == 3 || ev == 8) {
    switch (view) {
      case VIEW_MENU:    view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME; break;
      case VIEW_LEDLIST: view = VIEW_MENU; menu_focus = 3; break;
      case VIEW_CLOCK:   view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_MENU; menu_focus = 7; break;
      case VIEW_ABOUT:   view = VIEW_MENU; menu_focus = 10; break;
      case VIEW_TIMER:   view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_MENU; menu_focus = 11; break;
      case VIEW_DESK_PET: view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME; break;
      case VIEW_PC_HUD:  view = VIEW_DESK_PET; break;
      case VIEW_SPOTIFY:
      case VIEW_SPOTIFY_LYRICS:
        if (ev == 3) {
          beepButton();
          uiSetAppMode(MODE_PRINTER);
        } else {
          view = VIEW_MENU; menu_focus = 0;
        }
        break;
      default:           view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME; break;
    }
    reset_arm = 0;
    return;
  }

  switch (view) {
    case VIEW_HOME:
      if (ev == 2 || ev == 9) { // OK / Select / Right
        static uint32_t s_last_home_ok_ms = 0;
        uint32_t now_ms = millis();
        if (now_ms - s_last_home_ok_ms < 380) {
          s_last_home_ok_ms = 0;
          beepButton();
          s_pc_hud_latched = true;
          view = VIEW_PC_HUD;
          s_last_user_activity_ms = now_ms;
          Serial.println(F("[UI] Double-click OK on Home -> LATCHED PC HUD"));
        } else {
          s_last_home_ok_ms = now_ms;
          view = VIEW_MENU;
          menu_focus = 0;
        }
      } else if (ev == 1 || ev == 5 || ev == 6 || ev == 7) {
        view = VIEW_MENU;
        menu_focus = 0;
      }
      break;

    case VIEW_MENU:
      if (ev == 6) { // UP
        menu_focus = (menu_focus - 1 + MENU_ROWS) % MENU_ROWS;
      } else if (ev == 1 || ev == 7) { // DOWN
        menu_focus = (menu_focus + 1) % MENU_ROWS;
      } else if (ev == 2 || ev == 5 || ev == 9) { // OK / RIGHT / LONG-SELECT
        startBeepPattern(1); // confirmation beep
        switch (menu_focus) {
          case 0: uiActivateDeskPet(); break;
          case 1: { // Pet Stats toggle
            g_settings.pet_show_stats = !g_settings.pet_show_stats;
            settingsSave();
            break;
          }
          case 2: { // Pet Music toggle
            g_settings.pet_show_music = !g_settings.pet_show_music;
            settingsSave();
            break;
          }
          case 3: { // Mode toggle
            if (s_app_mode == MODE_PRINTER) {
              uiSetAppMode(MODE_SPOTIFY);
            } else {
              uiSetAppMode(MODE_PRINTER);
            }
            break;
          }
          case 4: uiActivatePcHud(); break;
          case 5: {  // brightness cycle
            uint8_t b = uiGetBacklight();
            b = (b >= 100) ? 40 : b + 30;
            uiSetBacklight(b);
            g_settings.brightness = b;
            settingsSave();
            break;
          }
          case 6: view = VIEW_LEDLIST; menu_focus = 0; break;
          case 7: applySoundToggle(); break;
          case 8: {  // siren policy: Auto-Mute <-> Until OK
            g_settings.siren_mode = g_settings.siren_mode ? 0 : 1;
            settingsSave();
            break;
          }
          case 9: view = VIEW_CLOCK; break;
          case 10: view = VIEW_TIMER; timer_sel = 0; break;
          case 11: { // Theme Dark/Light
            uiSetThemeLight(!uiGetThemeLight());
            break;
          }
          case 12: view = VIEW_ABOUT; break;
          case 13: {
            view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME;
            menu_focus = 0;
            break;
          }
        }
      }
      break;

    case VIEW_LEDLIST:
      if (ev == 6) { // UP
        menu_focus = (menu_focus - 1 + LEDLIST_ROWS) % LEDLIST_ROWS;
      } else if (ev == 1 || ev == 7) { // DOWN
        menu_focus = (menu_focus + 1) % LEDLIST_ROWS;
      } else if (ev == 2 || ev == 5 || ev == 9) { // OK / RIGHT
        startBeepPattern(1); // confirmation beep
        if (menu_focus == LEDLIST_ROWS - 1) { view = VIEW_MENU; menu_focus = 6; }
        else {
          uint8_t m = 0;
          switch (menu_focus) {
            case 0: m = 0; break; // Auto-Sync
            case 1: m = 9; break; // Music Sync
            case 2: m = 2; break; // Rainbow
            case 3: m = 1; break; // OFF
            case 4: m = 3; break; // Red
            case 5: m = 4; break; // Green
            case 6: m = 5; break; // Blue
            case 7: m = 6; break; // White
            case 8: m = 7; break; // Amber
          }
          ledSetOverride(m);
          g_settings.led_override = m;
          settingsSave();
        }
      }
      break;

    case VIEW_CLOCK:
      if (ev == 2 || ev == 9) { // SELECT or RIGHT: Toggle 12h / 24h format!
        beepButton();
        uiSetClock12h(!uiGetClock12h());
      } else if (ev != 0) { // Any Menu press, hold, back or home exits clock
        beepButton();
        if (s_app_mode == MODE_SPOTIFY) {
          view = VIEW_SPOTIFY;
        } else {
          view = VIEW_MENU;
          menu_focus = 6;
        }
      }
      break;

    case VIEW_ABOUT:
      if (ev != 0) {
        beepButton();
        view = VIEW_MENU;
        menu_focus = 9;
      }
      break;

    case VIEW_TIMER:
      if (timer_fired) {
        if (ev != 0) { timer_fired = false; timer_end_ms = 0; }
      } else if (timerRunning()) {
        if (ev == 2 || ev == 9) timer_end_ms = 0;    // stop
      } else {
        if (ev == 6) timer_sel = (timer_sel - 1 + TIMER_OPT_COUNT) % TIMER_OPT_COUNT; // UP
        else if (ev == 1 || ev == 7) timer_sel = (timer_sel + 1) % TIMER_OPT_COUNT; // DOWN
        else if (ev == 2 || ev == 9) { // OK / RIGHT
          timer_end_ms = millis() + TIMER_OPTS[timer_sel] * 60000UL;
          timer_fired = false;
          view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME;
        }
      }
      break;

    case VIEW_DESK_PET:
      if (ui_latched_result != 0) {
        // Pet is showing the print result emotion: any press ACKs it
        // exactly like the overlay's OK (mood engine reacts to relief)
        uint8_t ack_kind = ui_latched_result;
        ui_latched_result = 0;
        s_had_active_print = false;
        beepButton();
        petMoodCelebrate(ack_kind == 2 ? 2 : 0);
        break;
      }
      if (ev == 2 || ev == 9) { // SELECT or RIGHT
        // Double-click within 380ms toggles and latches full-screen PC HUD!
        static uint32_t s_last_pet_click_ms = 0;
        uint32_t now_ms = millis();
        if (now_ms - s_last_pet_click_ms < 380) {
          s_last_pet_click_ms = 0;
          beepButton();
          s_pc_hud_latched = true;
          view = VIEW_PC_HUD;
          s_last_user_activity_ms = now_ms;
          Serial.println(F("[UI] Double-click OK on Pet -> LATCHED PC HUD"));
        } else {
          s_last_pet_click_ms = now_ms;
          beepPet();
          deskPetReactClick();
        }
      } else if (ev == 1) { // MENU: Open Settings
        beepButton();
        view = VIEW_MENU;
        menu_focus = 0;
      } else if (ev != 0) { // Any other button: Wake up to dashboard/spotify
        beepButton();
        view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME;
      }
      break;

    case VIEW_PC_HUD:
      s_last_user_activity_ms = millis();
      if (ev == 2 || ev == 9) { // SELECT / OK
        static uint32_t s_last_hud_ok_ms = 0;
        uint32_t now_ms = millis();
        if (now_ms - s_last_hud_ok_ms < 380) {
          s_last_hud_ok_ms = 0;
          beepButton();
          s_pc_hud_latched = false;
          view = VIEW_DESK_PET;
          s_last_user_activity_ms = now_ms;
          Serial.println(F("[UI] Double-click OK on PC HUD -> UNLATCHED to Desk Pet"));
        } else {
          s_last_hud_ok_ms = now_ms;
          beepButton();
          // Single OK does NOT exit - keeps PC HUD latched!
        }
      } else if (ev == 1) { // MENU: Open Settings
        beepButton();
        view = VIEW_MENU;
        menu_focus = 0;
      }
      break;

    case VIEW_WEATHER:
    case VIEW_AI_STATS:
      s_last_user_activity_ms = millis();
      if (ev == 2 || ev == 9) {
        beepButton();
        view = VIEW_DESK_PET;
      } else if (ev == 1) {
        beepButton();
        view = VIEW_MENU;
        menu_focus = 0;
      }
      break;

    case VIEW_SPOTIFY:
      if (ev == 2 || ev == 9) { // SELECT or RIGHT: Toggle to Lyrics!
        beepButton();
        view = VIEW_SPOTIFY_LYRICS;
      } else if (ev == 1) { // MENU: Open Settings / Menu!
        beepButton();
        view = VIEW_MENU;
        menu_focus = 0;
      } else if (ev == 3 || ev == 10) { // Long Menu or HOME: Return to 3D Printer Mode!
        beepButton();
        uiSetAppMode(MODE_PRINTER);
      }
      break;

    case VIEW_SPOTIFY_LYRICS:
      if (ev == 2 || ev == 9) { // SELECT or RIGHT: Toggle back to Cover!
        beepButton();
        view = VIEW_SPOTIFY;
      } else if (ev == 1) { // MENU: Open Settings / Menu!
        beepButton();
        view = VIEW_MENU;
        menu_focus = 0;
      } else if (ev == 3 || ev == 10) { // Long Menu or HOME: Return to 3D Printer Mode!
        beepButton();
        uiSetAppMode(MODE_PRINTER);
      }
      break;
  }
}

static void applySoundToggle() {
  uint8_t m = (getBuzzerMode() + 1) % 3;
  setBuzzerMode(m);
  g_settings.buzzer_mode = m;
  settingsSave();
  if (m != 2) {
    beepButton(); // audible feedback with newly selected mode
  }
}

// ---------------- Backlight control (menu Brightness row) --------------
static uint8_t bl_pct = 100;
uint8_t uiGetBacklight() { return bl_pct; }
void uiSetBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  bl_pct = pct;
  if (PIN_TFT_BL >= 0 && PIN_TFT_BL != PIN_SD_MISO) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_TFT_BL, (uint32_t)(pct * 255 / 100));
#else
    ledcWrite(0, (uint32_t)(pct * 255 / 100));
#endif
  }
}

// ------------------------------------------------------------
// Mini now-playing strip on the pet screen (vibe mode): the pet
// keeps the spotlight while a slim chip shows title + progress.
// ------------------------------------------------------------
static void drawPetVibeStrip(uint32_t ms) {
  if (!g_settings.pet_show_music || !g_settings.pet_music_vibe) return;

  static char s_last_title[32] = {0};
  static uint32_t s_last_active_ms = 0;
  static float s_last_frac = 0.0f;

  bool fresh = (g_spotify.last_update_ms > 0) && (ms - g_spotify.last_update_ms < 6000);
  if (g_spotify.is_playing && fresh && g_spotify.title[0] != '\0') {
    strncpy(s_last_title, g_spotify.title, sizeof(s_last_title) - 1);
    s_last_title[sizeof(s_last_title) - 1] = 0;
    s_last_active_ms = ms;
    if (g_spotify.duration_ms > 0) {
      s_last_frac = (float)g_spotify.progress_ms / (float)g_spotify.duration_ms;
    }
  }

  // Grace period latch: keep card visible during brief track-change gaps to eliminate flicker
  bool is_active = (s_last_title[0] != '\0') && (s_last_active_ms > 0) && (ms - s_last_active_ms < 4000);
  if (!is_active) return;

  const int pill_x = 4;
  const int pill_y = 3;
  const int pill_w = 152;
  const int pill_h = 20;
  const int pill_r = 5;

  sprite.fillRoundRect(pill_x, pill_y, pill_w, pill_h, pill_r, 0xF7BE);
  sprite.drawRoundRect(pill_x, pill_y, pill_w, pill_h, pill_r, 0xD69A);
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(0x0000, 0xF7BE);

  char line[32];
  snprintf(line, sizeof(line), "%.24s", s_last_title);
  sprite.drawString(line, pill_x + 6, pill_y + 7);

  float frac = s_last_frac;
  if (frac > 1.0f) frac = 1.0f;
  if (frac < 0.0f) frac = 0.0f;
  sprite.fillRect(pill_x + 6, pill_y + 15, pill_w - 12, 2, 0xD69A);                  // track base
  sprite.fillRect(pill_x + 6, pill_y + 15, (int)((pill_w - 12) * frac), 2, 0x0000); // progress
}

// ============================================================
//  UI TASK
// ============================================================
static volatile bool s_ota_active = false;
void uiSetOtaActive(bool active) { s_ota_active = active; }
bool uiIsOtaActive() { return s_ota_active; }

static void uiTask(void* /*param*/) {
  TickType_t last_wake = xTaskGetTickCount();

  tft.init();
  tft.setRotation(1);
  tft.setBrightness(255);
  if (PIN_TFT_BL >= 0 && PIN_TFT_BL != PIN_SD_MISO) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_TFT_BL, 255);
#else
    ledcWrite(0, 255);
#endif
  }
  Serial.println(F("[UI] panel init OK (landscape 160x128) @ full brightness"));

  sprite.setColorDepth(16);
  if (!sprite.createSprite(W, H)) {
    Serial.println(F("[FATAL] sprite alloc failed"));
    vTaskDelete(nullptr);
  }
  deskPetInit();
  boot_start_ms = millis();
  if (options_sound_on()) {
    playHardwareBeep(BUZZER_FREQ_OK, 40); // 40ms synchronous boot chirp, guaranteed stops
  }
  Serial.println(F("[UI] boot splash started"));

  while (true) {
    if (s_ota_active) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    uint32_t ms = millis();
    TelemetryData d;
    telemetryRead(d);

    if (view == VIEW_BOOT && ms - boot_start_ms >= BOOT_MS) {
      uiSwitchView(g_settings.default_screen[0] ? g_settings.default_screen : "pet");
      startBeepPattern(1);   // boot-complete beep (verifies GPIO19 buzzer)
      Serial.printf("[UI] boot done -> %s (beep)\n", uiGetCurrentViewName());
    }

    // --- timer expiry ---
    if (timerRunning() && !timer_fired && (int32_t)(ms - timer_end_ms) >= 0) {
      timer_fired = true;
      startBeepPattern(2);          // reuse the double-beep alert
    }

    // Track active print state: only an active print can transition to FINISH/FAILED overlay!
    if (d.state == PST_RUNNING || d.state == PST_PAUSED) {
      s_had_active_print = true;
    } else if (d.state == PST_IDLE && d.percent == 0) {
      s_had_active_print = false;
    }

    // --- latch: FINISH/FAILED -> full-screen overlay until OK ---
    // (suppressed during image mode: the pushed image owns the screen;
    //  on the Desk Pet view the mood engine shows the emotion instead -
    //  the pet slowly turns red while a failure is ignored)
    if (ui_latched_result == 0 && !custom_image_active) {
      if (d.state == PST_FINISH && s_had_active_print && (d.percent >= 98 || (d.percent >= 95 && d.remain_min == 0))) {
        ui_latched_result = 1;
        if (view != VIEW_DESK_PET) view = VIEW_OVERLAY;
        s_had_active_print = false;
        s_pet_print_woken = false;
      }
      else if (d.state == PST_FAILED && s_had_active_print) {
        ui_latched_result = 2;
        if (view != VIEW_DESK_PET) view = VIEW_OVERLAY;
        s_had_active_print = false;
        s_pet_print_woken = false;
      }
    }
    if (d.state == PST_IDLE && d.percent == 0) {
      s_pet_print_woken = false;
    }

    // --- buttons (single-fire events from peripherals task) ---
    uint8_t ev = ui_button_event;
    if (ev) ui_button_event = 0;
    dispatchButtons(ev);

    // --- Desk Pet Auto-Idle / Wakeup Management ---
    if (s_last_user_activity_ms == 0) s_last_user_activity_ms = ms;
    if (ev != 0) {
      s_last_user_activity_ms = ms;
    }

    bool printer_idle = (d.state != PST_RUNNING && d.state != PST_PAUSED && ui_latched_result == 0);
    bool spotify_idle = (!g_spotify.is_playing || (g_spotify.last_update_ms > 0 && (ms - g_spotify.last_update_ms > 8000)));

    if (view == VIEW_DESK_PET) {
      // Wake up on incoming music playback. In vibe mode (pet_music_vibe)
      // the pet STAYS and dances to the beat with a mini progress strip.
      if (g_spotify.is_playing && (ms - g_spotify.last_update_ms < 10000)) {
        if (!g_settings.pet_music_vibe) {
          Serial.println(F("[UI] Music playing -> wake from Desk Pet to SPOTIFY"));
          view = VIEW_SPOTIFY;
          s_last_user_activity_ms = ms;
        }
      }
      // Wake up on active 3D printing - only once per print, so the user
      // can manually go back and let the pet watch (THINKING) if preferred
      else if (d.state == PST_RUNNING || d.state == PST_PAUSED) {
        if (!s_pet_print_woken) {
          s_pet_print_woken = true;
          Serial.println(F("[UI] Print active -> wake from Desk Pet to HOME"));
          view = VIEW_HOME;
        }
        s_last_user_activity_ms = ms;
      }
    } else if (view == VIEW_MENU) {
      // 15s timeout in settings menu: return to active mode
      if (ms - s_last_user_activity_ms >= 15000) {
        view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_HOME;
      }
    } else if (view == VIEW_SPOTIFY) {
      // With vibe mode on, PLAYING MUSIC summons the pet to dance
      bool music_vibe_entry = (g_spotify.is_playing && (ms - g_spotify.last_update_ms < 10000) &&
                               g_settings.pet_music_vibe);
      if (music_vibe_entry && (ms - s_last_user_activity_ms >= 8000)) {
        view = VIEW_DESK_PET;
        deskPetSetMood(PET_MOOD_NEUTRAL);
      }
    } else if (view == VIEW_PC_HUD) {
      // If latched, NEVER auto-timeout to Desk Pet or anything else!
      if (!s_pc_hud_latched) {
        if (ms - s_last_user_activity_ms >= 20000) {
          view = VIEW_DESK_PET;
        }
        if (g_spotify.is_playing && (ms - g_spotify.last_update_ms < 10000)) {
          view = VIEW_SPOTIFY;
          s_last_user_activity_ms = ms;
        } else if (d.state == PST_RUNNING || d.state == PST_PAUSED) {
          view = VIEW_HOME;
          s_last_user_activity_ms = ms;
        }
      }
    }

    // Dynamic frame pacing: 100 FPS for Desk Pet (10ms); 10 FPS for static image (100ms); 30 FPS elsewhere
    TickType_t current_frame_ticks = (view == VIEW_DESK_PET) ? pdMS_TO_TICKS(10) :
                                     (view == VIEW_CUSTOM_IMAGE) ? pdMS_TO_TICKS(100) : pdMS_TO_TICKS(1000 / UI_TARGET_FPS);
    static UiView s_prev_view = VIEW_BOOT;
    if (view != s_prev_view) {
      if (s_prev_view == VIEW_DESK_PET) petMoodOnLeave();
      s_prev_view = view;
      last_wake = xTaskGetTickCount();
    }

    // --- smooth progress interpolation between network polls (handover
    //     Task 5): advance the bar every frame on Core 1 so it animates at
    //     UI framerate instead of jumping once per 3-5s poll ---
    static uint32_t s_prog_frame_ms = 0;
    if (view == VIEW_SPOTIFY || view == VIEW_SPOTIFY_LYRICS) {
      if (s_prog_frame_ms == 0 || (ms - s_prog_frame_ms) > 200) {
        s_prog_frame_ms = ms;
      }
      uint32_t delta = ms - s_prog_frame_ms;
      s_prog_frame_ms = ms;
      bool fresh = (g_spotify.last_update_ms > 0 && (ms - g_spotify.last_update_ms < 10000));
      if (g_spotify.is_playing && fresh && delta > 0 && delta <= 100) {
        g_spotify.progress_ms += delta;
        if (g_spotify.duration_ms > 0 && g_spotify.progress_ms > g_spotify.duration_ms) {
          g_spotify.progress_ms = g_spotify.duration_ms;
        }
      }
    } else {
      s_prog_frame_ms = ms;
    }

    // art lives in the sprite buffers only while the cover view is up;
    // any other view (incl. lyrics) overwrites both buffers
    if (view != VIEW_SPOTIFY) s_art_in_frame = false;

    // --- render ---
    if (!s_fb_mutex) s_fb_mutex = xSemaphoreCreateMutex();
    xSemaphoreTake(s_fb_mutex, portMAX_DELAY);

    switch (view) {
      case VIEW_BOOT:    drawBootFrame(ms - boot_start_ms); break;
      case VIEW_HOME:    drawHomeFrame(d, ms); break;
      case VIEW_MENU:
        MENU_LABELS[3] = (s_app_mode == MODE_SPOTIFY) ? "Printer Mode" : "Spotify Mode";
        drawList("SETTINGS", MENU_LABELS, MENU_ROWS,
                 menu_focus, menuValue, menuActive); break;
      case VIEW_LEDLIST: drawList("LED CONTROL", LEDLIST_LABELS, LEDLIST_ROWS,
                                  menu_focus, ledListValue, ledListActive); break;
      case VIEW_CLOCK:   drawClockFrame(ms); break;
      case VIEW_TIMER:   drawTimerFrame(ms); break;
      case VIEW_ABOUT:   drawAboutFrame(ms); break;
      case VIEW_OVERLAY: drawOverlayFrame(d, ms); break;
      case VIEW_SPOTIFY:        drawSpotifyFrame(ms); break;
      case VIEW_SPOTIFY_LYRICS: drawSpotifyLyricsFrame(ms); break;
      case VIEW_DESK_PET:
        petMoodUpdate(ms, d);
        deskPetDrawFrame(sprite, ms);
        drawPetVibeStrip(ms);
        break;
      case VIEW_PC_HUD:         drawPcHudFrame(ms); break;
      case VIEW_WEATHER:        drawWeatherFrame(ms); break;
      case VIEW_AI_STATS:       drawAiStatsFrame(ms); break;
      case VIEW_CUSTOM_IMAGE:
        if (custom_image_active) {
          static bool had_notif = false;
          bool has_notif = (millis() < notif_until_ms && notif_title[0]);
          if (!custom_image_rendered || has_notif || had_notif || s_custom_jpeg_dirty) {
            if (s_custom_jpeg_buf && s_custom_jpeg_len > 0) {
              s_custom_jpeg_dirty = false;
              sprite.drawJpg(s_custom_jpeg_buf, s_custom_jpeg_len, 0, 0, W, H);
            } else if (s_custom_image_buf) {
              sprite.pushImage(0, 0, W, H, (const lgfx::swap565_t*)s_custom_image_buf);
            }
            custom_image_rendered = true;
            had_notif = has_notif;
          }
        }
        break;
    }

    // --- Live Notification Banner Overlay ---
    static bool s_had_notif = false;
    bool has_notif = (millis() < notif_until_ms && notif_title[0]);
    if (s_had_notif && !has_notif) {
      s_art_in_frame = false; // Redraw artwork once notification banner expires
    }
    s_had_notif = has_notif;

    if (has_notif && view != VIEW_DESK_PET) {
      sprite.fillRoundRect(4, 4, W - 8, 38, 6, COL_CARD);
      sprite.drawRoundRect(4, 4, W - 8, 38, 6, COL_CYAN);
      sprite.fillCircle(14, 15, 3, COL_AMBER);
      sprite.setFont(&fonts::Font0);
      sprite.setTextDatum(textdatum_t::middle_left);
      sprite.setTextColor(COL_CYAN, COL_CARD);
      sprite.drawString(notif_title, 22, 15);
      sprite.setTextColor(COL_TXT, COL_CARD);
      sprite.drawString(notif_msg, 10, 30);
    }

    // Push full frame to panel (LovyanGFX handles CS windowing and DMA cleanly)
    g_tft_spi_busy = true;
    sprite.pushSprite(0, 0);
    g_tft_spi_busy = false;
    xSemaphoreGive(s_fb_mutex);

    vTaskDelayUntil(&last_wake, current_frame_ticks);
  }
}

void uiTaskStart() {
  xTaskCreatePinnedToCore(
      uiTask, "uiTask", TASK_STACK_UI, nullptr, 2, nullptr, 1);
}
