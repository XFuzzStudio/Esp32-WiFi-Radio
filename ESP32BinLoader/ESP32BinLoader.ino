#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_ota_ops.h>

// ESP32 Bin Loader for LCDWiki ESP32-S3 ES3C28P.
// Reads firmware images from SD, installs the selected .bin with OTA APIs,
// and provides a rescue web UI for Wi-Fi setup plus SD card file management.
// Launched apps should set the next boot target back to factory so resets
// always return to this loader.

static constexpr int DEBUG_UART_RX = 44;
static constexpr int DEBUG_UART_TX = 43;
static constexpr int LCD_CS = 10;
static constexpr int LCD_DC = 46;
static constexpr int LCD_SCK = 12;
static constexpr int LCD_MOSI = 11;
static constexpr int LCD_MISO = 13;
static constexpr int LCD_RST = -1;
static constexpr int LCD_BL = 45;
static constexpr int TOUCH_SDA = 16;
static constexpr int TOUCH_SCL = 15;
static constexpr int TOUCH_RST = 18;
static constexpr int TOUCH_INT = 17;
static constexpr uint8_t TOUCH_ADDR = 0x38;
static constexpr int SD_CLK = 38;
static constexpr int SD_CMD = 40;
static constexpr int SD_D0 = 39;
static constexpr int SD_D1 = 41;
static constexpr int SD_D2 = 48;
static constexpr int SD_D3 = 47;
static constexpr uint32_t WIFI_TIMEOUT_MS = 14000;
static constexpr uint32_t TOUCH_DEBOUNCE_MS = 180;
static constexpr byte DNS_PORT = 53;
static constexpr uint8_t MAX_APPS = 16;
static constexpr char AP_PASS[] = "launcher123";
static constexpr char MANIFEST_FILE[] = "/apps/manifest.txt";

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_NETMASK(255, 255, 255, 0);

struct AppEntry {
  String label;
  String file;
  String notes;
};

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
File uploadFile;
Arduino_DataBus *displayBus = nullptr;
Arduino_GFX *gfx = nullptr;
AppEntry apps[MAX_APPS];

bool sdReady = false;
bool apActive = false;
bool connectPending = false;
bool displayReady = false;
bool touchReady = false;
bool uiDirty = true;
bool installing = false;
uint8_t appCount = 0;
uint8_t selectedApp = 0;
uint8_t topApp = 0;
uint32_t lastTouchMs = 0;
char apName[32] = {0};
char statusText[96] = "Starting";

class Ft6336Touch {
public:
  bool begin() {
    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(120);
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000UL);
    return readRegister(0xA8) || readRegister(0xA3);
  }

  bool read(uint16_t &x, uint16_t &y) {
    if (!writeRegisterPointer(0x02)) {
      return false;
    }
    if (Wire.requestFrom(TOUCH_ADDR, static_cast<uint8_t>(5)) != 5) {
      return false;
    }
    const uint8_t points = Wire.read() & 0x0F;
    if (!points) {
      return false;
    }
    const uint8_t xh = Wire.read();
    const uint8_t xl = Wire.read();
    const uint8_t yh = Wire.read();
    const uint8_t yl = Wire.read();
    x = static_cast<uint16_t>(((xh & 0x0F) << 8) | xl);
    y = static_cast<uint16_t>(((yh & 0x0F) << 8) | yl);
    return x < 240 && y < 320;
  }

private:
  bool writeRegisterPointer(uint8_t reg) {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(reg);
    return Wire.endTransmission(true) == 0;
  }

  bool readRegister(uint8_t reg) {
    if (!writeRegisterPointer(reg)) {
      return false;
    }
    if (Wire.requestFrom(TOUCH_ADDR, static_cast<uint8_t>(1)) != 1) {
      return false;
    }
    (void)Wire.read();
    return true;
  }
};

Ft6336Touch touch;

