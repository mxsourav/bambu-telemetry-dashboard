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

#define COL_BLACK     (uint16_t)(uiGetThemeLight() ? 0xFFFF : 0x0000)
#define COL_CARD      (uint16_t)(uiGetThemeLight() ? 0xEF7D : 0x18E3)
#define COL_SEP       (uint16_t)(uiGetThemeLight() ? 0xCE59 : 0x1082)
#define COL_TXT       (uint16_t)(uiGetThemeLight() ? 0x0000 : 0xFFFF)
#define COL_TXT_BLACK (uint16_t)(uiGetThemeLight() ? 0xFFFF : 0x0000)
#define COL_MUTED     (uint16_t)(uiGetThemeLight() ? 0x738E : 0x6B4D)
#define COL_ONLINE    (uint16_t)0x07E0
#define COL_OFFLINE   (uint16_t)0xF800
#define COL_CONNECT   (uint16_t)0xFFE0
#define COL_CYAN      (uint16_t)(uiGetThemeLight() ? 0x03B7 : 0x04FF)
#define COL_AMBER     (uint16_t)(uiGetThemeLight() ? 0xD380 : 0xFD20)
#define COL_SALMON    (uint16_t)(uiGetThemeLight() ? 0xD1E2 : 0xFB68)
#define COL_LBLUE     (uint16_t)(uiGetThemeLight() ? 0x3377 : 0x7DDF)
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
                        VIEW_SPOTIFY, VIEW_SPOTIFY_LYRICS };
static UiView view = VIEW_BOOT;
static uint32_t boot_start_ms = 0;
static uint16_t* s_custom_image_buf = nullptr;
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
static int8_t menu_focus = 0;       // row in current list
static int8_t timer_sel = 0;        // index into TIMER_OPTS
static int8_t reset_arm = 0;        // 0=normal 1=armed confirm
static const int8_t TIMER_OPTS[] = {5, 10, 15, 30, 60};
#define TIMER_OPT_COUNT 5
static uint32_t timer_end_ms = 0;   // 0 = timer off
static bool     timer_fired = false;

// ---------------- Button dispatch helpers -----------------------------
// list row counts
#define MENU_ROWS      11
#define LEDLIST_ROWS   9

static const char* MENU_LABELS[MENU_ROWS] = {
  "Spotify Mode", "LED Control", "Sound", "Siren", "Brightness", "Clock",
  "Theme", "Clock Fmt", "About", "Timer", "Back" };

static const char* LEDLIST_LABELS[LEDLIST_ROWS] = {
  "Auto-Sync", "OFF", "RAINBOW", "RED", "GREEN", "BLUE", "WHITE",
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
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString("M:down SEL:ok Mx2:home", 4, 120);  // 38ch hint = 228px, clipped
}

// ---------------- menu row values / active marks ----------------------
static bool timerRunning() { return timer_end_ms != 0; }

