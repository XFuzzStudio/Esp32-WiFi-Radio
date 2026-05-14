#include <Arduino.h>
#include <DNSServer.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <lvgl.h>
#include <stdarg.h>

#include "../shared/esp32_bin_loader_return.h"
#include "../shared/lcdwiki_es3c28p/arduino_gfx_display.h"
#include "../shared/lcdwiki_es3c28p/lcdwiki_es3c28p_config.h"
#include "../shared/lcdwiki_es3c28p/touch_ft6336.h"

static constexpr char APP_NAME[] = "ESP-WiFi-Scanner";
static constexpr char APP_VERSION[] = "1.1";
static constexpr char DATA_DIR[] = "/apps_data/ESP-WiFi-Scanner";
static constexpr char LOG_DIR[] = "/apps_data/ESP-WiFi-Scanner/logs";
static constexpr char LOG_FILE[] = "/apps_data/ESP-WiFi-Scanner/logs/scanner.log";
static constexpr char WIFI_CREDS_FILE[] = "/apps_data/ESP-WiFi-Scanner/wifi_creds.csv";
static constexpr uint16_t W = LCDWIKI_ES3C28P_SCREEN_WIDTH;
static constexpr uint16_t H = LCDWIKI_ES3C28P_SCREEN_HEIGHT;
static constexpr uint8_t LV_ROWS = 32;
static constexpr uint16_t COMMON_PORTS[] = {21, 22, 23, 25, 53, 80, 110, 123, 139, 143, 443, 445, 554, 587, 1883, 3306, 3389, 5353, 5432, 8080, 8443};

LcdWikiEs3c28pDisplay display;
LcdWikiFt6336Touch touch;
WebServer server(80);
DNSServer dns;
lv_display_t *lvDisp = nullptr;
lv_indev_t *lvInput = nullptr;
lv_obj_t *root = nullptr;
lv_obj_t *statusLbl = nullptr;
lv_obj_t *busyDot = nullptr;
lv_obj_t *bodyBox = nullptr;
lv_obj_t *bodyLbl = nullptr;
lv_obj_t *logBtn = nullptr;
lv_obj_t *ssidTa = nullptr;
lv_obj_t *passTa = nullptr;
lv_obj_t *portsTa = nullptr;
lv_obj_t *kb = nullptr;
uint16_t drawBuf[W * LV_ROWS];
bool sdReady = false;
bool dataReady = false;
bool apActive = false;
bool loggingEnabled = false;
bool busy = false;
bool wifiSelectionActive = false;
String lastLog = "Starting";
String wifiText;
String hostText;
String portText;
IPAddress selectedHost;
String scannedSsids[12];
int scannedWifiCount = 0;

void syncBody();
void updateHeaderStatus();
void setBusy(bool value);

String csvEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("|", "\\|");
  s.replace("\n", " ");
  return s;
}

String csvUnescape(String s) {
  String out;
  bool escNext = false;
  for (uint16_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (escNext) {
      out += c;
      escNext = false;
    } else if (c == '\\') {
      escNext = true;
    } else {
      out += c;
    }
  }
  return out;
}

String savedPasswordFor(const String &ssid) {
  if (!sdReady || !dataReady || !SD_MMC.exists(WIFI_CREDS_FILE)) return "";
  File f = SD_MMC.open(WIFI_CREDS_FILE, FILE_READ);
  if (!f) return "";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line.startsWith("#")) continue;
    const int sep = line.indexOf('|');
    if (sep <= 0) continue;
    if (csvUnescape(line.substring(0, sep)) == ssid) {
      String pass = csvUnescape(line.substring(sep + 1));
      f.close();
      return pass;
    }
  }
  f.close();
  return "";
}

void saveWifiCredential(const String &ssid, const String &pass) {
  if (!sdReady || !dataReady || !ssid.length()) return;
  String existing;
  if (SD_MMC.exists(WIFI_CREDS_FILE)) {
    File in = SD_MMC.open(WIFI_CREDS_FILE, FILE_READ);
    if (in) {
      while (in.available()) {
        String line = in.readStringUntil('\n');
        String trimmed = line;
        trimmed.trim();
        const int sep = trimmed.indexOf('|');
        if (sep > 0 && csvUnescape(trimmed.substring(0, sep)) == ssid) continue;
        existing += line;
        if (!existing.endsWith("\n")) existing += "\n";
      }
      in.close();
    }
  }
  File out = SD_MMC.open(WIFI_CREDS_FILE, "w");
  if (!out) return;
  out.print(existing);
  out.print(csvEscape(ssid));
  out.print("|");
  out.println(csvEscape(pass));
  out.close();
}

