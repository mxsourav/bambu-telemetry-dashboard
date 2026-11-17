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
#include "telemetry.h"
#include "peripherals.h"
#include "boot_splash.h"
#include "status_logo.h"

#include <LovyanGFX.hpp>
#include <time.h>

volatile uint8_t ui_button_event = 0;
volatile uint32_t ui_button_hold_start_ms = 0;

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
    pc.offset_x = 0;
    pc.offset_y = 0;
    pc.offset_rotation = 0;
    pc.dummy_read_pixel = 8;
    pc.dummy_read_bits  = 1;
    pc.readable  = false;
    pc.invert    = false;
    pc.rgb_order = TFT_RGB_INVERT ? 0 : 1;
    pc.dlen_16bit = false;
    pc.bus_shared = false;
    _panel.config(pc);
    setPanel(&_panel);
  }
};

static LGFX tft;
static LGFX_Sprite buf0(&tft);
static LGFX_Sprite buf1(&tft);
static LGFX_Sprite* back = &buf0;
#define sprite (*back)
#define W 160
#define H 128
#define BOOT_MS 5000

// ---------------- Palette (Apple dark mode) ---------------------------
#define COL_BLACK     0x0000
#define COL_CARD      0x18E3
#define COL_SEP       0x1082
#define COL_TXT       0xFFFF
#define COL_TXT_BLACK 0x0000
#define COL_MUTED     0x6B4D
#define COL_ONLINE    0x07E0
#define COL_OFFLINE   0xF800
#define COL_CONNECT   0xFFE0
#define COL_CYAN      0x04FF
#define COL_AMBER     0xFD20
#define COL_SALMON    0xFB68
#define COL_LBLUE     0x7DDF
#define COL_YELLOW    0xFF00   // paused overlay

// ---------------- Connection state ------------------------------------
enum ConnState : uint8_t { CONN_OFFLINE = 0, CONN_CONNECTING, CONN_ONLINE };
static ConnState connStateOf(const TelemetryData& d) {
  if (d.state == PST_DISCONNECTED) {
    extern bool network_ever_connected;
    return network_ever_connected ? CONN_CONNECTING : CONN_OFFLINE;
  }
  return CONN_ONLINE;
}

// ---------------- View state ------------------------------------------
enum UiView : uint8_t { VIEW_BOOT = 0, VIEW_HOME, VIEW_MENU, VIEW_LEDLIST,
                        VIEW_CLOCK, VIEW_TIMER, VIEW_OVERLAY };
static UiView view = VIEW_BOOT;
static uint32_t boot_start_ms = 0;

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
#define MENU_ROWS      7
#define LEDLIST_ROWS   9

static const char* MENU_LABELS[MENU_ROWS] = {
  "LED Control", "Sound", "Brightness", "Clock", "Timer", "Reset", "Back" };

static const char* LEDLIST_LABELS[LEDLIST_ROWS] = {
  "Auto-Sync", "OFF", "RAINBOW", "RED", "GREEN", "BLUE", "WHITE",
  "AMBER", "Back" };

static void applySoundToggle();

