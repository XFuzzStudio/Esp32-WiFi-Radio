#include <Arduino.h>
#include <AnimatedGIF.h>
#include <DNSServer.h>
#include <JPEGDEC.h>
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
static constexpr char APP_VERSION[] = "1.1";
static constexpr char DATA_DIR[] = "/apps_data/ESP-GiF-Player";
static constexpr char GIF_DIR[] = "/apps_data/ESP-GiF-Player/gifs";
static constexpr char PHOTO_DIR[] = "/apps_data/ESP-GiF-Player/photos";
static constexpr char UPLOAD_DIR[] = "/apps_data/ESP-GiF-Player/uploads";
static constexpr uint16_t W = 320;
static constexpr uint16_t H = 240;
static constexpr uint8_t LV_ROWS = 24;
static constexpr uint8_t MAX_MEDIA = 60;
static constexpr uint32_t PHOTO_MS = 7000;

enum class MediaType : uint8_t {
  Gif,
  Jpeg,
  Bmp,
};

struct MediaItem {
  String name;
  MediaType type;
};

LcdWikiEs3c28pDisplay display;
LcdWikiFt6336Touch touch;
AnimatedGIF gif;
JPEGDEC jpeg;
File gifFile;
File jpegFile;
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
MediaItem mediaItems[MAX_MEDIA];
uint8_t mediaCount = 0;
int selected = 0;
int gifOffsetX = 0;
int gifOffsetY = 0;
uint32_t photoStartedMs = 0;
bool sdReady = false;
bool dataReady = false;
bool apActive = false;
bool playing = false;
bool showingPhoto = false;
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

String mediaLabel(const MediaItem &item) {
  switch (item.type) {
    case MediaType::Gif: return String("[GIF] ") + item.name;
    case MediaType::Jpeg: return String("[JPG] ") + item.name;
    case MediaType::Bmp: return String("[BMP] ") + item.name;
  }
  return item.name;
}

bool isGifName(String n) {
  n.toLowerCase();
  return n.endsWith(".gif");
}

bool isJpegName(String n) {
  n.toLowerCase();
  return n.endsWith(".jpg") || n.endsWith(".jpeg");
}

bool isBmpName(String n) {
  n.toLowerCase();
  return n.endsWith(".bmp");
}

String mediaPath(int idx) {
  if (idx < 0 || idx >= mediaCount) return "";
  const MediaItem &item = mediaItems[idx];
  if (item.type == MediaType::Gif) return String(GIF_DIR) + "/" + item.name;
  return String(PHOTO_DIR) + "/" + item.name;
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
    if (!SD_MMC.exists(PHOTO_DIR)) SD_MMC.mkdir(PHOTO_DIR);
    if (!SD_MMC.exists(UPLOAD_DIR)) SD_MMC.mkdir(UPLOAD_DIR);
    dataReady = SD_MMC.exists(GIF_DIR) && SD_MMC.exists(PHOTO_DIR) && SD_MMC.exists(UPLOAD_DIR);
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
    d->point.x = y;
    d->point.y = LCDWIKI_ES3C28P_SCREEN_WIDTH - 1 - x;
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
  uint16_t line[W];
  int width = pDraw->iWidth;
  const int drawX = gifOffsetX + pDraw->iX;
  const int y = gifOffsetY + pDraw->iY + pDraw->y;
  if (drawX + width > W) width = W - drawX;
  if (y < 0 || y >= H || drawX >= W || width <= 0) return;
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
      if (runEnd > runStart) display.gfx()->draw16bitRGBBitmap(drawX + runStart, y, line, runEnd - runStart, 1);
      runStart = runEnd;
    }
  } else {
    for (int x = 0; x < width; x++) line[x] = pal[src[x]];
    display.gfx()->draw16bitRGBBitmap(drawX, y, line, width, 1);
  }
}

void *jpegOpen(const char *name, int32_t *size) {
  jpegFile = SD_MMC.open(name, FILE_READ);
  if (!jpegFile) return nullptr;
  *size = jpegFile.size();
  return static_cast<void *>(&jpegFile);
}

void jpegClose(void *handle) {
  File *f = static_cast<File *>(handle);
  if (f) f->close();
}

int32_t jpegRead(JPEGFILE *pFile, uint8_t *buf, int32_t len) {
  File *f = static_cast<File *>(pFile->fHandle);
  return f->read(buf, len);
}

int32_t jpegSeek(JPEGFILE *pFile, int32_t pos) {
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(pos);
  return pos;
}

int jpegDraw(JPEGDRAW *pDraw) {
  if (pDraw->x >= W || pDraw->y >= H) return 1;
  int w = pDraw->iWidth;
  int h = pDraw->iHeight;
  if (pDraw->x + w > W) w = W - pDraw->x;
  if (pDraw->y + h > H) h = H - pDraw->y;
  if (w > 0 && h > 0) display.gfx()->draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, w, h);
  return 1;
}

uint16_t read16(File &f) {
  uint16_t v = f.read();
  v |= static_cast<uint16_t>(f.read()) << 8;
  return v;
}

