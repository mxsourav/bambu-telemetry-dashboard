#include "peripherals.h"
#include "config.h"
#include "telemetry.h"
#include "ui.h"

#include <Arduino.h>
// sound toggle shared with UI options menu (0=on 1=off)
static volatile bool sound_on = true;
void setSoundEnabled(bool on) { sound_on = on; }
bool options_sound_on() { return sound_on; }

// ---------------- Button engine: state machine per button -------------
// Guarantees: exactly ONE event per press (no ghosting), NO repeats while
// held, LONG fires once at 600 ms hold, DOUBLE fires on quick release-
// release (menu button only). Fast: debounce only 20 ms.
struct BtnState {
  uint8_t  pin;
  bool     raw_high;        // last debounced level (true = released)
  uint32_t last_edge_ms;    // last debounced edge time
  bool     long_fired;      // LONG already emitted this hold
  bool     press_emitted;   // PRESS already emitted this hold
  uint32_t last_release_ms; // for double-press detection
};

static BtnState b_menu   {PIN_BTN_MENU,   true, 0, false, false, 0};
static BtnState b_select {PIN_BTN_SELECT, true, 0, false, false, 0};

// event codes (match ui.h): 1=menu 2=select 3=menuLong 4=menuDouble 5=selLong
static uint8_t buttonUpdate(BtnState& b, uint32_t ms) {
  bool now_high = digitalRead(b.pin);

  // --- level change: start debounce window ---
  if (now_high != b.raw_high) {
    if (ms - b.last_edge_ms >= BTN_DEBOUNCE_MS) {
      b.raw_high = now_high;
      b.last_edge_ms = ms;
      if (!now_high) {                     // ---- pressed ----
        b.long_fired = false;
        b.press_emitted = false;
      } else {                             // ---- released ----
        uint8_t ev = 0;
        if (&b == &b_menu &&
            b.last_release_ms != 0 &&
            ms - b.last_release_ms <= BTN_DOUBLE_MS &&
            !b.long_fired) {
          ev = 4;                          // double-press (menu)
          b.last_release_ms = 0;           // consume
        } else {
          b.last_release_ms = ms;
        }
        if (b.press_emitted && !b.long_fired && ev == 0) {
          ev = (&b == &b_menu) ? 1 : 2;    // normal short press
        }
        return ev;
      }
    }
  }

  // --- hold: fire LONG once at timeout (suppresses the short press) ---
  if (!b.raw_high && !b.long_fired &&
      ms - b.last_edge_ms >= BTN_LONG_MS) {
    b.long_fired = true;
    return (&b == &b_menu) ? 3 : 5;
  }
  return 0;
}

// ---------------- Buzzer: LEDC tone, non-blocking ---------------------
// Patterns are time-based state machines evaluated in this task's loop;
// no delay() calls anywhere.
static uint32_t siren_last_toggle_ms = 0;
static bool     siren_high = false;
static bool     siren_active = false;

static void toneStart(uint32_t freq) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_LEDC_CH, freq);
  ledcWrite(BUZZER_LEDC_CH, 128);          // 50% duty
#else
  ledcWriteTone(BUZZER_LEDC_CH, freq);
#endif
}
static void toneStop() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER_LEDC_CH, 0);
#else
  ledcWrite(BUZZER_LEDC_CH, 0);
#endif
}

// One-shot patterns: beep(timeout) and doubleBeep - sequenced by ms ticks
static uint8_t  beep_seq = 0;          // 0=idle, 1..n = step counter
static uint32_t beep_step_ms = 0;

void startBeepPattern(uint8_t pattern) {  beep_seq = pattern;
  beep_step_ms = millis();
  toneStart(BUZZER_FREQ_OK);
}

static void updateBeepPattern() {
  if (!beep_seq) return;
  uint32_t t = millis() - beep_step_ms;
  switch (beep_seq) {
    case 1:  // single beep: 200 ms on
      if (t >= 200) { toneStop(); beep_seq = 0; }
      break;
    case 2:  // double beep: 200 on, 100 off, 200 on
      if (t >= 200 && t < 300)      toneStop();
      else if (t >= 300 && t < 500) toneStart(BUZZER_FREQ_OK);
      else if (t >= 500)            { toneStop(); beep_seq = 0; }
      break;
  }
}

