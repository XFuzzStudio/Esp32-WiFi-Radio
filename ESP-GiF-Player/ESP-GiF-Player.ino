#include <Arduino.h>
#include <AnimatedGIF.h>
#include <DNSServer.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <lvgl.h>

#include "../shared/esp32_bin_loader_return.h"
#include "../shared/lcdwiki_es3c28p/arduino_gfx_display.h"
#include "../shared/lcdwiki_es3c28p/lcdwiki_es3c28p_config.h"
#include "../shared/lcdwiki_es3c28p/touch_ft6336.h"

static constexpr char APP_NAME[] = "ESP-GiF-Player";
static constexpr char APP_VERSION[] = "1.0";
static constexpr char DATA_DIR[] = "/apps_data/ESP-GiF-Player";
static constexpr char GIF_DIR[] = "/apps_data/ESP-GiF-Player/gifs";
static constexpr char UPLOAD_DIR[] = "/apps_data/ESP-GiF-Player/uploads";
static constexpr uint16_t W = LCDWIKI_ES3C28P_SCREEN_WIDTH;
static constexpr uint16_t H = LCDWIKI_ES3C28P_SCREEN_HEIGHT;
static constexpr uint8_t LV_ROWS = 24;
static constexpr uint8_t MAX_GIFS = 40;

LcdWikiEs3c28pDisplay display;
LcdWikiFt6336Touch touch;
AnimatedGIF gif;
File gifFile;
File uploadFile;
WebServer server(80);
DNSServer dns;
lv_display_t *lvDisp = nullptr;
lv_indev_t *lvInput = nullptr;
lv_obj_t *root = nullptr;
lv_obj_t *titleLbl = nullptr;
lv_obj_t *statusLbl = nullptr;
lv_obj_t *listObj = nullptr;
uint16_t drawBuf[W * LV_ROWS];
String gifNames[MAX_GIFS];
uint8_t gifCount = 0;
int selected = 0;
bool sdReady = false;
bool dataReady = false;
bool apActive = false;
bool playing = false;
String statusText = "Starting";

