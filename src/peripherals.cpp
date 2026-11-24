#include "peripherals.h"
#include "config.h"
#include "telemetry.h"
#include "ui.h"
#include "settings_manager.h"
#include "desk_pet.h"
#include "spotify_service.h"
#include "pc_stats.h"
#include "pet_mood.h"

#include <Arduino.h>

// Buzzer mode: 0 = Passive PWM, 1 = Active DC HIGH, 2 = Muted
static volatile uint8_t s_buzzer_mode = 0;
static void toneStop();

void setBuzzerMode(uint8_t mode) {
  s_buzzer_mode = mode % 3;
  toneStop(); // instant silence on mode change
}

uint8_t getBuzzerMode() {
  return s_buzzer_mode;
}

void setSoundEnabled(bool on) {
  if (!on) s_buzzer_mode = 2;
  else if (s_buzzer_mode == 2) s_buzzer_mode = 0;
}

bool options_sound_on() {
  return (s_buzzer_mode != 2);
}

// ---------------- Button engine: state machine per button -------------
// Active-LOW (direct switch to GND, ESP32 internal pullup, NO external resistors).
// Unpressed = HIGH (1), Pressed = LOW (0).
// 6-sample 60ms debouncing + 50ms hold requirement + SPI crosstalk rejection.
extern volatile bool g_tft_spi_busy;

struct BtnState {
  uint8_t  pin;
  uint8_t  active_level;    // 0 = active LOW (pressed when 0), 1 = active HIGH (pressed when 1)
  uint16_t history;         // 16-bit sample shift register (160ms history)
  bool     pressed;         // debounced state: true = pressed, false = released
  uint32_t press_start_ms;  // millis() when press confirmed
  bool     long_fired;      // true if long press already emitted
  uint32_t last_event_ms;   // cooldown lockout between events
  uint32_t glitch_win_ms;   // sliding 2s window for chatter rejection
  uint8_t  glitch_count;    // count of events within current window
  uint32_t lockout_ms;      // lockout timestamp if electrical chatter detected
};

static BtnState b_menu   {PIN_BTN_MENU,   0, 0, false, 0, false, 0, 0, 0, 0};
static BtnState b_select {PIN_BTN_SELECT, 1, 0, false, 0, false, 0, 0, 0, 0};

static uint8_t buttonEmit(BtnState& b, uint32_t ms, uint8_t ev) {
  if (ms - b.glitch_win_ms > 2000) {
    b.glitch_win_ms = ms;
    b.glitch_count = 0;
  }
  b.glitch_count++;
  if (b.glitch_count > 8) {
    // 8+ events in 2s = real electrical chatter (EMI fires dozens/sec;
    // a human scrolling a menu taps at most ~4/s, so 8 is safe).
    b.lockout_ms = ms + 2000; // Lock out pin for 2 seconds
    Serial.printf("[BTN] Pin %d chatter/EMI detected (%d ev/2s) -> locked for 2s\n", b.pin, b.glitch_count);
    return 0;
  }
  b.last_event_ms = ms;
  return ev;
}

// event codes (match ui.h): 1=menu 2=select 3=menuLong 4=menuDouble 5=selLong
static uint8_t buttonUpdate(BtnState& b, uint32_t ms) {
  if (ms < 1500) return 0; // 1.5s boot stabilization lockout to prevent boot chatter

  // If locked out due to chatter/EMI, suppress events and reset history
  if (b.lockout_ms > ms) {
    b.history = 0;
    b.pressed = false;
    return 0;
  }

  // During active SPI display transfers, skip sampling to avoid injecting fake released states
  if (g_tft_spi_busy) {
    return 0;
  }

  // 1 = currently pressed, 0 = currently released
  uint8_t raw = (digitalRead(b.pin) == b.active_level) ? 1 : 0;
  b.history = (b.history << 1) | raw;

  // 40ms cooldown between events for snappy response
  if (b.last_event_ms > 0 && (ms - b.last_event_ms < 40)) {
    return 0;
  }

  // 4 consecutive PRESSED reads (40 ms) = CONFIRMED PRESSED
  bool stable_pressed  = ((b.history & 0x0F) == 0x0F);
  // 4 consecutive RELEASED reads (40 ms) = CONFIRMED RELEASED
  bool stable_released = ((b.history & 0x0F) == 0x00);

  // Transition: Released -> Pressed
  if (!b.pressed && stable_pressed) {
    b.pressed = true;
    b.press_start_ms = ms;
    b.long_fired = false;
    return 0;
  }

  // While held: check for long-press threshold (450 ms)
  if (b.pressed && !b.long_fired && (ms - b.press_start_ms >= 450)) {
    b.long_fired = true;
    return buttonEmit(b, ms, (&b == &b_menu) ? 3 : 5); // 3=menu long, 5=select long
  }

  // Transition: Pressed -> Released
  if (b.pressed && stable_released) {
    b.pressed = false;
    uint32_t press_duration = ms - b.press_start_ms;
    // Require at least 30 ms hold duration to reject fast EMI spikes
    if (!b.long_fired && press_duration >= 30) {
      return buttonEmit(b, ms, (&b == &b_menu) ? 1 : 2); // 1=menu short, 2=select short
    }
  }

  return 0;
}