void logf(const char *fmt, ...);
void initDebug();
void initDisplay();
void initApName();
void initSd();
void setStatus(const char *fmt, ...);
void loadApps();
void addApp(String label, String file, String notes);
void addManifestLine(String line);
void scanBinApps();
void selectDelta(int delta);
void launchSelectedApp();
bool installAppFromSd(const String &path);
void drawUi();
void drawInstallProgress(const String &label, uint8_t percent);
void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String &label, uint16_t bg, uint16_t fg);
void drawTextClip(int16_t x, int16_t y, int16_t w, const String &text, uint16_t color, uint8_t size);
String clipped(const String &text, uint16_t pixels, uint8_t size);
void handleTouch();
void handleSerial();
void processCommand(String cmd);
bool inRect(uint16_t x, uint16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh);
bool connectWifi(bool verbose = true);
void startAp();
void setupRoutes();
void serviceNetwork();
String htmlEscape(String value);
String cleanPath(String path);
String parentPath(const String &path);
String contentTypeFor(const String &path);
String networkStatus();
String sdStatus();
void appendPageHeader(String &html, const char *title);
void appendPageFooter(String &html);
void appendFileRows(String &html, const String &path);
void handleRoot();
void handleWifiSave();
void handleSd();
void handleDownload();
void handleDelete();
void handleMkdir();
void handleUploadDone();
void handleUpload();
void handleLaunch();
void handleApStart();
void handleReboot();
void handleNotFound();

void logf(const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  Serial0.println(buffer);
}

void initDebug() {
  Serial0.begin(115200, SERIAL_8N1, DEBUG_UART_RX, DEBUG_UART_TX);
  delay(100);
  Serial0.println();
  Serial0.println("ESP32 Bin Loader 1.1");
}

void setStatus(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(statusText, sizeof(statusText), fmt, args);
  va_end(args);
  logf("%s", statusText);
  uiDirty = true;
}

void initDisplay() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  displayBus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);
  gfx = new Arduino_ILI9341(displayBus, LCD_RST, 0, false, 240, 320);
  displayReady = gfx && gfx->begin(27000000UL);
  if (displayReady) {
    gfx->setRotation(0);
    gfx->invertDisplay(true);
    gfx->fillScreen(0x0000);
  }

  touchReady = touch.begin();
  logf("Display: %s, touch: %s", displayReady ? "ok" : "failed", touchReady ? "ok" : "missing");
}

void initApName() {
  const uint32_t chip = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(apName, sizeof(apName), "ESP32BinLoader-%04X", chip & 0xFFFF);
}

void initSd() {
  sdReady = false;
  SD_MMC.end();
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  sdReady = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT, 8);
  if (!sdReady) {
    SD_MMC.end();
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
    sdReady = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 8);
  }
  if (sdReady) {
    if (!SD_MMC.exists("/apps")) {
      SD_MMC.mkdir("/apps");
    }
    logf("SD mounted: %llu MB", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    loadApps();
  } else {
    logf("SD mount failed");
    setStatus("SD mount failed");
  }
}

void addApp(String label, String file, String notes) {
  if (appCount >= MAX_APPS) {
    return;
  }
  label.trim();
  file.trim();
  notes.trim();
  if (!label.length() || !file.length()) {
    return;
  }
  if (file.indexOf("..") >= 0 || file.indexOf('/') >= 0 || file.indexOf('\\') >= 0) {
    return;
  }
  apps[appCount].label = label;
  apps[appCount].file = file;
  apps[appCount].notes = notes;
  appCount++;
}

void addManifestLine(String line) {
  line.trim();
  if (!line.length() || line.startsWith("#")) {
    return;
  }
  const int first = line.indexOf('|');
  if (first <= 0) {
    return;
  }
  const int second = line.indexOf('|', first + 1);
  const String label = line.substring(0, first);
  const String file = second > 0 ? line.substring(first + 1, second) : line.substring(first + 1);
  const String notes = second > 0 ? line.substring(second + 1) : "";
  addApp(label, file, notes);
}

void scanBinApps() {
  if (!sdReady) {
    return;
  }
  File dir = SD_MMC.open("/apps");
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return;
  }
  while (appCount < MAX_APPS) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory()) {
      String name = entry.name();
      name = name.substring(name.lastIndexOf('/') + 1);
      String lower = name;
      lower.toLowerCase();
      if (lower.endsWith(".bin")) {
        addApp(name, name, "Detected .bin");
      }
    }
    entry.close();
  }
  dir.close();
}