void logLine(const char *fmt, ...) {
  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial0.println(buf);
  lastLog = buf;
  if (loggingEnabled && sdReady && dataReady) {
    File f = SD_MMC.open(LOG_FILE, FILE_APPEND);
    if (f) {
      f.printf("%lu %s\n", millis(), buf);
      f.close();
    }
  }
  if (bodyLbl) syncBody();
}

String wifiHeaderText() {
  if (WiFi.status() == WL_CONNECTED) {
    return String("WiFi ") + WiFi.SSID() + "  " + WiFi.localIP().toString();
  }
  if (apActive) {
    return String("AP ") + WiFi.softAPIP().toString();
  }
  return "WiFi not connected";
}

void updateHeaderStatus() {
  if (statusLbl) {
    lv_label_set_text(statusLbl, wifiHeaderText().c_str());
  }
}

void setBusy(bool value) {
  busy = value;
  if (busyDot) {
    if (busy) {
      lv_obj_clear_flag(busyDot, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(busyDot, LV_OBJ_FLAG_HIDDEN);
    }
  }
  updateHeaderStatus();
  lv_timer_handler();
}

String esc(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

void initSd() {
  sdReady = false;
  dataReady = false;
  SD_MMC.end();
  SD_MMC.setPins(LCDWIKI_ES3C28P_SD_CLK, LCDWIKI_ES3C28P_SD_CMD, LCDWIKI_ES3C28P_SD_D0, LCDWIKI_ES3C28P_SD_D1, LCDWIKI_ES3C28P_SD_D2, LCDWIKI_ES3C28P_SD_D3);
  sdReady = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT, 8);
  if (!sdReady) {
    SD_MMC.end();
    SD_MMC.setPins(LCDWIKI_ES3C28P_SD_CLK, LCDWIKI_ES3C28P_SD_CMD, LCDWIKI_ES3C28P_SD_D0);
    sdReady = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 8);
  }
  if (sdReady) {
    if (!SD_MMC.exists("/apps_data")) SD_MMC.mkdir("/apps_data");
    if (!SD_MMC.exists(DATA_DIR)) SD_MMC.mkdir(DATA_DIR);
    if (!SD_MMC.exists(LOG_DIR)) SD_MMC.mkdir(LOG_DIR);
    dataReady = SD_MMC.exists(LOG_DIR);
    if (dataReady && !SD_MMC.exists(WIFI_CREDS_FILE)) {
      File f = SD_MMC.open(WIFI_CREDS_FILE, "w");
      if (f) {
        f.println("# ssid|password");
        f.close();
      }
    }
  }
}

void flushCb(lv_display_t *, const lv_area_t *a, uint8_t *px) {
  display.gfx()->draw16bitRGBBitmap(a->x1, a->y1, reinterpret_cast<uint16_t *>(px), a->x2 - a->x1 + 1, a->y2 - a->y1 + 1);
  lv_display_flush_ready(lvDisp);
}

void touchCb(lv_indev_t *, lv_indev_data_t *d) {
  uint16_t x, y;
  if (touch.read(x, y)) {
    d->state = LV_INDEV_STATE_PRESSED;
    d->point.x = x;
    d->point.y = y;
  } else {
    d->state = LV_INDEV_STATE_RELEASED;
  }
}

lv_obj_t *btn(const char *t, int x, int y, int w, int h, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(root);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0xEEF2F7), 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 5, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, lv_color_hex(0x111827), 0);
  lv_obj_center(l);
  return b;
}

void syncBody() {
  String s = String("Log: ") + lastLog + "\n";
  if (wifiSelectionActive) {
    s += "Tap WiFi row to select:\n";
    s += wifiText.length() ? wifiText : "No WiFi scan yet";
  } else if (hostText.length()) {
    s += hostText;
  } else if (portText.length()) {
    s += portText;
  } else if (wifiText.length()) {
    s += wifiText;
  } else {
    s += "Ready";
  }
  if (!dataReady) s = String("ERROR: SD card or ") + DATA_DIR + " missing.\nThis app requires SD logs folder.";
  lv_label_set_text(bodyLbl, s.c_str());
}