static const char* menuValue(int i) {
  static char buf[16];
  switch (i) {
    case 0: {
      return (s_app_mode == MODE_SPOTIFY) ? "Active" : "Switch ->";
    }
    case 2: {
      uint8_t m = getBuzzerMode();
      return (m == 0) ? "Passive" : (m == 1) ? "Active" : "Off";
    }
    case 3:  // siren policy
      return g_settings.siren_mode ? "Till OK" : "Auto";
    case 4: {
      snprintf(buf, sizeof(buf), "%d%%", uiGetBacklight());
      return buf;
    }
    case 5: {  // clock preview
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
    case 6: return uiGetThemeLight() ? "Light" : "Dark";
    case 7: return uiGetClock12h() ? "12H" : "24H";
    case 8: return ">";
    case 9: {  // timer status
      if (!timerRunning()) return "Off";
      int left = (timer_end_ms - millis()) / 1000;
      snprintf(buf, sizeof(buf), "%dm%02ds", left / 60, left % 60);
      return buf;
    }
    default: return nullptr;
  }
}

static bool menuActive(int i) {
  switch (i) {
    case 2: return options_sound_on();
    case 3: return g_settings.siren_mode != 0;   // highlighted = till-OK
    case 6: return uiGetThemeLight();
    case 7: return uiGetClock12h();
    case 9: return timerRunning();
    default: return false;
  }
}

static const char* ledListValue(int i) {
  switch (i) {
    case 0: return "auto";
    case 2: return "fx";
    default: return nullptr;
  }
}

static bool ledListActive(int i) {
  uint8_t ov = ledGetOverride();
  switch (i) {
    case 0: return ov == 0;
    case 1: return ov == 1;
    case 2: return ov == 2;
    case 3: return ov == 3;
    case 4: return ov == 4;
    case 5: return ov == 5;
    case 6: return ov == 6;
    case 7: return ov == 7;
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
    sprite.drawString("M-hold: home  SEL: menu", W / 2, H - 20);
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
  // pulsing OK hint
  if ((ms / 400) % 2) {
    sprite.setFont(&fonts::Font0);
    sprite.setTextColor(COL_TXT_BLACK, bg);
    sprite.drawString("press SELECT = OK", W / 2, 104);
  }
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
  sprite.drawString("IP:", 8, 102);
  sprite.setTextColor(COL_ONLINE, COL_BLACK);
  sprite.drawString(WiFi.isConnected() ? WiFi.localIP().toString().c_str() : "No Wi-Fi", 28, 102);

  // Footer
  sprite.drawFastHLine(0, 114, W, COL_SEP);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString("Press any button to return", W / 2, 122);
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
  if (timer_fired)      sprite.drawString("TIME!  SEL: clear", W / 2, 78);
  else if (timerRunning()) sprite.drawString("running  SEL: stop", W / 2, 78);
  else                  sprite.drawString("SEL: start  M: next", W / 2, 78);
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString("M-hold: back", 4, 120);
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
  // Only attempt SD read when new art is signaled (dirty) or throttled to 5s if missing,
  // to prevent rapid SPI pin toggling / piezo ticking every frame.
  static uint32_t s_last_art_check_ms = 0;
  if (spotifyArtDirty() || (!s_art_in_frame && (ms - s_last_art_check_ms >= 5000))) {
    s_last_art_check_ms = ms;
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

    // 2. Secondary fallback: SD card /art.jpg (if mounted)
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

  // 5. Track Title
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setFont(&fonts::Font2);
  sprite.setTextColor(COL_TXT);
  char titleBuf[24];
  const char* rawTitle = g_spotify.title[0] ? g_spotify.title : "No Track";
  strncpy(titleBuf, rawTitle, sizeof(titleBuf) - 1);
  titleBuf[sizeof(titleBuf) - 1] = 0;
  if (strlen(titleBuf) > 13) {
    titleBuf[11] = '.'; titleBuf[12] = '.'; titleBuf[13] = 0;
  }
  sprite.drawString(titleBuf, cardX + 8, cardY + 2);

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
  const char* curL = g_spotify.lyric_cur[0] ? g_spotify.lyric_cur : "♫ Music Playing ♫";
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

  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString("SEL: Cover    M: Exit", W / 2, 118);
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
      ui_latched_result = 0;
      s_had_active_print = false;
      view = VIEW_HOME;
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
      case VIEW_LEDLIST: view = VIEW_MENU; menu_focus = 1; break;
      case VIEW_CLOCK:   view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_MENU; menu_focus = 5; break;
      case VIEW_ABOUT:   view = VIEW_MENU; menu_focus = 8; break;
      case VIEW_TIMER:   view = (s_app_mode == MODE_SPOTIFY) ? VIEW_SPOTIFY : VIEW_MENU; menu_focus = 9; break;
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
      // Menu (1), Select (2), UP (6), DOWN (7), RIGHT (9) opens Settings from Home
      if (ev == 1 || ev == 2 || ev == 6 || ev == 7 || ev == 9) {
        view = VIEW_MENU;
        menu_focus = 0;
      }
      break;

    case VIEW_MENU:
      if (ev == 6) { // UP
        menu_focus = (menu_focus - 1 + MENU_ROWS) % MENU_ROWS;
      } else if (ev == 1 || ev == 7) { // DOWN
        menu_focus = (menu_focus + 1) % MENU_ROWS;
      } else if (ev == 2 || ev == 9) { // OK / RIGHT
        startBeepPattern(1); // confirmation beep
        switch (menu_focus) {
          case 0: {
            if (s_app_mode == MODE_PRINTER) {
              uiSetAppMode(MODE_SPOTIFY);
            } else {
              uiSetAppMode(MODE_PRINTER);
            }
            break;
          }
          case 1: view = VIEW_LEDLIST; menu_focus = 0; break;
          case 2: applySoundToggle(); break;
          case 3: {  // siren policy: Auto-Mute <-> Until OK
            g_settings.siren_mode = g_settings.siren_mode ? 0 : 1;
            settingsSave();
            break;
          }
          case 4: {  // brightness cycle
            uint8_t b = uiGetBacklight();
            b = (b >= 100) ? 40 : b + 30;
            uiSetBacklight(b);
            g_settings.brightness = b;
            settingsSave();
            break;
          }
          case 5: view = VIEW_CLOCK; break;
          case 6: { // Theme Dark/Light
            uiSetThemeLight(!uiGetThemeLight());
            break;
          }
          case 7: { // Clock Fmt 12H/24H
            uiSetClock12h(!uiGetClock12h());
            break;
          }
          case 8: view = VIEW_ABOUT; break;
          case 9: view = VIEW_TIMER; timer_sel = 0; break;
          case 10: {
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
      } else if (ev == 2 || ev == 9) { // OK / RIGHT
        startBeepPattern(1); // confirmation beep
        if (menu_focus == LEDLIST_ROWS - 1) { view = VIEW_MENU; menu_focus = 0; }
        else {
          ledSetOverride(menu_focus);   // 0..7 map directly
          g_settings.led_override = menu_focus;
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
          menu_focus = 5;
        }
      }
      break;

    case VIEW_ABOUT:
      if (ev != 0) {
        beepButton();
        view = VIEW_MENU;
        menu_focus = 8;
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

// ============================================================
//  UI TASK
// ============================================================
static void uiTask(void* /*param*/) {
  TickType_t last_wake = xTaskGetTickCount();
  const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / UI_TARGET_FPS);

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
  boot_start_ms = millis();
  if (options_sound_on()) {
    playHardwareBeep(BUZZER_FREQ_OK, 40); // 40ms synchronous boot chirp, guaranteed stops
  }
  Serial.println(F("[UI] boot splash started"));

  while (true) {
    uint32_t ms = millis();
    TelemetryData d;
    telemetryRead(d);

    if (view == VIEW_BOOT && ms - boot_start_ms >= BOOT_MS) {
      view = VIEW_HOME;
      startBeepPattern(1);   // boot-complete beep (verifies GPIO19 buzzer)
      Serial.println(F("[UI] boot done -> HOME (beep)"));
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
    // (suppressed during image mode: the pushed image owns the screen)
    if (ui_latched_result == 0 && !custom_image_active) {
      if (d.state == PST_FINISH && s_had_active_print && (d.percent >= 98 || (d.percent >= 95 && d.remain_min == 0))) {
        ui_latched_result = 1;
        view = VIEW_OVERLAY;
        s_had_active_print = false;
      }
      else if (d.state == PST_FAILED && s_had_active_print) {
        ui_latched_result = 2;
        view = VIEW_OVERLAY;
        s_had_active_print = false;
      }
    }

    // --- buttons (single-fire events from peripherals task) ---
    uint8_t ev = ui_button_event;
    if (ev) ui_button_event = 0;
    dispatchButtons(ev);

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
      bool fresh = (g_spotify.last_update_ms > 0 && (ms - g_spotify.last_update_ms < 3500));
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
    switch (view) {
      case VIEW_BOOT:    drawBootFrame(ms - boot_start_ms); break;
      case VIEW_HOME:    drawHomeFrame(d, ms); break;
      case VIEW_MENU:
        MENU_LABELS[0] = (s_app_mode == MODE_SPOTIFY) ? "Printer Mode" : "Spotify Mode";
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
      case VIEW_CUSTOM_IMAGE:
        if (custom_image_active) {
          static bool had_notif = false;
          bool has_notif = (millis() < notif_until_ms && notif_title[0]);
          if (!custom_image_rendered || has_notif || had_notif) {
            if (s_custom_image_buf) {
              sprite.pushImage(0, 0, W, H, s_custom_image_buf);
            }
            custom_image_rendered = true;
            had_notif = has_notif;
          } else {
            // Static image already drawn to screen; yield CPU and avoid 30 FPS DMA bus saturation
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
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

    if (has_notif) {
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

    vTaskDelayUntil(&last_wake, frame_ticks);
  }
}

void uiTaskStart() {
  xTaskCreatePinnedToCore(
      uiTask, "uiTask", TASK_STACK_UI, nullptr, 2, nullptr, 1);
}