void loadApps() {
  appCount = 0;
  selectedApp = 0;
  topApp = 0;
  if (!sdReady) {
    uiDirty = true;
    return;
  }

  if (SD_MMC.exists(MANIFEST_FILE)) {
    File f = SD_MMC.open(MANIFEST_FILE, FILE_READ);
    if (f) {
      while (f.available() && appCount < MAX_APPS) {
        addManifestLine(f.readStringUntil('\n'));
      }
      f.close();
    }
  }
  if (!appCount) {
    scanBinApps();
  }
  setStatus(appCount ? "Apps loaded: %u" : "No apps in /apps", appCount);
}

bool connectWifi(bool verbose) {
  const String ssid = prefs.getString("ssid", "");
  const String pass = prefs.getString("pass", "");
  if (!ssid.length()) {
    if (verbose) {
      logf("No saved WiFi");
    }
    return false;
  }

  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  if (verbose) {
    logf("Connecting WiFi: %s", ssid.c_str());
  }

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    logf("WiFi connected: %s", WiFi.localIP().toString().c_str());
    return true;
  }
  logf("WiFi connect failed");
  return false;
}

void startAp() {
  if (apActive) {
    return;
  }
  WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  apActive = WiFi.softAP(apName, AP_PASS);
  if (apActive) {
    dnsServer.start(DNS_PORT, "*", AP_IP);
    logf("AP active: %s pass %s ip %s", apName, AP_PASS, AP_IP.toString().c_str());
  } else {
    logf("AP start failed");
  }
}

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  return value;
}

String cleanPath(String path) {
  path.trim();
  path.replace("\\", "/");
  if (!path.length() || path[0] != '/') {
    path = "/" + path;
  }
  while (path.indexOf("//") >= 0) {
    path.replace("//", "/");
  }
  if (path.indexOf("..") >= 0) {
    return "/";
  }
  if (path.length() > 1 && path.endsWith("/")) {
    path.remove(path.length() - 1);
  }
  return path;
}

String parentPath(const String &path) {
  if (path == "/") {
    return "/";
  }
  const int slash = path.lastIndexOf('/');
  if (slash <= 0) {
    return "/";
  }
  return path.substring(0, slash);
}

String contentTypeFor(const String &path) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html";
  if (lower.endsWith(".txt") || lower.endsWith(".log") || lower.endsWith(".csv")) return "text/plain";
  if (lower.endsWith(".json")) return "application/json";
  if (lower.endsWith(".bin")) return "application/octet-stream";
  if (lower.endsWith(".bmp")) return "image/bmp";
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  return "application/octet-stream";
}

String networkStatus() {
  String text;
  if (WiFi.status() == WL_CONNECTED) {
    text += "STA ";
    text += WiFi.localIP().toString();
    text += " RSSI ";
    text += WiFi.RSSI();
    text += " dBm";
  } else {
    text += "STA offline";
  }
  if (apActive) {
    text += " / AP ";
    text += AP_IP.toString();
  }
  return text;
}

String sdStatus() {
  if (!sdReady) {
    return "not mounted";
  }
  String text = String(SD_MMC.cardSize() / (1024ULL * 1024ULL));
  text += " MB";
  return text;
}

String clipped(const String &text, uint16_t pixels, uint8_t size) {
  const uint16_t charW = 6 * size;
  if (!charW || text.length() * charW <= pixels) {
    return text;
  }
  const uint16_t maxChars = pixels / charW;
  if (maxChars <= 3) {
    return text.substring(0, maxChars);
  }
  return text.substring(0, maxChars - 3) + "...";
}

void drawTextClip(int16_t x, int16_t y, int16_t w, const String &text, uint16_t color, uint8_t size) {
  if (!displayReady) {
    return;
  }
  gfx->setTextWrap(false);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(clipped(text, w, size));
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String &label, uint16_t bg, uint16_t fg) {
  if (!displayReady) {
    return;
  }
  gfx->fillRoundRect(x, y, w, h, 7, bg);
  gfx->drawRoundRect(x, y, w, h, 7, 0xFFFF);
  drawTextClip(x + 8, y + (h / 2) - 5, w - 16, label, fg, 1);
}

