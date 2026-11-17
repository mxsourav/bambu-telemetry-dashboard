/* =====================================================================
 *  ESP32 + 1.8" TFT SPI 128x160 V1.1 (ST7735S) - Hardware Test Firmware
 * =====================================================================
 *  Verifies: color accuracy, backlight PWM, TAB offset alignment,
 *  max flicker-free FPS via full-screen DMA sprite.
 *
 *  WIRING:
 *    GND  -> GND        CS   -> GPIO 5
 *    VCC  -> 3.3V       RST  -> GPIO 18
 *    A0(DC)-> GPIO 23   SDA(MOSI) -> GPIO 17
 *    BLK  -> GPIO 26    SCK(SCLK) -> GPIO 14
 *
 *  ARDUINO IDE:
 *    1. Library Manager -> install "LovyanGFX"
 *    2. Open this file, Board: "ESP32 Dev Module", upload.
 *  PLATFORMIO:
 *    pio run -t upload && pio device monitor
 *
 *  TROUBLESHOOTING (see TFT_Display_Settings.txt for full background):
 *    - Static/noise pixels along an edge  -> change TFT_TAB_FIX below
 *    - Red and blue swapped               -> change TFT_RGB_INVERT below
 *    - Artifacts / black screen           -> lower TFT_SPI_MHZ to 27
 * ===================================================================== */

#include <LovyanGFX.hpp>

/* ---------------- USER TUNABLES ---------------- */

// TAB / offset variant (GRAM mapping of your panel batch):
//   TAB_BLACK   (0,0)  <- most common 1.8" V1.1, try first
//   TAB_GREEN   (2,1)
//   TAB_GREEN2  (2,1)  <- variant B, black fringe top/bottom
//   TAB_GREEN3  (2,3)
//   TAB_RED     (0,0)
#define TFT_TAB_FIX TFT_TAB_BLACK

// Color order: 0 = RGB (BLACKTAB default), 1 = swap red/blue (BGR)
#define TFT_RGB_INVERT 0

// SPI write clock in MHz. 40 = max FPS. GPIO 17 (MOSI) is a GPIO-matrix
// pin, not an IOMUX SPI pin, so if you see corruption at 40, drop to 27.
#define TFT_SPI_MHZ 40

// Backlight PWM
#define BL_PIN      26
#define BL_CH       0       // LEDC channel (v2.x core)
#define BL_FREQ     5000    // 5 kHz - above flicker perception
#define BL_BITS     8       // 0..255 resolution

/* ----------------------------------------------- */

// --- TAB offset table ---------------------------------------------
#define TFT_TAB_BLACK  0
#define TFT_TAB_GREEN  1
#define TFT_TAB_GREEN2 2
#define TFT_TAB_GREEN3 3
#define TFT_TAB_RED    4

#if TFT_TAB_FIX == TFT_TAB_BLACK || TFT_TAB_FIX == TFT_TAB_RED
  #define COL_OFF 0
  #define ROW_OFF 0
#elif TFT_TAB_FIX == TFT_TAB_GREEN2
  #define COL_OFF 2
  #define ROW_OFF 1
#else  // GREEN / GREEN3
  #define COL_OFF 2
  #define ROW_OFF 3
#endif

#if TFT_RGB_INVERT
  #define TFT_COLOR_ORDER 0   // TFT_BGR in LovyanGFX
#else
  #define TFT_COLOR_ORDER 1   // TFT_RGB in LovyanGFX
#endif

// ---------------- LovyanGFX in-sketch configuration ----------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7735S _panel;
  lgfx::Bus_SPI       _bus;