// ============================================================
//  BOOT SCREEN
// ============================================================
static void drawBootFrame(uint32_t ms) {
  sprite.fillSprite(COL_TXT);
  sprite.pushImage((W - STAN_W) / 2, 38, STAN_W, STAN_H, stan_bits);
  sprite.setTextDatum(textdatum_t::middle_center);
  sprite.setFont(&fonts::FreeSans9pt7b);
  sprite.setTextColor(COL_TXT_BLACK, COL_TXT);
  int32_t word_w = sprite.textWidth("studio.");
  sprite.drawString("studio.", W / 2, 88);
  int32_t stem_w = sprite.textWidth("studio");
  sprite.fillCircle(W / 2 + stem_w - word_w / 2 + 4, 88, 2, 0xF800);
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
    sprite.setTextColor(focused ? COL_TXT : (active ? COL_CYAN : COL_TXT),
                        focused ? 0x2129 : COL_BLACK);
    sprite.drawString(labels[idx], 10, y + row_h / 2);
    // right-hand value/indicator
    if (valueFn) {
      const char* val = valueFn(idx);
      if (val) {
        sprite.setTextDatum(textdatum_t::middle_right);
        sprite.setTextColor(focused ? COL_CYAN : COL_MUTED,
                            focused ? 0x2129 : COL_BLACK);
        sprite.drawString(val, W - 10, y + row_h / 2);
      }
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
  static char buf[12];
  switch (i) {
    case 1: return options_sound_on() ? "On" : "Off";
    case 3: {  // clock preview
      struct tm t;
      if (getLocalTime(&t, 0)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
        return buf;
      }
      return "--:--";
    }
    case 4: {  // timer status
      if (!timerRunning()) return "Off";
      int left = (timer_end_ms - millis()) / 1000;
      snprintf(buf, sizeof(buf), "%dm%02ds", left / 60, left % 60);
      return buf;
    }
    case 5: return reset_arm ? "SURE?" : "";
    default: return nullptr;
  }
}

static bool menuActive(int i) {
  switch (i) {
    case 1: return options_sound_on();
    case 4: return timerRunning();
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
  ConnState conn = connStateOf(d);
  for (int i = 0; i < 4; i++) {
    int bh = 2 + i * 2;
    uint16_t c = (conn == CONN_ONLINE && i < 3) ? COL_CYAN : COL_MUTED;
    sprite.fillRect(4 + i * 3, 9 + 4 - bh, 2, bh, c);
  }
  uint16_t st_col = (conn == CONN_ONLINE) ? COL_ONLINE
                    : (conn == CONN_CONNECTING) ? COL_CONNECT : COL_OFFLINE;
  const char* st = (conn == CONN_ONLINE) ? printStateName(d.state)
                   : (conn == CONN_CONNECTING) ? "CONNECTING" : "OFFLINE";
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::middle_left);
  sprite.setTextColor(st_col, COL_BLACK);
  sprite.setCursor(24, 9);
  sprite.print(st);
  sprite.pushImage(W - LOGO_W - 4, (18 - LOGO_H) / 2, LOGO_W, LOGO_H,
                   logo_bits, 0x0001);
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
  sprite.drawString("PROGRESS", cx, cy + 15);

  // ---- right metric cards ----
  const char* tags[3]   = {"NOZZLE", "BED", "TIME"};
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
    sprite.fillRoundRect(83, y, 76, 27, 6, COL_CARD);
    sprite.setFont(&fonts::Font0);
    sprite.setTextDatum(textdatum_t::top_left);
    sprite.setTextColor(tcol[i], COL_CARD);
    sprite.drawString(tags[i], 86, y + 3);
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.setTextDatum(textdatum_t::bottom_right);
    sprite.setTextColor(COL_TXT, COL_CARD);
    sprite.drawString(vals[i], 154, y + 24);
  }

  sprite.drawFastHLine(0, 112, W, COL_SEP);

  // ---- footer: job + filament + timer + hint ----
  sprite.setFont(&fonts::Font0);
  sprite.setTextDatum(textdatum_t::top_left);
  sprite.setTextColor(COL_TXT, COL_BLACK);
  const char* job = (d.state == PST_RUNNING || d.state == PST_PREPARE)
                      ? "Printing..." : "Idle";   // "A1: " prefix collided with timer
  sprite.drawString(job, 4, 114);

  // timer countdown (left-center of footer when armed)
  if (timerRunning()) {
    int left = (int)((timer_end_ms - ms) / 1000);
    if (left < 0) left = 0;
    snprintf(buf, sizeof(buf), "T-%dm%02ds", left / 60, left % 60);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setTextColor(timer_fired ? ((ms / 300) % 2 ? COL_AMBER : COL_TXT)
                                    : COL_LBLUE, COL_BLACK);
    sprite.drawString(buf, W / 2, 120);
  }

  // filament bottom-right
  char fbuf[12] = "--";
  if (d.filament_g >= 0)
    snprintf(fbuf, sizeof(fbuf), "%dg", (int)(d.filament_g + 0.5f));
  sprite.setTextDatum(textdatum_t::top_right);
  sprite.setTextColor(d.filament_g >= 0 ? COL_CYAN : COL_MUTED, COL_BLACK);
  sprite.drawString(fbuf, 156, 114);
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
//  CLOCK / TIMER screens
// ============================================================
static void drawClockFrame(uint32_t ms) {
  (void)ms;
  sprite.fillSprite(COL_BLACK);
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setFont(&fonts::FreeSansBold18pt7b);   // 24pt "23:59:59" = 180px > 160
    sprite.setTextColor(COL_TXT, COL_BLACK);
    sprite.drawString(buf, W / 2, H / 2 - 6);
    sprite.setFont(&fonts::Font0);
    sprite.setTextColor(COL_MUTED, COL_BLACK);
    sprite.drawString("IST  (NTP)", W / 2, H / 2 + 22);
  } else {
    sprite.setTextDatum(textdatum_t::middle_center);
    sprite.setFont(&fonts::FreeSansBold12pt7b);
    sprite.setTextColor(COL_MUTED, COL_BLACK);
    sprite.drawString("no NTP sync", W / 2, H / 2);
  }
  sprite.setFont(&fonts::Font0);
  sprite.setTextColor(COL_MUTED, COL_BLACK);
  sprite.drawString("M-hold: back", 4, 120);
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

// ============================================================
//  BUTTON DISPATCH
// ============================================================
static void dispatchButtons(uint8_t ev) {
  if (!ev) return;

  // overlay: select = OK (clear latch), any menu action also exits
  if (view == VIEW_OVERLAY) {
    if (ev == 2 || ev == 5 || ev == 4) {
      ui_latched_result = 0;
      view = VIEW_HOME;
    }
    return;
  }

  // menu double-press = HOME from anywhere
  if (ev == 4) { view = VIEW_HOME; reset_arm = 0; return; }
  // menu long = back one level
  if (ev == 3) {
    switch (view) {
      case VIEW_MENU:    view = VIEW_HOME; break;
      case VIEW_LEDLIST: view = VIEW_MENU; break;
      case VIEW_CLOCK:
      case VIEW_TIMER:   view = VIEW_MENU; break;
      default:           view = VIEW_HOME; break;
    }
    reset_arm = 0;
    return;
  }

  switch (view) {
    case VIEW_HOME:
      if (ev == 1) { view = VIEW_MENU; menu_focus = 0; }
      break;

    case VIEW_MENU:
      if (ev == 1) menu_focus = (menu_focus + 1) % MENU_ROWS;
      else if (ev == 2) {
        switch (menu_focus) {
          case 0: view = VIEW_LEDLIST; menu_focus = 0; break;
          case 1: applySoundToggle(); break;
          case 2: {  // brightness cycle 40/60/80/100
            uint8_t b = uiGetBacklight();
            b = (b >= 100) ? 40 : b + 20;
            uiSetBacklight(b);
            break;
          }
          case 3: view = VIEW_CLOCK; break;
          case 4: view = VIEW_TIMER; timer_sel = 0; break;
          case 5:  // reset: double-press arm
            if (reset_arm) ESP.restart();
            reset_arm = 1;
            break;
          case 6: view = VIEW_HOME; break;
        }
      }
      break;

    case VIEW_LEDLIST:
      if (ev == 1) menu_focus = (menu_focus + 1) % LEDLIST_ROWS;
      else if (ev == 2) {
        if (menu_focus == LEDLIST_ROWS - 1) { view = VIEW_MENU; menu_focus = 0; }
        else ledSetOverride(menu_focus);   // 0..7 map directly
      }
      break;

    case VIEW_CLOCK:
      if (ev == 2) view = VIEW_MENU;
      break;

    case VIEW_TIMER:
      if (timer_fired) {
        if (ev == 2 || ev == 5) { timer_fired = false; timer_end_ms = 0; }
      } else if (timerRunning()) {
        if (ev == 2) timer_end_ms = 0;    // stop
      } else {
        if (ev == 1) timer_sel = (timer_sel + 1) % TIMER_OPT_COUNT;
        else if (ev == 2) {
          timer_end_ms = millis() + TIMER_OPTS[timer_sel] * 60000UL;
          timer_fired = false;
          view = VIEW_HOME;
        }
      }
      break;
  }
}

static void applySoundToggle() {
  setSoundEnabled(!options_sound_on());
}

// ---------------- Backlight control (menu Brightness row) --------------
static uint8_t bl_pct = 100;
uint8_t uiGetBacklight() { return bl_pct; }
void uiSetBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  bl_pct = pct;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_BL, (uint32_t)(pct * 255 / 100));
#else
  ledcWrite(0, (uint32_t)(pct * 255 / 100));