// ---------------- Analog RGB strip (common anode, LEDC PWM) -----------
// HSV -> RGB for the smooth rainbow fade (avoids linear RGB artifacts).
static uint16_t rainbow_hue = 0;      // 0..255, incremented ~100 Hz
static uint32_t led_flash_last_ms = 0;
static bool     led_flash_on = false;

// LED soft-start: current draw of the strip is RAMPED to targets instead
// of stepped - sudden 100%->0 jumps are what sag the brick and brownout
// the ESP32. A ~250 ms exponential glide keeps peak dI/dt tiny.
static uint8_t cur_r = 0, cur_g = 0, cur_b = 0;   // live PWM duty
static bool     ramp_inited = false;

static void ledApply(uint8_t r, uint8_t g, uint8_t b) {
  // scale to brightness cap (limits brick current; 255 => LED_MAX_BRIGHTNESS)
  r = (uint16_t)r * LED_MAX_BRIGHTNESS / 255;
  g = (uint16_t)g * LED_MAX_BRIGHTNESS / 255;
  b = (uint16_t)b * LED_MAX_BRIGHTNESS / 255;
  if (!ramp_inited) {                 // first call: snap (boot)
    cur_r = r; cur_g = g; cur_b = b;
    ramp_inited = true;
  } else {                            // glide ~25%/tick toward target
    cur_r += (r - cur_r) >> 2;   // >>2 = 25% step (fast but smooth)
    cur_g += (g - cur_g) >> 2;
    cur_b += (b - cur_b) >> 2;
  }
  ledcWrite(LEDC_CH_R, cur_r);
  ledcWrite(LEDC_CH_G, cur_g);
  ledcWrite(LEDC_CH_B, cur_b);
}