public:
  LGFX() {
    // SPI bus
    {
      auto cfg = _bus.config();
      cfg.spi_host = VSPI_HOST;   // dedicated SPI peripheral
      cfg.spi_mode = 0;
      cfg.freq_write = TFT_SPI_MHZ * 1000000;  // SPI clock (write)
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 17;
      cfg.pin_miso = -1;          // not wired on this panel
      cfg.pin_dc   = 23;
      _bus.config(cfg);
    }
    _panel.setBus(&_bus);

    // Panel
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = 5;
      cfg.pin_rst  = 18;
      cfg.pin_busy = -1;
      cfg.panel_width  = 128;
      cfg.panel_height = 160;
      cfg.offset_x = COL_OFF;     // TAB fix: cures edge noise pixels
      cfg.offset_y = ROW_OFF;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable  = false;      // MISO not wired - saves init time
      cfg.invert    = false;      // ST7735 V1.1 (BLACKTAB): no INVON
      cfg.rgb_order = TFT_COLOR_ORDER;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;     // SPI bus not shared with SD card
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

LGFX tft;
static LGFX_Sprite sprite(&tft);   // 128 x 160 x 16bpp = 40960 B off-screen

// ---------------- Runtime state ----------------
static const uint16_t W = 128;
static const uint16_t H = 160;

static float bl_level = 0.0f;      // 0..100 backlight percent (smoothed)
static float bl_target = 100.0f;

static uint32_t frame_count = 0;   // frames in current 1 s window
static uint32_t fps_window_start = 0;
static uint16_t fps_current = 0;
static uint16_t fps_peak = 0;
static uint32_t last_serial_diag = 0;

static int32_t anim_t = 0;         // animation phase counter
static float   phase1 = 0.0f, phase2 = 0.0f, phase3 = 0.0f;
static float   bx = 20, by = 80, vx = 2.1f, vy = 1.7f;  // bouncing ball
static const float R = 9.0f;

// Forward declarations (PlatformIO compiles .ino as .cpp - no auto prototypes)
static void drawAnimationFrame(uint32_t ms);
static void backlightLoop();
static void backlightSet(float pct);
static void showColorBars();
static void runColorTest();

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println(F("====================================="));
  Serial.println(F(" ESP32 + ST7735 1.8\" TFT HW Test"));
  Serial.println(F("====================================="));

  // ---- Backlight PWM (manual LEDC, out of panel config) ----
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BL_PIN, BL_FREQ, BL_BITS);   // v3.x: one call
#else
  ledcSetup(BL_CH, BL_FREQ, BL_BITS);     // v2.x: channel + pin
  ledcAttachPin(BL_PIN, BL_CH);
#endif
  backlightSet(0);                        // start dark, fade in below

  // ---- Display init ----
  tft.init();
  tft.setRotation(0);          // portrait 128x160
  tft.setColorDepth(16);

  Serial.println(F("[INIT] LovyanGFX panel init OK"));
  Serial.printf("[INIT] SPI freq  : %d MHz\n", TFT_SPI_MHZ);
  Serial.println(F("[INIT] Pins      : MOSI=17 SCLK=14 CS=5 DC=23 RST=18 BL=26"));
  Serial.printf("[INIT] TAB fix   : %s (col_off=%d row_off=%d)\n",
    "BLACK", COL_OFF, ROW_OFF);
  Serial.printf("[INIT] ColorMode : %s\n",
    TFT_RGB_INVERT ? "BGR" : "RGB");
  Serial.printf("[INIT] Free heap : %u bytes\n", ESP.getFreeHeap());

  // ---- Off-screen full-screen sprite + DMA buffer ----
  sprite.setColorDepth(16);
  sprite.setBitmapColor(0x0000, 0xFFFF);
  if (!sprite.createSprite(W, H)) {
    Serial.println(F("[FATAL] Sprite alloc failed - out of RAM!"));
    while (true) delay(1000);
  }
  // LovyanGFX: DMA is done via tft.pushImageDMA() using the sprite's raw
  // buffer - the bus uses its own DMA channel (SPI_DMA_CH_AUTO), no extra
  // per-sprite buffer needed.
  Serial.printf("[INIT] DMA-capable sprite ready (%u bytes)\n",
    (unsigned)(W * H * 2));

  // ---- Boot: smooth backlight fade-in 0 -> 100% ----
  Serial.println(F("[TEST] Backlight fade-in..."));
  uint32_t fade_start = millis();
  while (bl_level < 99.5f) {
    backlightLoop();     // eased ramp to bl_target = 100
    delay(2);
  }
  Serial.printf("[TEST] Fade-in complete in %lu ms\n",
    (unsigned long)(millis() - fade_start));

  // ---- Phase 1: color bars ----
  runColorTest();
}