#endif
}

// ============================================================
//  UI TASK
// ============================================================
static void uiTask(void* /*param*/) {
  TickType_t last_wake = xTaskGetTickCount();
  const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / UI_TARGET_FPS);

  tft.init();
  tft.setRotation(1);
  tft.setColorDepth(16);
  tft.setBrightness(255);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_BL, 255);
#else
  ledcWrite(0, 255);
#endif
  Serial.println(F("[UI] panel init OK (landscape 160x128) @ full brightness"));

  sprite.setColorDepth(16);
  if (!buf0.createSprite(W, H) || !buf1.createSprite(W, H)) {
    Serial.println(F("[FATAL] sprite alloc failed"));
    vTaskDelete(nullptr);
  }
  buf1.fillSprite(0xFFFF);
  boot_start_ms = millis();

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

    // --- latch: FINISH/FAILED -> full-screen overlay until OK ---
    if (ui_latched_result == 0) {
      if (d.state == PST_FINISH) { ui_latched_result = 1; view = VIEW_OVERLAY; }
      else if (d.state == PST_FAILED) { ui_latched_result = 2; view = VIEW_OVERLAY; }
    }

    // --- buttons (single-fire events from peripherals task) ---
    uint8_t ev = ui_button_event;
    if (ev) ui_button_event = 0;
    dispatchButtons(ev);

    // --- render ---
    switch (view) {
      case VIEW_BOOT:    drawBootFrame(ms - boot_start_ms); break;
      case VIEW_HOME:    drawHomeFrame(d, ms); break;
      case VIEW_MENU:    drawList("SETTINGS", MENU_LABELS, MENU_ROWS,
                                  menu_focus, menuValue, menuActive); break;
      case VIEW_LEDLIST: drawList("LED CONTROL", LEDLIST_LABELS, LEDLIST_ROWS,
                                  menu_focus, ledListValue, ledListActive); break;
      case VIEW_CLOCK:   drawClockFrame(ms); break;
      case VIEW_TIMER:   drawTimerFrame(ms); break;
      case VIEW_OVERLAY: drawOverlayFrame(d, ms); break;
    }

    // --- push: one burst when changed, nothing when identical ---
    LGFX_Sprite* front = (back == &buf0) ? &buf1 : &buf0;
    if (memcmp(front->getBuffer(), back->getBuffer(), W * H * 2) != 0) {
      tft.pushImageDMA(0, 0, W, H, (uint16_t*)back->getBuffer());
      tft.waitDMA();
      back = front;
    }

    vTaskDelayUntil(&last_wake, frame_ticks);
  }
}

void uiTaskStart() {
  xTaskCreatePinnedToCore(
      uiTask, "uiTask", TASK_STACK_UI, nullptr, 2, nullptr, 1);
}