void drawUi() {
  if (!displayReady || !uiDirty || installing) {
    return;
  }
  uiDirty = false;

  gfx->fillScreen(0x0000);
  gfx->fillRect(0, 0, 240, 44, 0x2104);
  drawTextClip(8, 8, 224, "ESP32 Bin Loader", 0xFFFF, 2);
  drawTextClip(8, 29, 224, networkStatus(), WiFi.status() == WL_CONNECTED ? 0x07E0 : 0xFFE0, 1);

  drawTextClip(8, 54, 224, statusText, 0xC618, 1);
  drawTextClip(8, 68, 224, String("SD: ") + sdStatus(), sdReady ? 0x07E0 : 0xF800, 1);

  const int listY = 88;
  const int rowH = 38;
  for (uint8_t row = 0; row < 4; row++) {
    const uint8_t idx = topApp + row;
    const int y = listY + row * rowH;
    const bool selected = idx == selectedApp && idx < appCount;
    const uint16_t bg = selected ? 0x24BE : 0x18E3;
    gfx->fillRoundRect(8, y, 224, rowH - 5, 7, bg);
    gfx->drawRoundRect(8, y, 224, rowH - 5, 7, selected ? 0xFFFF : 0x7BEF);
    if (idx < appCount) {
      drawTextClip(18, y + 6, 156, apps[idx].label, selected ? 0x0000 : 0xFFFF, 1);
      drawTextClip(18, y + 20, 156, apps[idx].file, selected ? 0x2104 : 0xC618, 1);
      const String mark = SD_MMC.exists(String("/apps/") + apps[idx].file) ? "OK" : "MISS";
      drawTextClip(190, y + 12, 34, mark, mark == "OK" ? 0x07E0 : 0xF800, 1);
    } else {
      drawTextClip(18, y + 12, 190, row == 0 && !appCount ? "No apps on SD" : "", 0x7BEF, 1);
    }
  }

  drawButton(8, 266, 52, 42, "UP", 0x39E7, 0xFFFF);
  drawButton(66, 266, 52, 42, "DOWN", 0x39E7, 0xFFFF);
  drawButton(124, 266, 52, 42, "ENTER", 0x07E0, 0x0000);
  drawButton(182, 266, 50, 42, apActive ? "AP ON" : "AP", apActive ? 0xFD20 : 0x001F, 0xFFFF);
}

void drawInstallProgress(const String &label, uint8_t percent) {
  if (!displayReady) {
    return;
  }
  if (percent > 100) {
    percent = 100;
  }
  gfx->fillScreen(0x0000);
  drawTextClip(16, 50, 208, "Installing app", 0x07FF, 2);
  drawTextClip(16, 86, 208, label, 0xFFFF, 1);
  gfx->drawRect(16, 130, 208, 18, 0x7BEF);
  gfx->fillRect(18, 132, (204 * percent) / 100, 14, 0x07E0);
  drawTextClip(16, 162, 208, String(percent) + "%", 0xC618, 1);
}

void selectDelta(int delta) {
  if (!appCount) {
    return;
  }
  int next = static_cast<int>(selectedApp) + delta;
  if (next < 0) {
    next = appCount - 1;
  } else if (next >= appCount) {
    next = 0;
  }
  selectedApp = static_cast<uint8_t>(next);
  if (selectedApp < topApp) {
    topApp = selectedApp;
  }
  if (selectedApp >= topApp + 4) {
    topApp = selectedApp - 3;
  }
  uiDirty = true;
}

