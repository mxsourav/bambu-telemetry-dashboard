/* =====================================================================
 *  web_server.cpp  -  STΔN studio. Web Control Platform (Core 0)
 * ===================================================================== */
#include "web_server.h"
#include "web_ui.h"
#include "config.h"
#include "telemetry.h"
#include "ui.h"
#include "peripherals.h"
#include "settings_manager.h"
#include "spotify_service.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <Update.h>
#include <ArduinoJson.h>

static WebServer server(80);
static File uploadFile;
static String uploadTargetPath;

#pragma pack(push, 1)
struct BMPHeader {
  uint16_t bfType;          // 0x4D42 ("BM")
  uint32_t bfSize;          // 66 + 160*128*2 = 41026
  uint16_t bfReserved1;     // 0
  uint16_t bfReserved2;     // 0
  uint32_t bfOffBits;       // 66
  uint32_t biSize;          // 40
  int32_t  biWidth;         // 160
  int32_t  biHeight;        // -128 (top-down matching RGB565 memory buffer)
  uint16_t biPlanes;        // 1
  uint16_t biBitCount;      // 16
  uint32_t biCompression;   // 3 (BI_BITFIELDS)
  uint32_t biSizeImage;     // 160*128*2 = 40960
  int32_t  biXPelsPerMeter; // 2835
  int32_t  biYPelsPerMeter; // 2835
  uint32_t biClrUsed;       // 0
  uint32_t biClrImportant;  // 0
  uint32_t redMask;         // 0xF800
  uint32_t greenMask;       // 0x07E0
  uint32_t blueMask;        // 0x001F
};
#pragma pack(pop)

extern bool g_sd_mounted;

// ---------------- CORS Helpers ----------------------------------------
static void sendCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS, PUT, DELETE");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Origin, Accept, X-Requested-With");
  server.sendHeader("Access-Control-Allow-Private-Network", "true");
  server.sendHeader("Connection", "close");
}

static void handleOptions() {
  sendCorsHeaders();
  server.sendHeader("Access-Control-Max-Age", "86400");
  server.send(204);
}