void setButtonLabel(lv_obj_t *button, const char *text) {
  if (!button) return;
  lv_obj_t *label = lv_obj_get_child(button, 0);
  if (label) lv_label_set_text(label, text);
}

void toggleLogging() {
  loggingEnabled = !loggingEnabled;
  setButtonLabel(logBtn, loggingEnabled ? "Log ON" : "Log OFF");
  logLine("Scan logging %s", loggingEnabled ? "enabled" : "disabled");
  syncBody();
}

void scanWifi() {
  setBusy(true);
  wifiSelectionActive = true;
  wifiText = "";
  hostText = "";
  portText = "";
  scannedWifiCount = 0;
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n && i < 12; i++) {
    scannedSsids[scannedWifiCount++] = WiFi.SSID(i);
    wifiText += String(i) + ": " + WiFi.SSID(i) + " " + WiFi.RSSI(i) + "dBm";
    if (savedPasswordFor(WiFi.SSID(i)).length()) wifiText += " saved";
    wifiText += "\n";
  }
  logLine("WiFi scan: %d networks", n);
  syncBody();
  setBusy(false);
}

void connectFromFields() {
  setBusy(true);
  wifiSelectionActive = false;
  String ssid = lv_textarea_get_text(ssidTa);
  String pass = lv_textarea_get_text(passTa);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    lv_timer_handler();
    delay(50);
  }
  if (WiFi.status() == WL_CONNECTED) {
    saveWifiCredential(ssid, pass);
    wifiText = String("Connected ") + WiFi.localIP().toString() + " RSSI " + WiFi.RSSI();
  } else {
    wifiText = "Connect failed";
  }
  logLine("%s", wifiText.c_str());
  updateHeaderStatus();
  syncBody();
  setBusy(false);
}

bool tcpProbe(IPAddress ip, uint16_t port, uint16_t timeoutMs) {
  WiFiClient c;
  c.setTimeout(timeoutMs);
  bool ok = c.connect(ip, port, timeoutMs);
  c.stop();
  return ok;
}

void scanHosts() {
  setBusy(true);
  wifiSelectionActive = false;
  hostText = "";
  portText = "";
  if (WiFi.status() != WL_CONNECTED) {
    hostText = "No STA connection";
    syncBody();
    setBusy(false);
    return;
  }
  IPAddress base = WiFi.localIP();
  uint8_t found = 0;
  for (int i = 1; i < 255; i++) {
    IPAddress ip(base[0], base[1], base[2], i);
    if (ip == WiFi.localIP()) continue;
    if (tcpProbe(ip, 80, 45) || tcpProbe(ip, 443, 45) || tcpProbe(ip, 22, 45)) {
      selectedHost = ip;
      hostText += ip.toString() + "\n";
      found++;
      syncBody();
      lv_timer_handler();
    }
    delay(1);
  }
  logLine("Host scan done: %u live hosts", found);
  syncBody();
  setBusy(false);
}

void appendPort(uint16_t p) {
  if (!selectedHost) selectedHost = WiFi.gatewayIP();
  if (tcpProbe(selectedHost, p, 180)) {
    portText += String(p) + " open\n";
    logLine("%s:%u open", selectedHost.toString().c_str(), p);
  }
}

void scanPorts() {
  setBusy(true);
  wifiSelectionActive = false;
  hostText = "";
  portText = String("Target ") + (selectedHost ? selectedHost.toString() : WiFi.gatewayIP().toString()) + "\n";
  String custom = lv_textarea_get_text(portsTa);
  custom.trim();
  if (custom.length()) {
    int start = 0;
    while (start < custom.length()) {
      int comma = custom.indexOf(',', start);
      if (comma < 0) comma = custom.length();
      int p = custom.substring(start, comma).toInt();
      if (p > 0 && p < 65536) appendPort(p);
      start = comma + 1;
    }
  } else {
    for (uint16_t p : COMMON_PORTS) appendPort(p);
  }
  logLine("Port scan done");
  syncBody();
  setBusy(false);
}

void startAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  String name = String(APP_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);
  apActive = WiFi.softAP(name.c_str(), "scanner123");
  if (apActive) dns.start(53, "*", WiFi.softAPIP());
}

void rootPage() {
  String h = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><style>body{font-family:system-ui;background:#101820;color:#edf;margin:20px}button,input{width:100%;padding:10px;margin:5px 0}pre{white-space:pre-wrap;background:#17222b;padding:12px}</style>";
  h += "<h1>Network tools 1.1</h1><p>" + esc(wifiHeaderText()) + "</p><p>" + esc(lastLog) + "</p>";
  h += String("<p>Logging: ") + (loggingEnabled ? "ON" : "OFF") + "</p><form method=post action=/logtoggle><button>Toggle scan log</button></form>";
  h += "<form method=post action=/wifi><input name=ssid placeholder=SSID><input name=pass placeholder=Password type=password><button>Connect</button></form>";
  h += "<form method=post action=/scanwifi><button>Scan WiFi</button></form><form method=post action=/scanhosts><button>Scan Hosts</button></form>";
  h += "<form method=post action=/scanports><input name=ports placeholder='Custom ports CSV, blank=popular'><button>Scan Ports</button></form>";
  h += "<h2>Results</h2><pre>" + esc(wifiText + "\n" + hostText + "\n" + portText) + "</pre><p><a href=/log>Download log</a></p>";
  server.send(200, "text/html", h);
}

void setupRoutes() {
  server.on("/", rootPage);
  server.on("/wifi", HTTP_POST, [](){ String ssid=server.arg("ssid"); String pass=server.arg("pass"); WiFi.mode(WIFI_AP_STA); WiFi.begin(ssid.c_str(), pass.c_str()); saveWifiCredential(ssid, pass); server.sendHeader("Location","/"); server.send(302); });
  server.on("/scanwifi", HTTP_POST, [](){ scanWifi(); server.sendHeader("Location","/"); server.send(302); });
  server.on("/scanhosts", HTTP_POST, [](){ scanHosts(); server.sendHeader("Location","/"); server.send(302); });
  server.on("/scanports", HTTP_POST, [](){ if (server.hasArg("ports")) lv_textarea_set_text(portsTa, server.arg("ports").c_str()); scanPorts(); server.sendHeader("Location","/"); server.send(302); });
  server.on("/logtoggle", HTTP_POST, [](){ toggleLogging(); server.sendHeader("Location","/"); server.send(302); });
  server.on("/log", [](){ if (!SD_MMC.exists(LOG_FILE)) { server.send(404,"text/plain","No log"); return; } File f=SD_MMC.open(LOG_FILE); server.streamFile(f,"text/plain"); f.close(); });
}

void focusCb(lv_event_t *e) {
  lv_obj_t *ta = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

void hideKeyboard() {
  if (!kb) return;
  lv_keyboard_set_textarea(kb, nullptr);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

void rootClickCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
  if (target == root || target == bodyBox || target == bodyLbl) {
    hideKeyboard();
  }
}

void outputClickCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !wifiSelectionActive || scannedWifiCount <= 0) return;
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a;
  lv_obj_get_coords(bodyBox, &a);
  const int line = ((p.y - a.y1) + lv_obj_get_scroll_y(bodyBox)) / 16;
  const int wifiIndex = line - 2;
  if (wifiIndex >= 0 && wifiIndex < scannedWifiCount) {
    const String ssid = scannedSsids[wifiIndex];
    lv_textarea_set_text(ssidTa, ssid.c_str());
    String pass = savedPasswordFor(ssid);
    if (pass.length()) {
      lv_textarea_set_text(passTa, pass.c_str());
      logLine("Selected saved WiFi: %s", ssid.c_str());
    } else {
      lv_textarea_set_text(passTa, "");
      logLine("Selected WiFi: %s", ssid.c_str());
    }
  }
}

void setupTextArea(lv_obj_t *ta) {
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_cursor_click_pos(ta, true);
  lv_obj_set_style_bg_color(ta, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(0x111827), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, 5, 0);
  lv_obj_clear_flag(ta, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ta, focusCb, LV_EVENT_FOCUSED, nullptr);
}