bool installAppFromSd(const String &path) {
  if (!sdReady || !SD_MMC.exists(path)) {
    setStatus("Missing %s", path.c_str());
    return false;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    if (file) {
      file.close();
    }
    setStatus("Cannot open app");
    return false;
  }
  const size_t size = file.size();
  if (size < 4096) {
    file.close();
    setStatus("App file too small");
    return false;
  }

  installing = true;
  drawInstallProgress(path, 0);
  if (!Update.begin(size, U_FLASH)) {
    Update.printError(Serial0);
    file.close();
    installing = false;
    setStatus("Update begin failed");
    return false;
  }

  uint8_t buffer[4096];
  size_t written = 0;
  uint8_t lastPercent = 0;
  while (file.available()) {
    const int n = file.read(buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    if (Update.write(buffer, n) != static_cast<size_t>(n)) {
      Update.printError(Serial0);
      Update.abort();
      file.close();
      installing = false;
      setStatus("Update write failed");
      return false;
    }
    written += n;
    const uint8_t percent = static_cast<uint8_t>((written * 100ULL) / size);
    if (percent >= lastPercent + 3) {
      lastPercent = percent;
      drawInstallProgress(path, percent);
      serviceNetwork();
    }
  }
  file.close();

  if (!Update.end(true)) {
    Update.printError(Serial0);
    installing = false;
    setStatus("Update end failed");
    return false;
  }
  drawInstallProgress(path, 100);
  setStatus("Booting selected app");
  delay(500);
  ESP.restart();
  return true;
}

void launchSelectedApp() {
  if (!appCount) {
    setStatus("No app selected");
    return;
  }
  const String path = String("/apps/") + apps[selectedApp].file;
  setStatus("Launching %s", apps[selectedApp].label.c_str());
  installAppFromSd(path);
}

bool inRect(uint16_t x, uint16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void handleTouch() {
  if (!touchReady || installing || millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) {
    return;
  }
  uint16_t x = 0;
  uint16_t y = 0;
  if (!touch.read(x, y)) {
    return;
  }
  lastTouchMs = millis();

  if (inRect(x, y, 8, 266, 52, 42)) {
    selectDelta(-1);
  } else if (inRect(x, y, 66, 266, 52, 42)) {
    selectDelta(1);
  } else if (inRect(x, y, 124, 266, 52, 42)) {
    launchSelectedApp();
  } else if (inRect(x, y, 182, 266, 50, 42)) {
    startAp();
    setStatus("AP %s", apActive ? "active" : "failed");
  } else if (y >= 88 && y < 88 + 4 * 38) {
    const uint8_t idx = topApp + ((y - 88) / 38);
    if (idx < appCount) {
      selectedApp = idx;
      uiDirty = true;
    }
  }
}

void processCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (!cmd.length()) {
    return;
  }
  if (cmd == "up") {
    selectDelta(-1);
  } else if (cmd == "down") {
    selectDelta(1);
  } else if (cmd == "enter" || cmd == "run") {
    launchSelectedApp();
  } else if (cmd == "ap") {
    startAp();
    setStatus("AP %s", apActive ? "active" : "failed");
  } else if (cmd == "reload") {
    initSd();
    loadApps();
  } else if (cmd == "list") {
    for (uint8_t i = 0; i < appCount; i++) {
      Serial0.printf("%u: %s -> /apps/%s\n", i, apps[i].label.c_str(), apps[i].file.c_str());
    }
  } else if (cmd.startsWith("run ")) {
    const int idx = cmd.substring(4).toInt();
    if (idx >= 0 && idx < appCount) {
      selectedApp = static_cast<uint8_t>(idx);
      launchSelectedApp();
    }
  } else {
    Serial0.println("Commands: up, down, enter/run, run N, ap, reload, list");
  }
}

void handleSerial() {
  static String line;
  while (Serial0.available()) {
    const char c = Serial0.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      processCommand(line);
      line = "";
    } else if (line.length() < 80) {
      line += c;
    }
  }
}

void appendPageHeader(String &html, const char *title) {
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>");
  html += title;
  html += F("</title><style>");
  html += F("body{font-family:system-ui,Segoe UI,sans-serif;margin:20px;background:#101418;color:#e8eef2}");
  html += F("main{max-width:880px;margin:auto}.card{border:1px solid #2b3a42;border-radius:10px;padding:14px;margin:12px 0;background:#172027}");
  html += F("input,button{box-sizing:border-box;width:100%;font:inherit;margin:6px 0;padding:10px;border-radius:7px;border:1px solid #53616b;background:#111a20;color:#fff}");
  html += F("button{background:#1b7f72;border:0;font-weight:700}.danger{background:#b23b3b}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}");
  html += F("a{color:#78dce8}.muted{color:#aab6bd;font-size:.92rem}table{width:100%;border-collapse:collapse}td,th{padding:8px;border-bottom:1px solid #2b3a42;text-align:left}");
  html += F("</style></head><body><main>");
  html += F("<h1>");
  html += title;
  html += F("</h1>");
}

void appendPageFooter(String &html) {
  html += F("</main></body></html>");
}