// ---------------- Handlers --------------------------------------------
static void handleRoot() {
  server.setContentLength(WEB_UI_GZ_LEN);
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  sendCorsHeaders();
  server.send(200, "text/html", "");
  size_t offset = 0;
  const size_t chunkSize = 1024;
  while (offset < WEB_UI_GZ_LEN) {
    size_t toSend = ((WEB_UI_GZ_LEN - offset) < chunkSize) ? (WEB_UI_GZ_LEN - offset) : chunkSize;
    server.sendContent_P((PGM_P)(WEB_UI_GZ + offset), toSend);
    offset += toSend;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void handleStatus() {
  TelemetryData d;
  telemetryRead(d);

  JsonDocument doc;
  doc["wifi_rssi"]    = WiFi.RSSI();
  doc["ip"]           = WiFi.localIP().toString();
  doc["heap_free"]    = ESP.getFreeHeap();
  doc["uptime_s"]     = millis() / 1000;
  doc["print_state"]  = printStateName(d.state);
  doc["percent"]      = d.percent;
  doc["nozzle_temp"]  = d.nozzle_temp;
  doc["nozzle_target"]= d.nozzle_target;
  doc["bed_temp"]     = d.bed_temp;
  doc["bed_target"]   = d.bed_target;
  doc["remain_min"]   = d.remain_min;
  doc["layer"]        = String(d.layer_num) + " / " + String(d.total_layers);
  doc["led_mode"]     = ledGetOverride();
  uint8_t r, g, b;
  ledGetCustomRgb(r, g, b);
  doc["led_r"]        = r;
  doc["led_g"]        = g;
  doc["led_b"]        = b;
  doc["brightness"]   = ledGetBrightness();
  extern uint32_t g_sd_total_mb;
  extern uint32_t g_sd_used_mb;
  doc["sd_total_mb"]  = g_sd_total_mb;
  doc["sd_used_mb"]   = g_sd_used_mb;
  doc["btn_menu"]     = digitalRead(PIN_BTN_MENU);
  doc["btn_select"]   = digitalRead(PIN_BTN_SELECT);
  doc["buzzer_mode"]  = getBuzzerMode();
  doc["theme_light"]  = uiGetThemeLight();
  doc["clock_12h"]    = uiGetClock12h();
  doc["led_inverted"] = ledGetInverted();
  doc["custom_image"] = uiIsCustomImageActive();

  String out;
  serializeJson(doc, out);
  sendCorsHeaders();
  server.send(200, "application/json", out);
}

static void handleScreenRle() {
  const uint16_t* fb = uiGetFramebuffer();
  if (!fb) {
    sendCorsHeaders();
    server.send(503, "text/plain", "Frame unavailable");
    return;
  }

  // When custom image is showing on screen, avoid streaming 61 KB of non-compressible RLE data
  // which overloads lwIP socket buffers and causes client timeouts.
  if (uiIsCustomImageActive()) {
    sendCorsHeaders();
    server.send(204, "text/plain", "");
    return;
  }

  sendCorsHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/octet-stream", "");

  const int totalPixels = 160 * 128;
  uint8_t chunk[1536];
  size_t chunkLen = 0;
  int i = 0;

  WiFiClient client = server.client();

  while (i < totalPixels) {
    if (!client.connected()) {
      break; // Client aborted, stop pushing immediately and release socket
    }
    uint16_t col = fb[i];
    uint8_t run = 1;
    while (i + run < totalPixels && fb[i + run] == col && run < 255) {
      run++;
    }
    chunk[chunkLen++] = run;
    // Send true RGB565 Big-Endian over the wire (MSB first, LSB second)
    // fb[i] on little-endian CPU has MSB in lower byte and LSB in upper byte
    chunk[chunkLen++] = (uint8_t)(col & 0xFF);
    chunk[chunkLen++] = (uint8_t)(col >> 8);
    i += run;

    if (chunkLen + 3 > sizeof(chunk)) {
      server.sendContent((const char*)chunk, chunkLen);
      chunkLen = 0;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  if (chunkLen > 0 && client.connected()) {
    server.sendContent((const char*)chunk, chunkLen);
  }
  if (client.connected()) {
    server.sendContent(""); // Final 0-length chunk to properly terminate HTTP chunked stream
  }
}

static void handleBuzzerTest() {
  if (server.hasArg("mode")) {
    setBuzzerMode(server.arg("mode").toInt());
  }
  String pat = server.hasArg("pattern") ? server.arg("pattern") : "1";
  if (pat == "click" || pat == "btn") {
    beepButton();
  } else if (pat == "alarm" || pat == "siren") {
    triggerSirenTest(3000); // 3 seconds of full rhythmic emergency siren
  } else if (pat == "2" || pat == "complete" || pat == "fanfare") {
    startBeepPattern(2);
  } else {
    startBeepPattern(1); // 2-tone rhythmic chime notification
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\",\"mode\":" + String(getBuzzerMode()) + "}");
}

static void handleBuzzerMode() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("plain")) {
      JsonDocument doc;
      deserializeJson(doc, server.arg("plain"));
      if (!doc["mode"].isNull()) {
        setBuzzerMode(doc["mode"].as<int>());
      }
    } else if (server.hasArg("mode")) {
      setBuzzerMode(server.arg("mode").toInt());
    }
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"mode\":" + String(getBuzzerMode()) + "}");
}

static void handleScreen() {
  const uint16_t* fb = uiGetFramebuffer();
  if (!fb) {
    server.send(503, "text/plain", "Frame unavailable");
    return;
  }
  BMPHeader hdr;
  hdr.bfType          = 0x4D42;
  hdr.bfSize          = sizeof(BMPHeader) + 160 * 128 * 2;
  hdr.bfReserved1     = 0;
  hdr.bfReserved2     = 0;
  hdr.bfOffBits       = sizeof(BMPHeader);
  hdr.biSize          = 40;
  hdr.biWidth         = 160;
  hdr.biHeight        = -128; // top-down
  hdr.biPlanes        = 1;
  hdr.biBitCount      = 16;
  hdr.biCompression   = 3; // BI_BITFIELDS
  hdr.biSizeImage     = 160 * 128 * 2;
  hdr.biXPelsPerMeter = 2835;
  hdr.biYPelsPerMeter = 2835;
  hdr.biClrUsed       = 0;
  hdr.biClrImportant  = 0;
  hdr.redMask         = 0xF800;
  hdr.greenMask       = 0x07E0;
  hdr.blueMask        = 0x001F;

  server.setContentLength(sizeof(BMPHeader) + 160 * 128 * 2);
  server.send(200, "image/bmp", "");
  server.sendContent((const char*)&hdr, sizeof(hdr));

  const char* p = (const char*)fb;
  size_t total = 160 * 128 * 2;
  size_t offset = 0;
  const size_t chunkSize = 1024;
  while (offset < total) {
    size_t toSend = ((total - offset) < chunkSize) ? (total - offset) : chunkSize;
    server.sendContent(p + offset, toSend);
    offset += toSend;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

static void handleButton() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char* btn = doc["btn"] | "";
    const char* act = doc["action"] | "click";
    if (!strcmp(btn, "up")) {
      ui_button_event = 6;
    } else if (!strcmp(btn, "down")) {
      ui_button_event = 7;
    } else if (!strcmp(btn, "left")) {
      ui_button_event = 8;
    } else if (!strcmp(btn, "right")) {
      ui_button_event = 9;
    } else if (!strcmp(btn, "ok") || !strcmp(btn, "select")) {
      if (!strcmp(act, "long")) ui_button_event = 5;
      else                      ui_button_event = 2;
    } else if (!strcmp(btn, "back")) {
      ui_button_event = 4;
    } else if (!strcmp(btn, "home")) {
      ui_button_event = 10;
    } else if (!strcmp(btn, "menu")) {
      if (!strcmp(act, "double"))     ui_button_event = 4;
      else if (!strcmp(act, "long"))  ui_button_event = 3;
      else                            ui_button_event = 1;
    }
    beepButton(); // audio click feedback for remote phone press
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleFsList() {
  sendCorsHeaders();
  if (!sdEnsureMounted()) {
    server.send(400, "application/json", "{\"error\":\"SD card not inserted or not mounted\"}");
    return;
  }
  String dirPath = server.hasArg("dir") ? server.arg("dir") : "/";
  if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;

  File dir = SD.open(dirPath);
  if (!dir || (dirPath != "/" && !dir.isDirectory())) {
    server.send(400, "application/json", "{\"error\":\"Invalid directory\"}");
    if (dir) dir.close();
    return;
  }

  JsonDocument doc;
  doc["dir"] = dirPath;
  JsonArray files = doc["files"].to<JsonArray>();

  File entry = dir.openNextFile();
  int count = 0;
  while (entry && count < 64) {
    JsonObject item = files.add<JsonObject>();
    item["name"] = String(entry.name());
    item["size"] = entry.size();
    item["dir"]  = entry.isDirectory();
    entry = dir.openNextFile();
    count++;
  }
  dir.close();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static void handleFsDownload() {
  sendCorsHeaders();
  if (!sdEnsureMounted()) {
    server.send(400, "text/plain", "SD card not mounted");
    return;
  }
  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Missing path");
    return;
  }
  String path = server.arg("path");
  if (!path.startsWith("/")) path = "/" + path;

  File f = SD.open(path, FILE_READ);
  if (f && !f.isDirectory()) {
    server.setContentLength(f.size());
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + String(f.name()) + "\"");
    server.send(200, "application/octet-stream", "");
    char buf[1024];
    while (f.available()) {
      size_t bytesRead = f.read((uint8_t*)buf, sizeof(buf));
      if (bytesRead == 0) break;
      server.sendContent(buf, bytesRead);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    f.close();
  } else {
    server.send(404, "text/plain", "File not found");
  }
}

static void handleFsDelete() {
  sendCorsHeaders();
  if (!sdEnsureMounted()) {
    server.send(400, "application/json", "{\"error\":\"SD card not mounted\"}");
    return;
  }
  if (!server.hasArg("path")) {
    server.send(400, "application/json", "{\"error\":\"Missing path\"}");
    return;
  }
  String path = server.arg("path");
  if (!path.startsWith("/")) path = "/" + path;

  if (SD.exists(path)) {
    File f = SD.open(path);
    bool isDir = f ? f.isDirectory() : false;
    if (f) f.close();
    bool res = isDir ? SD.rmdir(path) : SD.remove(path);
    server.send(200, "application/json", res ? "{\"status\":\"ok\"}" : "{\"error\":\"Delete failed\"}");
  } else {
    server.send(404, "application/json", "{\"error\":\"Path not found\"}");
  }
}

static bool upload_success = false;

static void handleFsUploadLoop() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    upload_success = false;
    if (!sdEnsureMounted()) {
      Serial.println(F("[FS] Upload failed: SD card not mounted"));
      return;
    }
    uploadTargetPath = server.hasArg("path") ? server.arg("path") : ("/" + upload.filename);
    if (!uploadTargetPath.startsWith("/")) uploadTargetPath = "/" + uploadTargetPath;
    if (SD.exists(uploadTargetPath)) SD.remove(uploadTargetPath);
    uploadFile = SD.open(uploadTargetPath, FILE_WRITE);
    if (!uploadFile) {
      Serial.printf("[FS] Failed to open %s for write\n", uploadTargetPath.c_str());
      return;
    }
    upload_success = true;
    Serial.printf("[FS] Upload start: %s\n", uploadTargetPath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      size_t written = uploadFile.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) upload_success = false;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.printf("[FS] Upload done: %u bytes\n", upload.totalSize);
    }
  }
}