// ---------------- Buzzer: Dual Active/Passive Driver ------------------
static uint32_t siren_start_ms = 0;
static uint32_t siren_test_until_ms = 0;
static bool     siren_active = false;
// Bark engine state (siren policy, see peripheralsTask):
static bool     finish_bark = false;       // loop fanfare while FINISH latched
static uint32_t finish_bark_last_ms = 0;
static uint32_t siren_burst_until = 0;     // auto-mute deadline (mode 0); 0=unlimited

void triggerSirenTest(uint32_t duration_ms) {
  siren_active = true;
  siren_start_ms = millis();
  siren_test_until_ms = millis() + duration_ms;
}

static void toneStart(uint32_t freq, uint32_t duty = 512) {
  if (s_buzzer_mode == 2) return; // Muted

  if (s_buzzer_mode == 1) {
    // Active Buzzer: DC 3.3V
    ledcDetachPin(PIN_BUZZER);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    // Passive Buzzer: LEDC PWM square wave with custom volume/duty
    pinMode(PIN_BUZZER, OUTPUT);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
    ledcWriteTone(BUZZER_LEDC_CH, freq);
    ledcWrite(BUZZER_LEDC_CH, duty);
  }
}

static void toneStop() {
  ledcWrite(BUZZER_LEDC_CH, 0);
  ledcWriteTone(BUZZER_LEDC_CH, 0);
  ledcDetachPin(PIN_BUZZER);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
}

void playHardwareBeep(uint32_t freq, uint32_t duration_ms) {
  toneStart(freq, 250);
  delay(duration_ms);
  toneStop();
}

// One-shot patterns: 1=2-tone chime, 2=3-tone fanfare, 3=soft button click
static uint8_t  beep_seq = 0;
static uint32_t beep_step_ms = 0;

void startBeepPattern(uint8_t pattern) {
  if (!options_sound_on()) return;
  beep_seq = pattern;
  beep_step_ms = millis();
  if (pattern == 1)      toneStart(1800, 400); // 1st tone of chime
  else if (pattern == 2) toneStart(1600, 450); // 1st tone of fanfare
  else                   toneStart(BUZZER_FREQ_OK, 400);
}

// Gentle, low-volume button click (duty 25 for passive, 8ms pulse for active)
void beepButton() {
  if (s_buzzer_mode == 2 || !options_sound_on()) return;
  beep_seq = 3;
  beep_step_ms = millis();
  toneStart(BUZZER_FREQ_CLICK, 25); // ~2.4% duty = very soft & quiet click
}

// Playful, cheerful 2-tone pet chirp (2200 Hz -> 2850 Hz)
void beepPet() {
  if (s_buzzer_mode == 2 || !options_sound_on()) return;
  beep_seq = 4;
  beep_step_ms = millis();
  toneStart(2200, 320); // First high squeak
}