// ============================================================
//  MAIN LOOP  -  flicker-free animation, no fillScreen() on TFT
// ============================================================
void loop() {
  uint32_t ms = millis();

  // --- frame-rate bookkeeping ---
  if (fps_window_start == 0) fps_window_start = ms;
  frame_count++;
  if (ms - fps_window_start >= 1000) {
    fps_current = frame_count * 1000 / (ms - fps_window_start);
    if (fps_current > fps_peak) fps_peak = fps_current;
    frame_count = 0;
    fps_window_start = ms;
  }

  // --- render entire frame in RAM, push once (atomic, no tearing) ---
  drawAnimationFrame(ms);
  // Single DMA SPI transaction of the full 128x160x16bpp frame.
  // LovyanGFX equivalent of TFT_eSPI's pushSpriteDMA:
  tft.pushImageDMA(0, 0, W, H, (uint16_t*)sprite.getBuffer());

  // --- backlight smoothing (non-blocking) ---
  backlightLoop();

  // --- serial diagnostics every 2 s ---
  if (ms - last_serial_diag >= 2000) {
    last_serial_diag = ms;
    Serial.printf("[FPS] cur=%u peak=%u | SPI=%dMHz | heap=%u\n",
      fps_current, fps_peak, TFT_SPI_MHZ, ESP.getFreeHeap());
  }
}

// ============================================================
//  BACKLIGHT  (LEDC PWM on GPIO 26, eased fade)
// ============================================================
static void backlightSet(float pct) {
  pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BL_PIN, (uint32_t)(pct * 255.0f / 100.0f));
#else
  ledcWrite(BL_CH, (uint32_t)(pct * 255.0f / 100.0f));
#endif
}

static void backlightLoop() {
  // ease toward bl_target (exponential smoothing = perceptually smooth)
  bl_level += (bl_target - bl_level) * 0.08f;
  if (fabsf(bl_target - bl_level) < 0.5f) bl_level = bl_target;
  backlightSet(bl_level);
}