static void handleFsUploadDone() {
  sendCorsHeaders();
  if (upload_success) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(500, "application/json", "{\"error\":\"File write failed\"}");
  }
}

static void handleLedSet() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    if (!doc["mode"].isNull()) {
      uint8_t m = doc["mode"];
      if (m == 1) { // OFF
        ledSetOverride(1);
      } else if (m == 2) { // Rainbow
        ledSetOverride(2);
      } else if (m == 3) { // Red
        ledSetCustomRgb(255, 0, 0);
      } else if (m == 4) { // Green
        ledSetCustomRgb(0, 255, 0);
      } else if (m == 5) { // Blue
        ledSetCustomRgb(0, 0, 255);
      } else if (m == 6) { // White
        ledSetCustomRgb(255, 255, 255);
      } else if (m == 7) { // Amber
        ledSetCustomRgb(255, 180, 0);
      } else if (m == 8 || !doc["r"].isNull()) { // Custom RGB from color wheel or direct RGB
        uint8_t r = doc["r"] | 0;
        uint8_t g = doc["g"] | 0;
        uint8_t b = doc["b"] | 0;
        ledSetCustomRgb(r, g, b);
      } else {
        ledSetOverride(m);
      }
    } else if (!doc["r"].isNull()) {
      uint8_t r = doc["r"] | 0;
      uint8_t g = doc["g"] | 0;
      uint8_t b = doc["b"] | 0;
      ledSetCustomRgb(r, g, b);
    }
    if (!doc["brightness"].isNull()) {
      ledSetBrightness(doc["brightness"]);
    }
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static bool   display_upload_success = false;
static size_t display_upload_bytes   = 0;