static void hsvToRgb(uint16_t h, uint8_t s, uint8_t v,
                     uint8_t& r, uint8_t& g, uint8_t& b) {
  uint8_t region = (h * 6) >> 8;            // 0..5
  uint8_t rem    = (h * 6) & 0xFF;          // position in region
  uint8_t p = (v * (255 - s)) >> 8;
  uint8_t q = (v * (255 - ((s * rem) >> 8))) >> 8;
  uint8_t t = (v * (255 - ((s * (255 - rem)) >> 8))) >> 8;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

// Menu override state (set from UI via ledSetOverride):
// 0 = auto (follow MQTT state), 1 = forced off, 2 = manual rainbow,
// 3..7 = forced preset colors (R,G,B,W,Amber)
static volatile uint8_t led_override = 0;

void ledSetOverride(uint8_t mode) { led_override = mode; }
uint8_t ledGetOverride() { return led_override; }

static void ledStateMachine(const TelemetryData& d, uint32_t ms) {
  // 1) latched result: FINISH green / FAILED red stays until user OKs it
  //    (MQTT may return to IDLE immediately - latch must outlive it)
  if (ui_latched_result == 1) { ledApply(0, 255, 0); return; }
  if (ui_latched_result == 2) { ledApply(255, 0, 0); return; }

  // 2) forced presets (menu): off / rainbow / colors
  if (led_override == 1) { ledApply(0, 0, 0); return; }
  if (led_override == 2) {              // manual rainbow whenever idle
    rainbow_hue += 1;
    uint8_t r, g, b;
    hsvToRgb(rainbow_hue, 255, 180, r, g, b);
    ledApply(r, g, b);
    return;
  }
  static const uint8_t preset[5][3] = {
    {255,0,0},{0,255,0},{0,0,255},{255,255,255},{255,180,0}};
  if (led_override >= 3 && led_override <= 7) {
    const uint8_t* c = preset[led_override - 3];
    ledApply(c[0], c[1], c[2]);
    return;
  }

  // timelapse / layer change: stark white (stg_cur 26 = timelapse stage)
  if (d.state == PST_TIMELAPSE || d.stg_cur == 26) {
    ledApply(255, 255, 255);
    return;
  }
  // error: hms array non-empty => stark red flash @500 ms, overrides all
  if (d.hms_count > 0) {
    if (ms - led_flash_last_ms >= 500) {
      led_flash_last_ms = ms;
      led_flash_on = !led_flash_on;
    }
    ledApply(led_flash_on ? 255 : 0, 0, 0);
    return;
  }
  switch (d.state) {
    case PST_RUNNING: {
      // buttery rainbow: HSV hue cycle @ ~100 Hz => ~24 s per revolution
      rainbow_hue += 1;
      uint8_t r, g, b;
      hsvToRgb(rainbow_hue, 255, 180, r, g, b);
      ledApply(r, g, b);
      break;
    }
    case PST_FINISH:  ledApply(0, 255, 0);   break;  // solid green
    case PST_FAILED:  ledApply(255, 0, 0);   break;  // solid red
    case PST_PAUSED:  ledApply(255, 210, 0); break;  // YELLOW = paused
    case PST_PREPARE: ledApply(0, 0, 180);   break;  // dim blue = working
    default:          ledApply(LED_IDLE_R, LED_IDLE_G, LED_IDLE_B);
                      break;  // firoza = powered-on idle signature
  }
}

// ---------------- Peripheral task body --------------------------------
static void peripheralsTask(void* /*param*/) {
  b_menu.pin   = PIN_BTN_MENU;
  b_select.pin = PIN_BTN_SELECT;
  pinMode(b_menu.pin, INPUT_PULLUP);
  pinMode(b_select.pin, INPUT_PULLUP);

  PrintState prev_state = PST_DISCONNECTED;
  TelemetryData d;
  TickType_t last_wake = xTaskGetTickCount();

  while (true) {
    uint32_t ms = millis();
    telemetryRead(d);

    // --- buttons: state-machine driven, one event per press ---
    uint8_t ev = 0;
    if ((ev = buttonUpdate(b_menu, ms)))   ui_button_event = ev;
    if ((ev = buttonUpdate(b_select, ms))) ui_button_event = ev;

    // --- state transitions: beeps (skipped when sound disabled) ---
    if (options_sound_on() && d.state != prev_state) {
      if (d.state == PST_FINISH)    startBeepPattern(2);  // double beep
      else if (d.state == PST_RUNNING && prev_state != PST_RUNNING)
        startBeepPattern(1);                              // single ack
      else if (d.state == PST_FAILED) siren_active = true;
      prev_state = d.state;
    }

    // --- error siren (hms > 0), overrides everything (respects sound) ---
    if (d.hms_count > 0) siren_active = true;
    if (siren_active) {
      if (d.hms_count == 0 && d.state != PST_FAILED) {
        siren_active = false;          // error cleared
        toneStop();
      } else if (!options_sound_on()) {
        toneStop();                    // muted: LEDs still flash red
      } else if (ms - siren_last_toggle_ms >= 300) {
        siren_last_toggle_ms = ms;
        siren_high = !siren_high;
        toneStart(siren_high ? BUZZER_FREQ_HI : BUZZER_FREQ_LO);
      }
    }

    updateBeepPattern();

    // --- analog RGB strip state machine (LEDC PWM @ ~100 Hz) ---
    ledStateMachine(d, ms);
    // NOTE: strip is controllable at all times - menu overrides (OFF or
    // preset colors) take priority over MQTT-driven colors, checked first
    // inside ledStateMachine().

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(30));  // 33 Hz scan - buttons
    // still feel instant (well under 50 ms perception), LED ramp still
    // smooth, and PWM/LEDC churn drops 3x vs the old 100 Hz loop
  }
}

void peripheralsTaskStart() {
  xTaskCreatePinnedToCore(
      peripheralsTask, "periTask", TASK_STACK_PERIPH, nullptr, 1, nullptr,
      1 /* Core 1 - below UI priority 2 so rendering never waits */);
}