void appendFileRows(String &html, const String &path) {
  if (!sdReady) {
    html += F("<p class='muted'>SD not mounted.</p>");
    return;
  }
  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) {
    html += F("<p class='muted'>Cannot open directory.</p>");
    if (dir) {
      dir.close();
    }
    return;
  }

  html += F("<table><tr><th>Name</th><th>Size</th><th>Actions</th></tr>");
  if (path != "/") {
    html += F("<tr><td>..</td><td>dir</td><td><a href='/sd?path=");
    html += htmlEscape(parentPath(path));
    html += F("'>open</a></td></tr>");
  }

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    String name = entry.name();
    if (!name.startsWith("/")) {
      name = path == "/" ? "/" + name : path + "/" + name;
    }
    html += F("<tr><td>");
    html += htmlEscape(name.substring(name.lastIndexOf('/') + 1));
    html += F("</td><td>");
    html += entry.isDirectory() ? F("dir") : String(entry.size());
    html += F("</td><td>");
    if (entry.isDirectory()) {
      html += F("<a href='/sd?path=");
      html += htmlEscape(name);
      html += F("'>open</a>");
    } else {
      html += F("<a href='/download?path=");
      html += htmlEscape(name);
      html += F("'>download</a>");
    }
    html += F(" | <a href='/delete?path=");
    html += htmlEscape(name);
    html += F("' onclick='return confirm(\"Delete?\")'>delete</a></td></tr>");
    entry.close();
  }
  dir.close();
  html += F("</table>");
}

void handleRoot() {
  String html;
  html.reserve(5000);
  appendPageHeader(html, "ESP32 Bin Loader");
  html += F("<section class='card'><p class='muted'>Network: ");
  html += htmlEscape(networkStatus());
  html += F("<br>AP: ");
  html += htmlEscape(String(apName));
  html += F(" / pass ");
  html += AP_PASS;
  html += F("<br>SD: ");
  html += htmlEscape(sdStatus());
  html += F("</p><div class='row'><a href='/sd?path=/apps'><button>Open /apps</button></a><a href='/sd?path=/'><button>Open SD root</button></a></div></section>");
  html += F("<section class='card'><h2>Apps</h2><table><tr><th>Name</th><th>File</th><th>Action</th></tr>");
  if (!appCount) {
    html += F("<tr><td colspan='3' class='muted'>No apps found in /apps.</td></tr>");
  }
  for (uint8_t i = 0; i < appCount; i++) {
    html += F("<tr><td>");
    html += htmlEscape(apps[i].label);
    html += F("</td><td>");
    html += htmlEscape(apps[i].file);
    html += F("</td><td><form method='post' action='/launch' onsubmit='return confirm(\"Install and reboot into this app?\")'><input type='hidden' name='idx' value='");
    html += String(i);
    html += F("'><button>Launch</button></form></td></tr>");
  }
  html += F("</table></section>");
  html += F("<section class='card'><h2>WiFi</h2><form method='post' action='/wifi'><input name='ssid' placeholder='SSID' value='");
  html += htmlEscape(prefs.getString("ssid", ""));
  html += F("'><input name='pass' placeholder='Password' type='password' value='");
  html += htmlEscape(prefs.getString("pass", ""));
  html += F("'><button>Save and connect</button></form></section>");
  html += F("<section class='card'><div class='row'><form method='post' action='/ap'><button>Start AP</button></form><form method='post' action='/reboot'><button class='danger'>Reboot</button></form></div></section>");
  appendPageFooter(html);
  server.send(200, "text/html", html);
}

void handleWifiSave() {
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (!ssid.length()) {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  connectPending = true;
  server.send(200, "text/html", "<p>Saved. Connecting...</p><p><a href='/'>Back</a></p>");
}

void handleSd() {
  const String path = cleanPath(server.arg("path"));
  String html;
  html.reserve(10000);
  appendPageHeader(html, "SD Browser");
  html += F("<p class='muted'>Path: ");
  html += htmlEscape(path);
  html += F(" / <a href='/'>home</a></p>");
  html += F("<section class='card'><form method='post' action='/upload?dir=");
  html += htmlEscape(path);
  html += F("' enctype='multipart/form-data'><input type='file' name='file' required><button>Upload here</button></form>");
  html += F("<form method='post' action='/mkdir'><input type='hidden' name='path' value='");
  html += htmlEscape(path);
  html += F("'><input name='name' placeholder='New folder name'><button>Create folder</button></form></section>");
  html += F("<section class='card'>");
  appendFileRows(html, path);
  html += F("</section>");
  appendPageFooter(html);
  server.send(200, "text/html", html);
}

void handleDownload() {
  const String path = cleanPath(server.arg("path"));
  if (!sdReady || path == "/" || !SD_MMC.exists(path)) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "Not a file");
    if (file) {
      file.close();
    }
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + path.substring(path.lastIndexOf('/') + 1) + "\"");
  server.streamFile(file, contentTypeFor(path));
  file.close();
}