static void handleDisplayImageLoop() {
  HTTPUpload& upload = server.upload();
  uint16_t* img = uiGetCustomImageBuffer();
  if (!img) return;

  const size_t targetBytes = 160 * 128 * 2;
  if (upload.status == UPLOAD_FILE_START) {
    display_upload_success = false;
    display_upload_bytes = 0;
    Serial.println(F("[DISP] Image upload start"));
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (display_upload_bytes < targetBytes) {
      size_t copyLen = upload.currentSize;
      if (display_upload_bytes + copyLen > targetBytes) {
        copyLen = targetBytes - display_upload_bytes;
      }
      memcpy(((uint8_t*)img) + display_upload_bytes, upload.buf, copyLen);
      display_upload_bytes += copyLen;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (display_upload_bytes >= targetBytes) {
      display_upload_success = true;
      uiActivateCustomImage();
      Serial.printf("[DISP] Image upload done: %u bytes\n", (unsigned)display_upload_bytes);
    } else if (display_upload_bytes >= (targetBytes - 512)) {
      memset(((uint8_t*)img) + display_upload_bytes, 0, targetBytes - display_upload_bytes);
      display_upload_success = true;
      uiActivateCustomImage();
      Serial.printf("[DISP] Image upload padded: %u / %u bytes\n", (unsigned)display_upload_bytes, (unsigned)targetBytes);
    } else {
      Serial.printf("[DISP] Image upload incomplete: %u / %u bytes\n", (unsigned)display_upload_bytes, (unsigned)targetBytes);
    }
  }
}

static void handleDisplayImageDone() {
  sendCorsHeaders();
  server.sendHeader("Connection", "close");
  if (!display_upload_success && server.hasArg("plain")) {
    uint16_t* img = uiGetCustomImageBuffer();
    if (img) {
      String body = server.arg("plain");
      const size_t targetBytes = 160 * 128 * 2;
      size_t copyLen = body.length() > targetBytes ? targetBytes : body.length();
      memcpy((uint8_t*)img, (const uint8_t*)body.c_str(), copyLen);
      uiActivateCustomImage();
      display_upload_success = true;
    }
  }

  if (display_upload_success) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"No image data received\"}");
  }
}