static void updateBeepPattern() {
  if (!beep_seq) return;
  uint32_t t = millis() - beep_step_ms;
  switch (beep_seq) {
    case 1: // Notification: 2-tone rhythmic chime
      if (t < 70) {
        // Step 1: 1800 Hz
      } else if (t < 100) {
        toneStop(); // 30ms gap
      } else if (t < 200) {
        toneStart(BUZZER_FREQ_HI, 450); // Step 2: 2500 Hz
      } else {
        toneStop();
        beep_seq = 0;
      }
      break;

    case 2: // Major notification / print complete: 3-tone rhythmic fanfare
      if (t < 80) {
        // Step 1: 1600 Hz
      } else if (t < 110) {
        toneStop(); // 30ms gap
      } else if (t < 190) {
        toneStart(BUZZER_FREQ_OK, 480); // Step 2: 2000 Hz
      } else if (t < 220) {
        toneStop(); // 30ms gap
      } else if (t < 360) {
        toneStart(BUZZER_FREQ_HI, 512); // Step 3: 2500 Hz
      } else {
        toneStop();
        beep_seq = 0;
      }
      break;

    case 3: // Button Click: ultra-short & quiet
      // 8 ms for active buzzer, 15 ms for passive buzzer
      if (t >= (uint32_t)((s_buzzer_mode == 1) ? 8 : 15)) {
        toneStop();
        beep_seq = 0;
      }
      break;

    case 4: // Pet Chirp: cheerful, cute high-pitched two-tone squeak
      if (t < 35) {
        // Step 1: 2200 Hz
      } else if (t < 48) {
        toneStop(); // 13ms gap
      } else if (t < 95) {
        toneStart(2850, 320); // Step 2: 2850 Hz chirp
      } else {
        toneStop();
        beep_seq = 0;
      }
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

// Menu override state (set from UI via ledSetOverride):
// 0 = auto (follow MQTT state), 1 = forced off, 2 = manual rainbow,
// 3..7 = forced preset colors (R,G,B,W,Amber), 8 = custom RGB
static volatile uint8_t led_override = 0;
void ledSetOverride(uint8_t mode) { led_override = mode; }
uint8_t ledGetOverride() { return led_override; }

static volatile uint8_t led_custom_r = 255;
static volatile uint8_t led_custom_g = 255;
static volatile uint8_t led_custom_b = 255;
static volatile uint8_t led_brightness_pct = 100;

void ledSetCustomRgb(uint8_t r, uint8_t g, uint8_t b) {
  led_custom_r = r;
  led_custom_g = g;
  led_custom_b = b;
  led_override = 8;
}
void ledGetCustomRgb(uint8_t& r, uint8_t& g, uint8_t& b) {
  r = led_custom_r;
  g = led_custom_g;
  b = led_custom_b;
}
void ledSetBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  led_brightness_pct = pct;
}
uint8_t ledGetBrightness() { return led_brightness_pct; }

static bool led_inverted = false;
void ledSetInverted(bool inv) { led_inverted = inv; }
bool ledGetInverted() { return led_inverted; }

static void ledApply(uint8_t r, uint8_t g, uint8_t b, uint32_t ms = 0) {
  // Direct, instant, exact PWM output without lag or math truncation
  uint8_t eff_pct = led_brightness_pct;
  // Dynamic music beat pulsation: kick transients pump the LED strip in sync with the beat
  if (ms > 0 && musicIsActive()) {
    float hz = deskPetGetVibeHz();
    if (hz < 0.5f || hz > 8.0f) hz = 2.0f;
    float ph = (float)ms * 0.001f * hz * 6.2831853f;
    float kick = powf(0.5f - 0.5f * cosf(ph), 6.0f);
    float energy = (pcStatsIsActive() && g_pc_stats.music_vibe > 0.05f) ? g_pc_stats.music_vibe : 0.75f;
    if (energy > 1.0f) energy = 1.0f;
    float pulse = 0.18f + 0.82f * kick * (0.35f + 0.65f * energy);
    if (pulse > 1.0f) pulse = 1.0f;
    eff_pct = (uint8_t)(led_brightness_pct * pulse);
  }
  uint16_t cap = (uint16_t)LED_MAX_BRIGHTNESS * eff_pct / 100;
  uint8_t wr = (uint16_t)r * cap / 255;
  uint8_t wg = (uint16_t)g * cap / 255;
  uint8_t wb = (uint16_t)b * cap / 255;
  if (led_inverted) {
    cur_r = 255 - wr;
    cur_g = 255 - wg;
    cur_b = 255 - wb;
  } else {
    cur_r = wr;
    cur_g = wg;
    cur_b = wb;
  }
  ledcWrite(LEDC_CH_R, cur_r);
  ledcWrite(LEDC_CH_G, cur_g);
  ledcWrite(LEDC_CH_B, cur_b);
}

// Dedicated Real-Time Music Reactive LED Engine
static void renderMusicSyncLed(uint32_t ms) {
  bool active = musicIsActive();
  uint8_t r, g, b;

  if (active) {
    // 1. Pick base music color: genre signature mood color, or custom RGB if set
    if (led_override == 8 && (led_custom_r != 255 || led_custom_g != 255 || led_custom_b != 255)) {
      r = led_custom_r;
      g = led_custom_g;
      b = led_custom_b;
    } else {
      petMoodGetGenreColor(r, g, b);
    }

    // 2. Frequency & Beat Phase
    float hz = deskPetGetVibeHz();
    if (hz < 0.5f || hz > 8.0f) hz = 2.0f;
    float ph = (float)ms * 0.001f * hz * 6.2831853f;

    // 3. Transient Flash Envelope:
    // Power of 6 makes a punchy, sharp transient spike on the kick/snare beat
    float kick = powf(0.5f - 0.5f * cosf(ph), 6.0f);

    // Dynamic energy scaling from audio beat analyzer FFT
    float energy = (pcStatsIsActive() && g_pc_stats.music_vibe > 0.05f) ? g_pc_stats.music_vibe : 0.75f;
    if (energy > 1.0f) energy = 1.0f;

    // 18% ambient floor + up to 82% punchy beat flash
    float pulse = 0.18f + 0.82f * kick * (0.35f + 0.65f * energy);
    if (pulse > 1.0f) pulse = 1.0f;

    uint8_t eff_pct = (uint8_t)(led_brightness_pct * pulse);
    uint16_t cap = (uint16_t)LED_MAX_BRIGHTNESS * eff_pct / 100;
    uint8_t wr = (uint16_t)r * cap / 255;
    uint8_t wg = (uint16_t)g * cap / 255;
    uint8_t wb = (uint16_t)b * cap / 255;

    if (led_inverted) {
      cur_r = 255 - wr;
      cur_g = 255 - wg;
      cur_b = 255 - wb;
    } else {
      cur_r = wr;
      cur_g = wg;
      cur_b = wb;
    }
    ledcWrite(LEDC_CH_R, cur_r);
    ledcWrite(LEDC_CH_G, cur_g);
    ledcWrite(LEDC_CH_B, cur_b);
  } else {
    // Music is paused or waiting for audio:
    // Gentle rhythmic breathing standby glow in electric violet / cyber purple
    float ph = (float)ms * 0.001f * 0.6f * 6.2831853f; // 0.6 Hz slow breath
    float breath = 0.20f + 0.25f * (0.5f + 0.5f * sinf(ph));
    uint8_t eff_pct = (uint8_t)(led_brightness_pct * breath);
    uint16_t cap = (uint16_t)LED_MAX_BRIGHTNESS * eff_pct / 100;
    r = 150; g = 30; b = 255; // neon purple standby
    uint8_t wr = (uint16_t)r * cap / 255;
    uint8_t wg = (uint16_t)g * cap / 255;
    uint8_t wb = (uint16_t)b * cap / 255;

    if (led_inverted) {
      cur_r = 255 - wr;
      cur_g = 255 - wg;
      cur_b = 255 - wb;
    } else {
      cur_r = wr;
      cur_g = wg;
      cur_b = wb;
    }
    ledcWrite(LEDC_CH_R, cur_r);
    ledcWrite(LEDC_CH_G, cur_g);
    ledcWrite(LEDC_CH_B, cur_b);
  }
}

// Full 360-degree continuous HSV to RGB converter (pure integer math, zero glitch)
static void hsvToRgb360(uint16_t h, uint8_t s, uint8_t v,
                        uint8_t& r, uint8_t& g, uint8_t& b) {
  h %= 360;
  if (s == 0) { r = g = b = v; return; }
  uint8_t region = h / 60;
  uint32_t rem = (h - (region * 60)) * 255 / 60;
  uint8_t p = (v * (255 - s)) / 255;
  uint8_t q = (v * (255 - ((s * rem) / 255))) / 255;
  uint8_t t = (v * (255 - ((s * (255 - rem)) / 255))) / 255;
  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

static void ledStateMachine(const TelemetryData& d, uint32_t ms) {
  // 1) USER FORCED PRESETS / WEB CONTROLS ALWAYS TAKE HIGHEST PRIORITY!
  if (led_override == 1) { ledApply(0, 0, 0); return; } // OFF
  if (led_override == 9) { // Dedicated Music Sync Mode
    renderMusicSyncLed(ms);
    return;
  }
  if (led_override == 2) { // Butter-smooth rainbow
    static uint32_t last_rb_ms = 0;
    if (ms - last_rb_ms >= 30) {
      last_rb_ms = ms;
      rainbow_hue = (rainbow_hue + 1) % 360;
    }
    uint8_t r, g, b;
    hsvToRgb360(rainbow_hue, 255, 255, r, g, b);
    ledApply(r, g, b);
    return;
  }
  static const uint8_t preset[5][3] = {
    {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255}, {255,180,0}};
  if (led_override >= 3 && led_override <= 7) {
    const uint8_t* c = preset[led_override - 3];
    ledApply(c[0], c[1], c[2]);
    return;
  }
  if (led_override == 8) {
    // If music is active, pulse the custom RGB to the beat!
    if (musicIsActive()) {
      renderMusicSyncLed(ms);
    } else {
      ledApply(led_custom_r, led_custom_g, led_custom_b);
    }
    return;
  }

  // 2) Latched result: FINISH green / FAILED red (only in Auto-Sync mode: led_override == 0)
  if (ui_latched_result == 1) { ledApply(0, 255, 0); return; }
  if (ui_latched_result == 2) { ledApply(255, 0, 0); return; }

  // 3) Timelapse / layer change: warm amber
  if (d.state == PST_TIMELAPSE || d.stg_cur == 26) {
    ledApply(255, 160, 0);
    return;
  }
  // 4) Error: hms array non-empty => stark red flash @500 ms
  if (d.hms_count > 0) {
    if (ms - led_flash_last_ms >= 500) {
      led_flash_last_ms = ms;
      led_flash_on = !led_flash_on;
    }
    ledApply(led_flash_on ? 255 : 0, 0, 0);
    return;
  }

  // 5) Auto-Sync Music: When printer is idle/disconnected and music is playing, sync LEDs to the beat!
  if (musicIsActive() && d.state != PST_RUNNING && d.state != PST_PREPARE && d.state != PST_PAUSED) {
    renderMusicSyncLed(ms);
    return;
  }

  switch (d.state) {
    case PST_RUNNING: {
      // While printing: warm amber; if music is playing, pulse in rhythm!
      ledApply(255, 160, 0, ms);
      break;
    }
    case PST_FINISH:  ledApply(0, 255, 0);   break;  // solid green
    case PST_FAILED:  ledApply(255, 0, 0);   break;  // solid red
    case PST_PAUSED:  ledApply(255, 210, 0); break;  // YELLOW = paused
    case PST_PREPARE: ledApply(255, 140, 0); break;  // warm amber warming up
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
  gpio_pullup_en((gpio_num_t)b_menu.pin);
  gpio_pullup_en((gpio_num_t)b_select.pin);
  gpio_pulldown_dis((gpio_num_t)b_menu.pin);
  gpio_pulldown_dis((gpio_num_t)b_select.pin);

  // Hardware LEDC PWM Attachment (LEDC channels 2, 3, 4)
  ledcSetup(LEDC_CH_R, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_R, LEDC_CH_R);
  ledcSetup(LEDC_CH_G, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_G, LEDC_CH_G);
  ledcSetup(LEDC_CH_B, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(PIN_LED_B, LEDC_CH_B);

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  vTaskDelay(pdMS_TO_TICKS(50));

  PrintState prev_state = PST_DISCONNECTED;
  TelemetryData d;
  TickType_t last_wake = xTaskGetTickCount();

  while (true) {
    uint32_t ms = millis();
    telemetryRead(d);

    // Buttons are physically wired with pullups, active LOW (grounded when pressed)
    static bool calibrated = false;
    if (!calibrated) {
      b_menu.active_level   = 0;
      b_select.active_level = 0;
      calibrated = true;
      Serial.println(F("[BTN] Static polarity locked: Menu=Active-LOW Select=Active-LOW"));
    }

    // --- buttons: state-machine driven, one event per press ---
    uint8_t ev = buttonUpdate(b_menu, ms);
    if (!ev) ev = buttonUpdate(b_select, ms);
    if (ev) {
      ui_button_event = ev;
      beepButton(); // crisp audio feedback on every button press
      Serial.printf("[BTN] event=%u (menu=%d sel=%d)\n", ev,
                    digitalRead(PIN_BTN_MENU), digitalRead(PIN_BTN_SELECT));
    }

    // --- state transitions: beeps / barks (skipped when muted) ---
    // Siren policy (g_settings.siren_mode):
    //   0 = AUTO-MUTE: one notification burst (~2 s) then silent
    //   1 = UNTIL-OK : FINISH loops the fanfare and FAILED/HMS loops the
    //                  siren until the user ACKs on the overlay (OK button)
    static bool had_active_print_sound = false;
    if (d.state == PST_RUNNING || d.state == PST_PAUSED) {
      had_active_print_sound = true;
    } else if (d.state == PST_IDLE && d.percent == 0) {
      had_active_print_sound = false;
    }

    if (options_sound_on() && d.state != prev_state) {
      if (d.state == PST_FINISH) {
        if (had_active_print_sound && (d.percent >= 98 || (d.percent >= 95 && d.remain_min == 0))) {
          had_active_print_sound = false;
          if (g_settings.siren_mode == 1) {
            finish_bark = true;             // loop fanfare until latch cleared
            finish_bark_last_ms = 0;
          } else {
            startBeepPattern(2);            // single fanfare (legacy)
          }
        }
      }
      // (Print running start chime disabled to prevent random beeps)
      else if (d.state == PST_FAILED) {
        if (had_active_print_sound) {
          had_active_print_sound = false;
          siren_burst_until = (g_settings.siren_mode == 1) ? 0 : ms + 2200;
          siren_active = true;
          siren_start_ms = ms;
        }
      }
      prev_state = d.state;
    }

    // --- FINISH fanfare loop: barks until OK clears the latch ---
    if (finish_bark) {
      if (ui_latched_result != 1 || g_settings.siren_mode != 1) {
        finish_bark = false;              // user ACKed (or mode switched)
      } else if (finish_bark_last_ms == 0 ||
                 ms - finish_bark_last_ms >= 1200) {
        finish_bark_last_ms = ms;
        startBeepPattern(2);              // repeat fanfare every 1.2 s
      }
    }

    // --- error siren (hms > 0 / FAILED / latched FAILED, or manual test) ---
    bool err_cond = (d.hms_count > 0) || (d.state == PST_FAILED) ||
                    (g_settings.siren_mode == 1 && ui_latched_result == 2);
    bool test_running = (siren_test_until_ms > ms);
    // auto-mute: burst deadline passed -> stay silent until cond re-occurs
    bool auto_muted = (siren_burst_until != 0) &&
                      ((int32_t)(ms - siren_burst_until) >= 0);
    if ((err_cond || test_running) && !(auto_muted && !test_running)) {
      if (!siren_active) {
        siren_active = true;
        siren_start_ms = ms;
        if (err_cond) {                   // fresh error: restart burst window
          siren_burst_until = (g_settings.siren_mode == 1) ? 0 : ms + 2200;
        }
      }
    } else {
      if (siren_active) {
        siren_active = false;
        toneStop();
      }
      if (!err_cond) siren_burst_until = 0;   // condition cleared -> rearm
    }

    if (siren_active) {
      if (!options_sound_on()) {
        toneStop();
      } else {
        // Full emergency rhythmic siren (880ms cadence):
        // 0..160ms: HIGH (2700Hz) - full volume
        // 160..230ms: SILENCE (70ms gap)
        // 230..390ms: LOW (1500Hz) - full volume
        // 390..460ms: SILENCE (70ms gap)
        // 460..620ms: HIGH (2700Hz) - full volume
        // 620..880ms: SILENCE (260ms rhythm pause)
        uint32_t cycle = (ms - siren_start_ms) % 880;
        if (cycle < 160) {
          toneStart(BUZZER_FREQ_HI, 512);
        } else if (cycle < 230) {
          toneStop();
        } else if (cycle < 390) {
          toneStart(BUZZER_FREQ_LO, 512);
        } else if (cycle < 460) {
          toneStop();
        } else if (cycle < 620) {
          toneStart(BUZZER_FREQ_HI, 512);
        } else {
          toneStop();
        }
      }
    }

    updateBeepPattern();

    // --- analog RGB strip state machine (LEDC PWM @ ~100 Hz) ---
    ledStateMachine(d, ms);
    // NOTE: strip is controllable at all times - menu overrides (OFF or
    // preset colors) take priority over MQTT-driven colors, checked first
    // inside ledStateMachine().

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));  // 100 Hz scan - instant
    // response, 3-sample 30 ms debouncing, butter-smooth LED ramp
  }
}

void peripheralsTaskStart() {
  xTaskCreatePinnedToCore(
      peripheralsTask, "periTask", TASK_STACK_PERIPH, nullptr, 2, nullptr,
      1 /* Core 1 - priority 2 round-robin with UI task */);
}