void handleDelete() {
  const String path = cleanPath(server.arg("path"));
  String back = parentPath(path);
  bool ok = false;
  if (sdReady && path != "/" && SD_MMC.exists(path)) {
    File entry = SD_MMC.open(path);
    if (entry) {
      ok = entry.isDirectory() ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
      entry.close();
    }
  }
  server.sendHeader("Location", "/sd?path=" + back, true);
  server.send(ok ? 302 : 500, "text/plain", ok ? "" : "Delete failed");
}

void handleMkdir() {
  const String base = cleanPath(server.arg("path"));
  String name = server.arg("name");
  name.trim();
  name.replace("\\", "");
  name.replace("/", "");
  name.replace("..", "");
  if (!sdReady || !name.length()) {
    server.send(400, "text/plain", "Bad folder name or SD missing");
    return;
  }
  const String path = base == "/" ? "/" + name : base + "/" + name;
  SD_MMC.mkdir(path);
  server.sendHeader("Location", "/sd?path=" + base, true);
  server.send(302, "text/plain", "");
}

void handleUploadDone() {
  const String dir = cleanPath(server.arg("dir"));
  server.sendHeader("Location", "/sd?path=" + dir, true);
  server.send(302, "text/plain", "");
}

void handleUpload() {
  HTTPUpload &upload = server.upload();
  const String dir = cleanPath(server.arg("dir"));
  if (upload.status == UPLOAD_FILE_START) {
    if (!sdReady) {
      return;
    }
    String filename = upload.filename;
    filename.replace("\\", "/");
    filename = filename.substring(filename.lastIndexOf('/') + 1);
    filename.replace("..", "");
    const String path = dir == "/" ? "/" + filename : dir + "/" + filename;
    uploadFile = SD_MMC.open(path, "w");
    logf("Upload start: %s", path.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    logf("Upload done: %u bytes", static_cast<unsigned>(upload.totalSize));
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
    }
    logf("Upload aborted");
  }
}

void handleLaunch() {
  if (!server.hasArg("idx")) {
    server.send(400, "text/plain", "Missing app index");
    return;
  }
  const int idx = server.arg("idx").toInt();
  if (idx < 0 || idx >= appCount) {
    server.send(404, "text/plain", "App index not found");
    return;
  }
  selectedApp = static_cast<uint8_t>(idx);
  server.send(200, "text/html", "<p>Installing selected app. Device will reboot if successful.</p>");
  delay(100);
  launchSelectedApp();
}

void handleApStart() {
  startAp();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleReboot() {
  server.send(200, "text/html", "<p>Rebooting...</p>");
  delay(200);
  ESP.restart();
}

void handleNotFound() {
  if (apActive) {
    server.sendHeader("Location", String("http://") + AP_IP.toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void setupRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/sd", HTTP_GET, handleSd);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/mkdir", HTTP_POST, handleMkdir);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.on("/launch", HTTP_POST, handleLaunch);
  server.on("/ap", HTTP_POST, handleApStart);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.onNotFound(handleNotFound);
}

void serviceNetwork() {
  server.handleClient();
  if (apActive) {
    dnsServer.processNextRequest();
  }
  if (connectPending) {
    connectPending = false;
    if (!connectWifi(true)) {
      startAp();
    }
  }
}

void setup() {
  initDebug();
  initDisplay();
  initApName();
  prefs.begin("factory-launcher", false);
  initSd();

  const bool wifiOk = connectWifi(true);
  if (!wifiOk) {
    startAp();
  }

  setupRoutes();
  server.begin();
  logf("Web UI ready: %s", wifiOk ? WiFi.localIP().toString().c_str() : AP_IP.toString().c_str());
  setStatus("Ready");
  drawUi();
}

void loop() {
  serviceNetwork();
  handleSerial();
  handleTouch();
  drawUi();
  delay(2);
}