void buildUi() {
  root = lv_screen_active();
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root, rootClickCb, LV_EVENT_CLICKED, nullptr);
  statusLbl = lv_label_create(root);
  lv_obj_set_pos(statusLbl, 8, 8);
  lv_obj_set_width(statusLbl, 194);
  lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x111827), 0);
  busyDot = lv_obj_create(root);
  lv_obj_set_pos(busyDot, 212, 8);
  lv_obj_set_size(busyDot, 18, 18);
  lv_obj_set_style_radius(busyDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(busyDot, lv_color_hex(0xDBEAFE), 0);
  lv_obj_set_style_border_color(busyDot, lv_color_hex(0x2563EB), 0);
  lv_obj_set_style_border_width(busyDot, 3, 0);
  lv_obj_clear_flag(busyDot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(busyDot, LV_OBJ_FLAG_HIDDEN);
  ssidTa = lv_textarea_create(root);
  lv_obj_set_pos(ssidTa, 8, 34);
  lv_obj_set_size(ssidTa, 108, 30);
  lv_textarea_set_placeholder_text(ssidTa, "SSID");
  setupTextArea(ssidTa);
  passTa = lv_textarea_create(root);
  lv_obj_set_pos(passTa, 124, 34);
  lv_obj_set_size(passTa, 108, 30);
  lv_textarea_set_password_mode(passTa, true);
  lv_textarea_set_placeholder_text(passTa, "Pass");
  setupTextArea(passTa);
  portsTa = lv_textarea_create(root);
  lv_obj_set_pos(portsTa, 8, 70);
  lv_obj_set_size(portsTa, 224, 30);
  lv_textarea_set_placeholder_text(portsTa, "Ports CSV or blank");
  setupTextArea(portsTa);
  btn("WiFi", 8, 106, 52, 30, [](lv_event_t *){ scanWifi(); });
  btn("Conn", 64, 106, 52, 30, [](lv_event_t *){ connectFromFields(); });
  btn("Hosts", 120, 106, 52, 30, [](lv_event_t *){ scanHosts(); });
  btn("Ports", 176, 106, 56, 30, [](lv_event_t *){ scanPorts(); });
  logBtn = btn("Log OFF", 8, 142, 224, 24, [](lv_event_t *){ toggleLogging(); });
  bodyBox = lv_obj_create(root);
  lv_obj_set_pos(bodyBox, 8, 172);
  lv_obj_set_size(bodyBox, 224, 140);
  lv_obj_set_style_bg_color(bodyBox, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(bodyBox, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_border_width(bodyBox, 1, 0);
  lv_obj_set_style_radius(bodyBox, 5, 0);
  lv_obj_set_scroll_dir(bodyBox, LV_DIR_VER);
  lv_obj_add_flag(bodyBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bodyBox, rootClickCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(bodyBox, outputClickCb, LV_EVENT_CLICKED, nullptr);
  bodyLbl = lv_label_create(bodyBox);
  lv_obj_set_width(bodyLbl, 204);
  lv_label_set_long_mode(bodyLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(bodyLbl, lv_color_hex(0x111827), 0);
  lv_obj_add_flag(bodyLbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bodyLbl, rootClickCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(bodyLbl, outputClickCb, LV_EVENT_CLICKED, nullptr);
  kb = lv_keyboard_create(root);
  lv_obj_set_size(kb, 240, 108);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  updateHeaderStatus();
  syncBody();
}

void setup() {
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  esp32BinLoaderReturnToFactoryOnNextBoot();
  display.begin();
  touch.begin();
  initSd();
  lv_init();
  lvDisp = lv_display_create(W, H);
  lv_display_set_color_format(lvDisp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisp, flushCb);
  lv_display_set_buffers(lvDisp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lvInput = lv_indev_create();
  lv_indev_set_type(lvInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvInput, touchCb);
  buildUi();
  startAp();
  setupRoutes();
  server.begin();
  logLine("%s %s AP %s", APP_NAME, APP_VERSION, WiFi.softAPIP().toString().c_str());
  updateHeaderStatus();
}

void loop() {
  lv_tick_inc(5);
  lv_timer_handler();
  server.handleClient();
  if (apActive) dns.processNextRequest();
  updateHeaderStatus();
  delay(5);
}
