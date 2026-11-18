/* =====================================================================
 *  config.h  -  central pin map + tunables (single source of truth)
 * ===================================================================== */
#pragma once

// ---------------- Display (verified working, do not change) ----------
// 4-pin SPI header on the TFT module: SCK / MOSI / MISO / CS
// (MISO exists on the module header; this panel never reads back, so the
//  ESP32 side is left unconnected - documented so nobody "fixes" it later)
#define PIN_TFT_MOSI   17
#define PIN_TFT_SCLK   14
#define PIN_TFT_MISO   -1     // module has the pin; panel readback unused
#define PIN_TFT_CS      5
#define PIN_TFT_DC     23
#define PIN_TFT_RST    18
#define PIN_TFT_BL     26
#define TFT_SPI_MHZ    40
#define TFT_TAB_FIX    0      // BLACKTAB, col_off=0 row_off=0 (verified)
#define TFT_RGB_INVERT 0      // RGB order (verified)

// ---------------- Inputs / outputs ----------------------------------
#define PIN_BTN_MENU       4      // INPUT_PULLUP, active LOW
#define PIN_BTN_SELECT     16     // INPUT_PULLUP, active LOW
#define PIN_BUZZER         19     // Buzzer output (LEDC PWM tone or Active DC)

// ---------------- SD card (separate SPI - HSPI, matrix-routed) -------
//   CS=15, SCK=25, MOSI=13, MISO=35
// (MISO moved 26 -> 35: GPIO 26 actively drives the TFT backlight PWM,
//  which fought the SD MISO signal when BLK was not jumpered to 3.3V -
//  intermittent SD mounts, corrupted writes and runtime crashes. GPIO 35
//  is input-only, strapping-safe and otherwise unused - dedicated MISO.)
#define PIN_SD_CS      15
#define PIN_SD_SCK     25
#define PIN_SD_MOSI    13
#define PIN_SD_MISO    35
#define SD_SPI_HOST    HSPI_HOST   // TFT stays on VSPI - no bus contention

// ---------------- Analog RGB strip (common anode via MOSFETs) --------
// 5V 5050 strip, 3x N-channel MOSFET low-side drivers, LEDC PWM.
// Common anode => duty 0% = LED off, 255 = full on.
#define PIN_LED_R      27
#define PIN_LED_G      22
#define PIN_LED_B      21
#define LED_PWM_FREQ   5000   // 5 kHz - above flicker perception
#define LED_PWM_BITS   8      // 0..255 per channel
#define LED_MAX_BRIGHTNESS 200  // cap (78%) - limits brick current draw
// LEDC channels 2/3/4 (0=backlight, 1=buzzer)
#define LEDC_CH_R      2
#define LEDC_CH_G      3
#define LEDC_CH_B      4
// Power-on idle color (Firoza = turquoise/cyan) until a print state
// takes over the strip.
#define LED_IDLE_R     0
#define LED_IDLE_G     210    // firoza: ~ (0, 210, 190)
#define LED_IDLE_B     190

// ---------------- Buzzer ---------------------------------------------
#define BUZZER_LEDC_CH  1     // channel 0 = backlight, 1 = buzzer
#define BUZZER_FREQ_OK    2000  // completion beep
#define BUZZER_FREQ_LO    1000  // siren low tone
#define BUZZER_FREQ_HI    2500  // siren high tone
#define BUZZER_FREQ_CLICK 1600  // gentle low-volume button click

// ---------------- SD card (optional; stub init in main.cpp) ----------
// Mounted at boot if a card is present; logs result. Not yet used for
// data - reserved for screenshots/logging later.

// ---------------- Network / MQTT -------------------------------------
#define MQTT_PORT       8883
#define MQTT_USER       "bblp"
#define MQTT_TOPIC_PREFIX "device/"
#define MQTT_TOPIC_SUFFIX "/report"
#define MQTT_MAX_PACKET_SIZE 4096
#define TELEMETRY_MUTEX_TIMEOUT_MS 100

// ---------------- UI --------------------------------------------------
#define UI_TARGET_FPS   30    // 30 fps pacing on Core 1 - fluid animations, zero CPU starvation
// Button engine timing (ms)
#define BTN_DEBOUNCE_MS   20  // stable-sample window
#define BTN_LONG_MS       500 // hold duration => LONG event (once)
#define BTN_DOUBLE_MS     350 // release-to-release gap => DOUBLE (menu only)

// ---------------- FreeRTOS task stack sizes (bytes) ------------------
#define TASK_STACK_NET   8192
#define TASK_STACK_UI    4096
#define TASK_STACK_PERIPH 3072