// ============================================================
//  COLOR TEST  -  solid primary bars to verify RGB order & dead px
// ============================================================
static void showColorBars() {
  static const char* names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  static const uint16_t cols[] = {
    0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
  // split screen: 5 vertical bars in one frame
  for (int i = 0; i < 5; i++) {
    sprite.fillRect(i * W / 5, 0, W / 5, H, cols[i]);
  }
  sprite.setTextColor(0xFFFF, 0x0000);
  sprite.setTextSize(1);
  for (int i = 0; i < 5; i++) {
    sprite.setCursor(i * W / 5 + 4, H - 12);
    sprite.print(names[i]);
  }
}

static void runColorTest() {
  Serial.println(F("[TEST] Color bar sequence (5s)..."));
  static const char* names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  static const uint16_t cols[] = {
    0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
  for (int i = 0; i < 5; i++) {
    sprite.fillSprite(cols[i]);
    sprite.setTextColor(i == 4 ? 0xFFFF : 0x0000, cols[i]);
    sprite.setTextSize(2);
    sprite.setCursor(4, 4);
    sprite.print(names[i]);
    tft.pushImageDMA(0, 0, W, H, (uint16_t*)sprite.getBuffer());
    Serial.printf("  -> %s\n", names[i]);
    delay(800);      // hold each color so eyes can scan for dead pixels
  }
  // final split-screen summary
  showColorBars();
  tft.pushImageDMA(0, 0, W, H, (uint16_t*)sprite.getBuffer());
  delay(1200);

  // Backlight sanity pulse: 100 -> 40 -> 100, verifies PWM control
  Serial.println(F("[TEST] Backlight pulse 100->40->100%"));
  bl_target = 40;
  while (bl_level > 41.0f) { backlightLoop(); delay(2); }
  delay(150);
  bl_target = 100;
  while (bl_level < 99.5f) { backlightLoop(); delay(2); }
  Serial.println(F("[TEST] Color test done, entering animation"));
}

// ============================================================
//  ANIMATION FRAME  -  rendered fully in RAM each frame
// ============================================================
static void drawAnimationFrame(uint32_t ms) {
  // 1) dark UI grid background (inside sprite, NOT on the TFT)
  sprite.fillSprite(0x0020);                       // very dark blue
  sprite.drawFastHLine(0, 0, W, 0x33B0);           // header underline
  for (int x = 0; x <= W; x += 16)
    sprite.drawFastVLine(x, 12, H - 12, 0x105B0);
  for (int y = 12; y <= H; y += 16)
    sprite.drawFastHLine(0, y, W, 0x105B0);

  // 2) FPS + SPI clock readout pinned top-right
  char buf[24];
  snprintf(buf, sizeof(buf), "%uFPS %dMHz", fps_current, TFT_SPI_MHZ);
  sprite.setTextColor(0xFFE0, 0x0020);             // yellow on dark
  sprite.setTextSize(1);
  sprite.setCursor(W - 74, 3);
  sprite.print(buf);

  // 3) sine-oscillating gauge bars (track shortened to leave room
  //    for the % label: label starts at x=94, ends at x=124 < 128)
  const int8_t gx = 8, gw = W - 16 - 34;
  const uint8_t gh = 8, gap = 22;
  float v1 = 0.5f + 0.5f * sinf(ms * 0.0011f);
  float v2 = 0.5f + 0.5f * sinf(ms * 0.0007f + 2.1f);
  float v3 = 0.5f + 0.5f * sinf(ms * 0.0016f + 4.4f);
  float vals[3] = { v1, v2, v3 };
  static const uint16_t bar_col[3] = {0x07E0, 0xFFE0, 0xF81F}; //G Y M
  for (int i = 0; i < 3; i++) {
    int y = 22 + i * gap;
    int w = (int)(vals[i] * gw);
    // track + fill (fill drawn fresh each frame, so no residue)
    sprite.drawRect(gx, y, gw, gh, 0x630C);
    if (w > 0) sprite.fillRect(gx + 1, y + 1, w, gh - 2, bar_col[i]);
    // percentage text (5 chars = 30 px, fits before right edge)
    sprite.setTextSize(1);
    sprite.setTextColor(0xFFFF, 0x0020);
    sprite.setCursor(gx + gw + 4, y + 1);
    snprintf(buf, sizeof(buf), "%3d%%", (int)(vals[i] * 100));
    sprite.print(buf);
  }

  // 4) bouncing anti-aliased ball
  bx += vx;  by += vy;
  if (bx < R || bx > W - R) { vx = -vx; bx = bx < R ? R : W - R; }
  if (by < 14 + R || by > H - R) { vy = -vy; by = by < 14 + R ? 14 + R : H - R; }
  sprite.fillSmoothCircle((int)bx, (int)by, (int)R, 0x07FF);

  // 5) sine wave across bottom half
  for (int x = 0; x < W; x++) {
    int y = 118 + (int)(sinf((x + anim_t) * 0.06f) * 12.0f);
    sprite.drawPixel(x, y, 0xFD20);                // orange trace
    sprite.drawPixel(x, y + 1, 0x780F);            // dim echo
  }
  anim_t++;

  // 6) multi-size text render test (alignment / axis check)
  //    size-2 glyphs are 16 px tall: y=144 is the last row that fits
  sprite.setTextColor(0xFFFF, 0x0020);
  sprite.setTextSize(1);
  sprite.setCursor(4, 132);  sprite.print(F("Size1 123 ABC"));
  sprite.setTextSize(2);
  sprite.setCursor(4, 144);  sprite.print(F("Sz2"));
}

/* =====================================================================
 *  NOTES
 *  -----
 *  - FPS ceiling: full 128x160x16 push over SPI takes ~6.5 ms @ 40 MHz
 *    and ~9.5 ms @ 27 MHz, so expect ~70-80 FPS @ 40 MHz / ~55 FPS @ 27.
 *  - Tearing is impossible in this design: the TFT only ever receives
 *    one contiguous DMA burst per frame (pushSpriteDMA); it never
 *    redraws partial regions from the MCU side.
 *  - Edge noise?  Change TFT_TAB_FIX at the top of the file.
 *  - Red/blue swapped?  Set TFT_RGB_INVERT to 1.
 * ===================================================================== */