uint32_t read32(File &f) {
  uint32_t v = read16(f);
  v |= static_cast<uint32_t>(read16(f)) << 16;
  return v;
}

bool drawBmp(const String &path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return false;
  if (read16(f) != 0x4D42) {
    f.close();
    return false;
  }
  (void)read32(f);
  (void)read32(f);
  const uint32_t dataOffset = read32(f);
  const uint32_t headerSize = read32(f);
  const int32_t bmpW = static_cast<int32_t>(read32(f));
  int32_t bmpH = static_cast<int32_t>(read32(f));
  if (headerSize < 40 || bmpW <= 0 || bmpH == 0) {
    f.close();
    return false;
  }
  const bool topDown = bmpH < 0;
  if (topDown) bmpH = -bmpH;
  const uint16_t planes = read16(f);
  const uint16_t depth = read16(f);
  const uint32_t compression = read32(f);
  if (planes != 1 || depth != 24 || compression != 0) {
    f.close();
    return false;
  }
  const uint32_t rowSize = (bmpW * 3 + 3) & ~3;
  const int x0 = max(0, (static_cast<int>(W) - static_cast<int>(bmpW)) / 2);
  const int y0 = max(0, (static_cast<int>(H) - static_cast<int>(bmpH)) / 2);
  const int drawW = min(static_cast<int32_t>(W), bmpW);
  const int drawH = min(static_cast<int32_t>(H), bmpH);
  uint16_t line[W];
  for (int y = 0; y < drawH; y++) {
    const int srcY = topDown ? y : (bmpH - 1 - y);
    f.seek(dataOffset + srcY * rowSize);
    for (int x = 0; x < drawW; x++) {
      const uint8_t b = f.read();
      const uint8_t g = f.read();
      const uint8_t r = f.read();
      line[x] = display.gfx()->color565(r, g, b);
    }
    display.gfx()->draw16bitRGBBitmap(x0, y0 + y, line, drawW, 1);
    yield();
  }
  f.close();
  return true;
}

void stopPlayback() {
  if (playing && !showingPhoto) gif.close();
  playing = false;
  showingPhoto = false;
}

void nextMedia();

bool showPhoto(int idx) {
  String path = mediaPath(idx);
  display.gfx()->fillScreen(0x0000);
  bool ok = false;
  if (mediaItems[idx].type == MediaType::Jpeg) {
    if (jpeg.open(path.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDraw)) {
      int scale = 0;
      while ((jpeg.getWidth() >> scale) > W || (jpeg.getHeight() >> scale) > H) {
        if (scale >= 3) break;
        scale++;
      }
      const int opts[] = {0, JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH};
      const int outW = jpeg.getWidth() >> scale;
      const int outH = jpeg.getHeight() >> scale;
      jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
      jpeg.decode(max(0, (W - outW) / 2), max(0, (H - outH) / 2), opts[scale]);
      jpeg.close();
      ok = true;
    }
  } else if (mediaItems[idx].type == MediaType::Bmp) {
    ok = drawBmp(path);
  }
  if (ok) {
    showingPhoto = true;
    playing = true;
    photoStartedMs = millis();
    setStatus(String("Showing ") + mediaItems[idx].name);
  }
  return ok;
}