void setStatus(const String &s) {
  statusText = s;
  Serial0.println(s);
  if (statusLbl) lv_label_set_text(statusLbl, statusText.c_str());
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
    if (!SD_MMC.exists(GIF_DIR)) SD_MMC.mkdir(GIF_DIR);
    if (!SD_MMC.exists(UPLOAD_DIR)) SD_MMC.mkdir(UPLOAD_DIR);
    dataReady = SD_MMC.exists(GIF_DIR) && SD_MMC.exists(UPLOAD_DIR);
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

void *gifOpen(const char *name, int32_t *size) {
  gifFile = SD_MMC.open(name, FILE_READ);
  if (!gifFile) return nullptr;
  *size = gifFile.size();
  return static_cast<void *>(&gifFile);
}

void gifClose(void *handle) {
  File *f = static_cast<File *>(handle);
  if (f) f->close();
}

int32_t gifRead(GIFFILE *pFile, uint8_t *buf, int32_t len) {
  File *f = static_cast<File *>(pFile->fHandle);
  int32_t remaining = pFile->iSize - pFile->iPos;
  if (remaining <= 0) return 0;
  if (len > remaining) len = remaining;
  int32_t n = f->read(buf, len);
  pFile->iPos = f->position();
  return n;
}

int32_t gifSeek(GIFFILE *pFile, int32_t pos) {
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(pos);
  pFile->iPos = f->position();
  return pFile->iPos;
}

void gifDraw(GIFDRAW *pDraw) {
  uint16_t line[320];
  int width = pDraw->iWidth;
  if (pDraw->iX + width > W) width = W - pDraw->iX;
  int y = pDraw->iY + pDraw->y;
  if (y < 0 || y >= H || pDraw->iX >= W || width <= 0) return;
  uint8_t *src = pDraw->pPixels;
  uint16_t *pal = pDraw->pPalette;
  if (pDraw->ucHasTransparency) {
    int runStart = 0;
    while (runStart < width) {
      while (runStart < width && src[runStart] == pDraw->ucTransparent) runStart++;
      int runEnd = runStart;
      while (runEnd < width && src[runEnd] != pDraw->ucTransparent) {
        line[runEnd - runStart] = pal[src[runEnd]];
        runEnd++;
      }
      if (runEnd > runStart) display.gfx()->draw16bitRGBBitmap(pDraw->iX + runStart, y, line, runEnd - runStart, 1);
      runStart = runEnd;
    }
  } else {
    for (int x = 0; x < width; x++) line[x] = pal[src[x]];
    display.gfx()->draw16bitRGBBitmap(pDraw->iX, y, line, width, 1);
  }
}

String gifPath(int idx) {
  if (idx < 0 || idx >= gifCount) return "";
  return String(GIF_DIR) + "/" + gifNames[idx];
}

void stopGif() {
  if (playing) gif.close();
  playing = false;
}

void startGif(int idx) {
  if (!dataReady || idx < 0 || idx >= gifCount) {
    setStatus("No GIF selected");
    return;
  }
  stopGif();
  selected = idx;
  display.gfx()->fillScreen(0x0000);
  String path = gifPath(selected);
  if (gif.open(path.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
    playing = true;
    setStatus(String("Playing ") + gifNames[selected]);
  } else {
    setStatus("GIF open failed");
  }
}

void scanGifs() {
  gifCount = 0;
  if (!dataReady) return;
  File dir = SD_MMC.open(GIF_DIR);
  if (!dir || !dir.isDirectory()) return;
  while (gifCount < MAX_GIFS) {
    File e = dir.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      String n = e.name();
      n = n.substring(n.lastIndexOf('/') + 1);
      String l = n;
      l.toLowerCase();
      if (l.endsWith(".gif")) gifNames[gifCount++] = n;
    }
    e.close();
  }
  dir.close();
  if (selected >= gifCount) selected = 0;
}

void rebuildList();

void listEvent(lv_event_t *e) {
  int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  startGif(idx);
}

lv_obj_t *button(const char *text, int x, int y, int w, int h, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(root);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

void rebuildList() {
  if (!listObj) return;
  lv_obj_clean(listObj);
  if (!dataReady) {
    lv_obj_t *l = lv_label_create(listObj);
    lv_label_set_text(l, "ERROR: SD data folder missing");
    return;
  }
  for (int i = 0; i < gifCount; i++) {
    lv_obj_t *b = lv_list_add_button(listObj, nullptr, gifNames[i].c_str());
    lv_obj_add_event_cb(b, listEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
}

void buildUi() {
  root = lv_screen_active();
  lv_obj_clean(root);
  titleLbl = lv_label_create(root);
  lv_label_set_text(titleLbl, "ESP-GiF-Player 1.0");
  lv_obj_set_pos(titleLbl, 8, 6);
  statusLbl = lv_label_create(root);
  lv_obj_set_pos(statusLbl, 8, 28);
  lv_obj_set_width(statusLbl, 224);
  button("Prev", 8, 52, 52, 30, [](lv_event_t *){ if (gifCount) startGif((selected + gifCount - 1) % gifCount); });
  button(playing ? "Pause" : "Play", 64, 52, 52, 30, [](lv_event_t *){ playing ? stopGif() : startGif(selected); setStatus(playing ? "Playing" : "Paused"); });
  button("Next", 120, 52, 52, 30, [](lv_event_t *){ if (gifCount) startGif((selected + 1) % gifCount); });
  button("Refresh", 176, 52, 56, 30, [](lv_event_t *){ scanGifs(); rebuildList(); setStatus("List refreshed"); });
  listObj = lv_list_create(root);
  lv_obj_set_pos(listObj, 8, 90);
  lv_obj_set_size(listObj, 224, 220);
  rebuildList();
  setStatus(dataReady ? "Ready" : "SD data missing");
}

void startAp() {
  WiFi.mode(WIFI_AP);
  String name = String(APP_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);
  apActive = WiFi.softAP(name.c_str(), "gifplayer123");
  if (apActive) dns.start(53, "*", WiFi.softAPIP());
}

void rootPage() {
  String h = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><style>body{font-family:system-ui;background:#101820;color:#edf;margin:20px}button,input{width:100%;padding:10px;margin:5px 0}li{margin:8px 0}</style>";
  h += "<h1>ESP-GiF-Player 1.0</h1><p>" + esc(statusText) + "</p>";
  h += "<form method=post action=/upload enctype=multipart/form-data><input type=file name=file accept='.gif,image/gif'><button>Upload GIF</button></form>";
  h += "<form method=post action=/refresh><button>Refresh list</button></form><ol>";
  for (int i = 0; i < gifCount; i++) {
    h += "<li>" + esc(gifNames[i]) + "<form method=post action=/play><input type=hidden name=i value='" + String(i) + "'><button>Play</button></form><form method=post action=/delete><input type=hidden name=i value='" + String(i) + "'><button>Delete</button></form></li>";
  }
  h += "</ol><form method=post action=/pause><button>Play/Pause</button></form>";
  server.send(200, "text/html", h);
}

void uploadDone() {
  scanGifs();
  rebuildList();
  server.sendHeader("Location", "/", true);
  server.send(302);
}

void handleUpload() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    String name = u.filename;
    name = name.substring(name.lastIndexOf('/') + 1);
    name.replace("\\", "");
    name.replace("/", "");
    if (!name.endsWith(".gif") && !name.endsWith(".GIF")) name += ".gif";
    uploadFile = SD_MMC.open(String(GIF_DIR) + "/" + name, "w");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
  }
}

void setupRoutes() {
  server.on("/", rootPage);
  server.on("/upload", HTTP_POST, uploadDone, handleUpload);
  server.on("/refresh", HTTP_POST, [](){ scanGifs(); rebuildList(); server.sendHeader("Location","/"); server.send(302); });
  server.on("/play", HTTP_POST, [](){ startGif(server.arg("i").toInt()); server.sendHeader("Location","/"); server.send(302); });
  server.on("/pause", HTTP_POST, [](){ playing ? stopGif() : startGif(selected); server.sendHeader("Location","/"); server.send(302); });
  server.on("/delete", HTTP_POST, [](){ int i=server.arg("i").toInt(); if (i>=0 && i<gifCount) SD_MMC.remove(gifPath(i)); scanGifs(); rebuildList(); server.sendHeader("Location","/"); server.send(302); });
}

void setup() {
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  esp32BinLoaderReturnToFactoryOnNextBoot();
  display.begin();
  touch.begin();
  display.gfx()->fillScreen(0x0000);
  initSd();
  scanGifs();
  gif.begin(LITTLE_ENDIAN_PIXELS);
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
}

void loop() {
  if (playing) {
    int delayMs = 0;
    if (!gif.playFrame(true, &delayMs, nullptr)) {
      gif.reset();
    }
  } else {
    lv_tick_inc(5);
    lv_timer_handler();
  }
  server.handleClient();
  if (apActive) dns.processNextRequest();
  delay(2);
}