static void handleDisplayClear() {
  uiClearCustomImage();
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleOtaLoop() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Flashing start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA] Flashed %u bytes OK. Rebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

static void handleOtaDone() {
  server.sendHeader("Connection", "close");
  if (!Update.hasError()) {
    server.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
  } else {
    server.send(500, "text/plain", "FAIL");
  }
}

static void handleReboot() {
  server.send(200, "application/json", "{\"status\":\"rebooting\"}");
  delay(500);
  ESP.restart();
}

static void handleNotify() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char* title = doc["title"] | "Notification";
    const char* msg = doc["msg"] | "";
    uint32_t dur = doc["duration"] | 4000;
    uiShowNotification(title, msg, dur);
  } else if (server.hasArg("title") || server.hasArg("msg")) {
    String title = server.arg("title");
    String msg = server.arg("msg");
    uiShowNotification(title.c_str(), msg.c_str(), 4000);
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleSettingsGet() {
  JsonDocument doc;
  doc["theme_light"]  = g_settings.theme_light;
  doc["clock_12h"]    = g_settings.clock_12h;
  doc["buzzer_mode"]  = g_settings.buzzer_mode;
  doc["brightness"]   = g_settings.brightness;
  doc["led_override"] = g_settings.led_override;
  doc["led_inverted"] = ledGetInverted();
  String out;
  serializeJson(doc, out);
  sendCorsHeaders();
  server.send(200, "application/json", out);
}

static void handleSettingsSet() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    if (!doc["theme_light"].isNull()) {
      g_settings.theme_light = doc["theme_light"];
      uiSetThemeLight(g_settings.theme_light);
    }
    if (!doc["clock_12h"].isNull()) {
      g_settings.clock_12h = doc["clock_12h"];
      uiSetClock12h(g_settings.clock_12h);
    }
    if (!doc["buzzer_mode"].isNull()) {
      g_settings.buzzer_mode = doc["buzzer_mode"];
      setBuzzerMode(g_settings.buzzer_mode);
    }
    if (!doc["brightness"].isNull()) {
      g_settings.brightness = doc["brightness"];
      uiSetBacklight(g_settings.brightness);
    }
    if (!doc["led_inverted"].isNull()) {
      ledSetInverted(doc["led_inverted"]);
    }
    settingsSave();
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleSpotifyNow() {
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char* title = doc["title"] | doc["name"] | "";
    const char* artist = doc["artist"] | "";
    uint32_t prog = doc["progress_ms"] | 0;
    uint32_t dur = doc["duration_ms"] | 1;
    bool playing = doc["is_playing"] | true;
    const char* img_url = doc["image_url"] | nullptr;
    const char* lyric_cur = doc["lyric_cur"] | nullptr;
    const char* lyric_next = doc["lyric_next"] | nullptr;
    spotifySetTrackInfo(title, artist, prog, dur, playing, img_url, lyric_cur, lyric_next);
  }
  sendCorsHeaders();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static uint8_t s_cover_upload_buf[56 * 56 * 2];
static size_t  s_cover_upload_len = 0;

static void handleSpotifyCoverLoop() {
  HTTPUpload& upload = server.upload();
  const size_t targetBytes = 56 * 56 * 2;
  if (upload.status == UPLOAD_FILE_START) {
    s_cover_upload_len = 0;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_cover_upload_len < targetBytes) {
      size_t copyLen = upload.currentSize;
      if (s_cover_upload_len + copyLen > targetBytes) {
        copyLen = targetBytes - s_cover_upload_len;
      }
      memcpy(s_cover_upload_buf + s_cover_upload_len, upload.buf, copyLen);
      s_cover_upload_len += copyLen;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (s_cover_upload_len == targetBytes) {
      spotifySetCoverArt((const uint16_t*)s_cover_upload_buf);
      Serial.println(F("[SPOTIFY] Cover art received via upload"));
    }
  }
}

static void handleSpotifyCoverDone() {
  sendCorsHeaders();
  if (s_cover_upload_len == 56 * 56 * 2) {
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    return;
  }
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    const size_t reqSize = 56 * 56 * 2;
    if (body.length() >= reqSize) {
      spotifySetCoverArt((const uint16_t*)body.c_str());
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleSpotifyToken() {
  if (server.method() == HTTP_POST && server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char* token = doc["refresh_token"] | "";
    if (token && token[0]) {
      spotifySetRefreshToken(token);
    }
  }
  JsonDocument doc;
  doc["has_token"] = (spotifyGetRefreshToken()[0] != 0);
  doc["is_active"] = spotifyIsActive();
  String out;
  serializeJson(doc, out);
  sendCorsHeaders();
  server.send(200, "application/json", out);
}

// ---------------- Server Task & Lifecycle -----------------------------
void webServerLoop() {
  server.handleClient();
}

static void webTask(void* /*param*/) {
  Serial.println(F("[WEB] webTask started, waiting for WiFi..."));
  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  Serial.printf("[WEB] WiFi ready! IP=%s. Registering routes...\n", WiFi.localIP().toString().c_str());
  WiFi.setSleep(false);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/control/screen", HTTP_GET, handleScreen);
  server.on("/api/control/screen_rle", HTTP_GET, handleScreenRle);
  server.on("/api/control/button", HTTP_POST, handleButton);
  server.on("/api/buzzer/test", HTTP_GET, handleBuzzerTest);
  server.on("/api/buzzer/test", HTTP_POST, handleBuzzerTest);
  server.on("/api/buzzer/mode", HTTP_GET, handleBuzzerMode);
  server.on("/api/buzzer/mode", HTTP_POST, handleBuzzerMode);
  server.on("/api/fs/list", HTTP_GET, handleFsList);
  server.on("/api/fs/download", HTTP_GET, handleFsDownload);
  server.on("/api/fs/delete", HTTP_POST, handleFsDelete);
  server.on("/api/fs/upload", HTTP_POST, handleFsUploadDone, handleFsUploadLoop);
  server.on("/api/led/set", HTTP_POST, handleLedSet);
  server.on("/api/display/image", HTTP_POST, handleDisplayImageDone, handleDisplayImageLoop);
  server.on("/api/display/clear", HTTP_POST, handleDisplayClear);
  server.on("/api/system/update", HTTP_POST, handleOtaDone, handleOtaLoop);
  server.on("/api/system/reboot", HTTP_POST, handleReboot);
  server.on("/api/notify", HTTP_POST, handleNotify);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsSet);
  server.on("/api/spotify/now", HTTP_POST, handleSpotifyNow);
  server.on("/api/spotify/cover", HTTP_POST, handleSpotifyCoverDone, handleSpotifyCoverLoop);
  server.on("/api/spotify/token", HTTP_POST, handleSpotifyToken);
  server.on("/api/spotify/token", HTTP_GET, handleSpotifyToken);

  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      sendCorsHeaders();
      server.send(404, "text/plain", "Not Found");
    }
  });

  spotifyServiceInit();
  server.begin();
  Serial.println(F("[WEB] STΔN studio. HTTP control server live on :80"));

  while (true) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2)); // snappy 2ms yield gives sub-5ms response
  }
}

void webServerTaskStart() {
  xTaskCreatePinnedToCore(
      webTask, "webTask", 8192, nullptr, 2, nullptr, 0 /* Core 0 - priority 2 */);
}