void startMedia(int idx) {
  if (!dataReady || idx < 0 || idx >= mediaCount) {
    setStatus("No media selected");
    return;
  }
  stopPlayback();
  selected = idx;
  if (mediaItems[selected].type == MediaType::Gif) {
    display.gfx()->fillScreen(0x0000);
    String path = mediaPath(selected);
    if (gif.open(path.c_str(), gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
      gifOffsetX = max(0, (static_cast<int>(W) - gif.getCanvasWidth()) / 2);
      gifOffsetY = max(0, (static_cast<int>(H) - gif.getCanvasHeight()) / 2);
      playing = true;
      showingPhoto = false;
      setStatus(String("Playing ") + mediaItems[selected].name);
    } else {
      setStatus("GIF open failed");
    }
  } else if (!showPhoto(selected)) {
    setStatus("Photo open failed");
  }
}

void addMedia(const String &name, MediaType type) {
  if (mediaCount >= MAX_MEDIA) return;
  mediaItems[mediaCount++] = {name, type};
}

void scanDir(const char *dirName, bool photos) {
  File dir = SD_MMC.open(dirName);
  if (!dir || !dir.isDirectory()) return;
  while (mediaCount < MAX_MEDIA) {
    File e = dir.openNextFile();
    if (!e) break;
    if (!e.isDirectory()) {
      String n = e.name();
      n = n.substring(n.lastIndexOf('/') + 1);
      if (!photos && isGifName(n)) addMedia(n, MediaType::Gif);
      if (photos && isJpegName(n)) addMedia(n, MediaType::Jpeg);
      if (photos && isBmpName(n)) addMedia(n, MediaType::Bmp);
    }
    e.close();
  }
  dir.close();
}

void scanMedia() {
  mediaCount = 0;
  if (!dataReady) return;
  scanDir(GIF_DIR, false);
  scanDir(PHOTO_DIR, true);
  if (selected >= mediaCount) selected = 0;
}

void rebuildList();

void listEvent(lv_event_t *e) {
  int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  startMedia(idx);
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
  for (int i = 0; i < mediaCount; i++) {
    const String label = mediaLabel(mediaItems[i]);
    lv_obj_t *b = lv_list_add_button(listObj, nullptr, label.c_str());
    lv_obj_add_event_cb(b, listEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
}

void buildUi() {
  root = lv_screen_active();
  lv_obj_clean(root);
  titleLbl = lv_label_create(root);
  lv_label_set_text(titleLbl, "ESP Media Frame 1.1");
  lv_obj_set_pos(titleLbl, 8, 6);
  statusLbl = lv_label_create(root);
  lv_obj_set_pos(statusLbl, 150, 8);
  lv_obj_set_width(statusLbl, 162);
  lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_DOT);
  button("Prev", 8, 34, 62, 30, [](lv_event_t *){ if (mediaCount) startMedia((selected + mediaCount - 1) % mediaCount); });
  button(playing ? "Pause" : "Play", 76, 34, 62, 30, [](lv_event_t *){ playing ? stopPlayback() : startMedia(selected); setStatus(playing ? "Playing" : "Paused"); });
  button("Next", 144, 34, 62, 30, [](lv_event_t *){ if (mediaCount) nextMedia(); });
  button("Refresh", 212, 34, 96, 30, [](lv_event_t *){ scanMedia(); rebuildList(); setStatus("List refreshed"); });
  listObj = lv_list_create(root);
  lv_obj_set_pos(listObj, 8, 72);
  lv_obj_set_size(listObj, 304, 160);
  rebuildList();
  setStatus(dataReady ? "Ready" : "SD data missing");
}

void nextMedia() {
  if (!mediaCount) return;
  startMedia((selected + 1) % mediaCount);
}

void startAp() {
  WiFi.mode(WIFI_AP);
  String name = String(APP_NAME) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX).substring(4);
  apActive = WiFi.softAP(name.c_str(), "gifplayer123");
  if (apActive) dns.start(53, "*", WiFi.softAPIP());
}

void rootPage() {
  String h = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><style>body{font-family:system-ui;background:#101820;color:#edf;margin:20px}button,input{width:100%;padding:10px;margin:5px 0}li{margin:8px 0}</style>";
  h += "<h1>ESP Media Frame 1.1</h1><p>" + esc(statusText) + "</p>";
  h += "<form method=post action=/upload enctype=multipart/form-data><input type=file name=file accept='.gif,.jpg,.jpeg,.bmp,image/gif,image/jpeg,image/bmp'><button>Upload media</button></form>";
  h += "<form method=post action=/refresh><button>Refresh list</button></form><ol>";
  for (int i = 0; i < mediaCount; i++) {
    h += "<li>" + esc(mediaLabel(mediaItems[i])) + "<form method=post action=/play><input type=hidden name=i value='" + String(i) + "'><button>Play</button></form><form method=post action=/delete><input type=hidden name=i value='" + String(i) + "'><button>Delete</button></form></li>";
  }
  h += "</ol><form method=post action=/pause><button>Play/Pause</button></form>";
  server.send(200, "text/html", h);
}

void uploadDone() {
  scanMedia();
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
    String lower = name;
    lower.toLowerCase();
    if (!isGifName(lower) && !isJpegName(lower) && !isBmpName(lower)) name += ".jpg";
    const char *dir = isGifName(name) ? GIF_DIR : PHOTO_DIR;
    uploadFile = SD_MMC.open(String(dir) + "/" + name, "w");
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END || u.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
  }
}

void setupRoutes() {
  server.on("/", rootPage);
  server.on("/upload", HTTP_POST, uploadDone, handleUpload);
  server.on("/refresh", HTTP_POST, [](){ scanMedia(); rebuildList(); server.sendHeader("Location","/"); server.send(302); });
  server.on("/play", HTTP_POST, [](){ startMedia(server.arg("i").toInt()); server.sendHeader("Location","/"); server.send(302); });
  server.on("/pause", HTTP_POST, [](){ playing ? stopPlayback() : startMedia(selected); server.sendHeader("Location","/"); server.send(302); });
  server.on("/delete", HTTP_POST, [](){ int i=server.arg("i").toInt(); if (i>=0 && i<mediaCount) SD_MMC.remove(mediaPath(i)); scanMedia(); rebuildList(); server.sendHeader("Location","/"); server.send(302); });
}

void setup() {
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  esp32BinLoaderReturnToFactoryOnNextBoot();
  display.begin();
  display.gfx()->setRotation(1);
  touch.begin();
  display.gfx()->fillScreen(0x0000);
  initSd();
  scanMedia();
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
  if (playing && !showingPhoto) {
    int delayMs = 0;
    if (!gif.playFrame(true, &delayMs, nullptr)) {
      gif.close();
      nextMedia();
    }
  } else if (playing && showingPhoto) {
    if (millis() - photoStartedMs >= PHOTO_MS) nextMedia();
  } else {
    lv_tick_inc(5);
    lv_timer_handler();
  }
  server.handleClient();
  if (apActive) dns.processNextRequest();
  delay(2);
}
