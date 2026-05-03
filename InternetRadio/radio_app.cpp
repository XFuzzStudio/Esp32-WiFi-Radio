#include "radio_app.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Audio.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <stdarg.h>
#include <time.h>

#include "../hardware_specs/lcdwiki-esp32-s3-es3c28p/arduino_gfx_display.h"
#include "../hardware_specs/lcdwiki-esp32-s3-es3c28p/lcdwiki_es3c28p_config.h"
#include "../hardware_specs/lcdwiki-esp32-s3-es3c28p/touch_ft6336.h"

#define DEBUG_PORT Serial0

volatile uint32_t g_i2sFrameCount = 0;
volatile uint32_t g_i2sCallbackCount = 0;
volatile uint32_t g_i2sLastMs = 0;
volatile uint32_t g_i2sPeak = 0;
volatile bool g_i2sForceTone = false;
volatile uint16_t g_i2sTonePhase = 0;

void audio_process_raw_samples(int32_t *outBuff, int16_t validSamples) {
  (void)outBuff;
  (void)validSamples;
}

void audio_process_i2s(int32_t *outBuff, int16_t validSamples, bool *continueI2S) {
  if (continueI2S) {
    *continueI2S = true;
  }

  if (g_i2sForceTone) {
    const int32_t amplitude = 0x08000000;
    for (int16_t i = 0; i < validSamples; i++) {
      const int32_t sample = g_i2sTonePhase < 24 ? amplitude : -amplitude;
      outBuff[i * 2] = sample;
      outBuff[i * 2 + 1] = sample;
      g_i2sTonePhase++;
      if (g_i2sTonePhase >= 48) {
        g_i2sTonePhase = 0;
      }
    }
  }

  uint32_t peak = 0;
  const int32_t total = static_cast<int32_t>(validSamples) * 2;
  for (int32_t i = 0; i < total; i++) {
    const int64_t sample = outBuff[i];
    const uint32_t magnitude = sample < 0 ? static_cast<uint32_t>(-sample) : static_cast<uint32_t>(sample);
    if (magnitude > peak) {
      peak = magnitude;
    }
  }
  g_i2sPeak = peak;
  g_i2sFrameCount += validSamples;
  g_i2sCallbackCount++;
  g_i2sLastMs = millis();
}

namespace {

static constexpr uint8_t MAX_STATIONS = 24;
static constexpr uint8_t DEFAULT_VOLUME = 13;
static constexpr uint8_t MAX_VOLUME = 21;
static constexpr uint16_t DEFAULT_TOUCH_DEBOUNCE_MS = 170;
static constexpr uint16_t DEFAULT_WIFI_CONNECT_TIMEOUT_MS = 18000;
static constexpr uint16_t DEFAULT_WIFI_RETRY_MS = 15000;
static constexpr uint16_t CONFIG_SAVE_DELAY_MS = 3000;
static constexpr uint8_t DEFAULT_LED_BRIGHTNESS = 18;
static constexpr uint16_t DEFAULT_LED_PULSE_MS = 2400;
static constexpr uint16_t DEFAULT_LED_BLINK_MS = 260;
static constexpr uint16_t DEFAULT_STATUS_REFRESH_MS = 5000;
static constexpr uint16_t DEFAULT_BATTERY_MIN_MV = 3300;
static constexpr uint16_t DEFAULT_BATTERY_MAX_MV = 4200;
static constexpr uint16_t DEFAULT_BATTERY_SCALE_PERMILLE = 2000;
static constexpr uint16_t DEFAULT_CLOCK_REFRESH_MS = 1000;
static constexpr uint32_t NTP_RECONFIGURE_MS = 3600000UL;
static constexpr char DEFAULT_NTP_SERVER[] = "pool.ntp.org";
static constexpr char DEFAULT_TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
static constexpr byte DNS_PORT = 53;
static constexpr int8_t DEBUG_UART_RX = 44;
static constexpr int8_t DEBUG_UART_TX = 43;
static constexpr char STATIONS_FILE[] = "/stations.csv";
static constexpr char CONFIG_FILE[] = "/radio.cfg";
static constexpr char COVERS_DIR[] = "/covers";
static constexpr char BOOT_LOGO_FILE[] = "/logo.bmp";

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_NETMASK(255, 255, 255, 0);

static constexpr uint16_t C_BLACK = 0x0000;
static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr uint16_t C_DIM = 0x7BEF;
static constexpr uint16_t C_DARK = 0x18E3;
static constexpr uint16_t C_PANEL = 0x2945;
static constexpr uint16_t C_BLUE = 0x001F;
static constexpr uint16_t C_CYAN = 0x07FF;
static constexpr uint16_t C_GREEN = 0x07E0;
static constexpr uint16_t C_YELLOW = 0xFFE0;
static constexpr uint16_t C_ORANGE = 0xFD20;
static constexpr uint16_t C_RED = 0xF800;

struct Station {
  String name;
  String url;
  String coverUrl;
  String coverPath;
};

struct ThemeDef {
  const char *id;
  const char *label;
  uint16_t header;
  uint16_t status;
  uint16_t panel;
  uint16_t button;
  uint16_t accent;
  uint16_t highlight;
  uint8_t ledR;
  uint8_t ledG;
  uint8_t ledB;
};

enum class UiScreen : uint8_t {
  Main,
  Menu,
};

enum class LedMode : uint8_t {
  Boot,
  Wifi,
  Ap,
  Streaming,
  Idle,
  Error,
};

enum class LedEffect : uint8_t {
  Off,
  Solid,
  Breathe,
  Blink,
  Vu,
};

LcdWikiEs3c28pDisplay display;
LcdWikiFt6336Touch touch;
Adafruit_NeoPixel pixel(
  LCDWIKI_ES3C28P_RGB_COUNT,
  LCDWIKI_ES3C28P_RGB_PIN,
  NEO_GRB + NEO_KHZ800
);
Audio audio;
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

Station stations[MAX_STATIONS];
uint8_t stationCount = 0;
int currentStation = 0;
int scrollStation = 0;
uint8_t volumeLevel = DEFAULT_VOLUME;
uint8_t themeIndex = 0;

UiScreen uiScreen = UiScreen::Main;
LedMode ledMode = LedMode::Boot;

bool displayReady = false;
bool touchReady = false;
bool sdReady = false;
bool apActive = false;
bool portalStarted = false;
bool playing = false;
bool connectPending = false;
bool uiDirty = true;
bool uiFullRedraw = true;
bool metadataDirty = false;
bool audioEnded = false;
bool audioPinoutReady = false;
bool codecTrace = false;
bool i2sTrace = false;
bool autoPlay = true;
bool coverDownload = true;
bool configDirty = false;
bool ledEnabled = true;
bool startApOnBoot = false;
bool batteryEnabled = true;
bool batteryValid = false;
bool bootSdMissing = false;
bool bootConfigMissing = false;
bool bootStationsMissing = false;
bool bootStationsInvalid = false;
bool configLoadedFromSd = false;
bool stationsLoadedFromSd = false;
bool ntpEnabled = true;
bool ntpConfigured = false;
bool clockValid = false;
bool clock24h = true;

uint32_t lastTouchMs = 0;
uint32_t lastWifiRetryMs = 0;
uint32_t lastLedMs = 0;
uint32_t lastI2sTraceMs = 0;
uint32_t lastI2sTraceFrames = 0;
uint32_t errorUntilMs = 0;
uint32_t configDirtyMs = 0;
uint32_t lastSystemStatusMs = 0;
uint32_t lastClockMs = 0;
uint32_t lastNtpConfigMs = 0;
uint32_t wifiRetryMs = DEFAULT_WIFI_RETRY_MS;
uint32_t wifiConnectTimeoutMs = DEFAULT_WIFI_CONNECT_TIMEOUT_MS;
uint32_t statusRefreshMs = DEFAULT_STATUS_REFRESH_MS;
uint16_t touchDebounceMs = DEFAULT_TOUCH_DEBOUNCE_MS;
uint16_t ledPulseMs = DEFAULT_LED_PULSE_MS;
uint16_t ledBlinkMs = DEFAULT_LED_BLINK_MS;
uint16_t batteryMinMv = DEFAULT_BATTERY_MIN_MV;
uint16_t batteryMaxMv = DEFAULT_BATTERY_MAX_MV;
uint16_t batteryScalePermille = DEFAULT_BATTERY_SCALE_PERMILLE;
uint16_t batteryAdcMv = 0;
uint16_t batteryMv = 0;
uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;
uint8_t batteryPercent = 0;
int8_t wifiSignalBars = 0;
int8_t wifiRssiDbm = 0;
int startupStation = -1;

LedEffect ledBootEffect = LedEffect::Breathe;
LedEffect ledWifiEffect = LedEffect::Blink;
LedEffect ledApEffect = LedEffect::Breathe;
LedEffect ledStreamingEffect = LedEffect::Vu;
LedEffect ledIdleEffect = LedEffect::Breathe;
LedEffect ledErrorEffect = LedEffect::Blink;

char apName[32] = {0};
char streamTitle[128] = {0};
char audioStatus[96] = "Ready";
char clockText[16] = "--:--";
String serialLine;
String ntpServer = DEFAULT_NTP_SERVER;
String timezoneSpec = DEFAULT_TIMEZONE;
String wifiSsid;
String wifiPassword;

const char DEFAULT_STATIONS[] =
  "# name|stream_url|optional_cover_bmp_url\n"
  "Groove Salad|http://ice5.somafm.com/groovesalad-128-mp3\n"
  "Drone Zone|http://ice5.somafm.com/dronezone-128-mp3\n"
  "Deep Space One|http://ice5.somafm.com/deepspaceone-128-mp3\n";

const char DEFAULT_CONFIG[] =
  "# LCDWiki Internet Radio config\n"
  "# WiFi values are plain text on SD. Leave wifi_ssid empty to start setup AP.\n"
  "# theme: ocean, forest, sunset, mono, aurora, ember, berry, ice, mint, plum, steel, amber, neon, wine\n"
  "theme=ocean\n"
  "volume=13\n"
  "autoplay=1\n"
  "cover_download=1\n"
  "startup_station=last\n"
  "wifi_ssid=\n"
  "wifi_password=\n"
  "start_ap_on_boot=0\n"
  "wifi_retry_seconds=15\n"
  "wifi_connect_timeout_seconds=18\n"
  "status_refresh_ms=5000\n"
  "ntp_enabled=1\n"
  "ntp_server=pool.ntp.org\n"
  "timezone=CET-1CEST,M3.5.0,M10.5.0/3\n"
  "clock_24h=1\n"
  "touch_debounce_ms=170\n"
  "battery_enabled=1\n"
  "battery_min_mv=3300\n"
  "battery_max_mv=4200\n"
  "battery_scale_permille=2000\n"
  "led_enabled=1\n"
  "led_brightness=18\n"
  "led_boot_effect=breathe\n"
  "led_wifi_effect=blink\n"
  "led_ap_effect=breathe\n"
  "led_streaming_effect=vu\n"
  "led_idle_effect=breathe\n"
  "led_error_effect=blink\n"
  "led_pulse_ms=2400\n"
  "led_blink_ms=260\n"
  "# led effects: off, solid, breathe, blink, vu\n"
  "# startup_station: last or station index\n"
  "# safe ranges: volume 0..21, led_brightness 0..100, retry 5..120 s, timeout 5..60 s\n"
  "# battery_scale_permille default 2000 means ADC voltage times 2.000\n";

const ThemeDef THEMES[] = {
  {"ocean", "Ocean", C_BLUE, C_DARK, C_PANEL, C_PANEL, C_CYAN, C_GREEN, 0, 54, 70},
  {"forest", "Forest", 0x03A0, 0x1944, 0x2265, 0x1A24, C_GREEN, C_CYAN, 0, 70, 18},
  {"sunset", "Sunset", 0xC320, 0x3123, 0x49C4, 0x4143, C_ORANGE, C_YELLOW, 80, 30, 0},
  {"mono", "Mono", 0x4208, 0x2104, 0x3186, 0x2945, C_WHITE, C_CYAN, 42, 42, 42},
  {"aurora", "Aurora", 0x04B0, 0x1129, 0x214C, 0x124A, 0x07F0, 0xF81F, 0, 80, 54},
  {"ember", "Ember", 0xB104, 0x2882, 0x3903, 0x5943, C_ORANGE, C_YELLOW, 90, 24, 0},
  {"berry", "Berry", 0x8811, 0x2007, 0x302A, 0x4810, 0xF81F, 0x07FF, 84, 0, 60},
  {"ice", "Ice", 0x039F, 0x112C, 0x21CF, 0x1230, C_WHITE, C_CYAN, 20, 64, 90},
  {"mint", "Mint", 0x05E8, 0x1185, 0x2247, 0x1A06, 0x87F0, C_WHITE, 12, 82, 48},
  {"plum", "Plum", 0x6015, 0x2008, 0x302B, 0x4010, 0xA81F, 0xFD20, 64, 18, 82},
  {"steel", "Steel", 0x4A69, 0x2104, 0x3186, 0x39E7, 0x867D, C_WHITE, 46, 56, 68},
  {"amber", "Amber", 0xA380, 0x2920, 0x3A00, 0x5260, C_YELLOW, C_ORANGE, 86, 44, 0},
  {"neon", "Neon", 0x0014, 0x080C, 0x1027, 0x1810, 0x07F0, 0xF81F, 20, 90, 78},
  {"wine", "Wine", 0x7806, 0x2803, 0x3805, 0x5007, 0xF8AA, C_YELLOW, 86, 10, 28},
};

static constexpr uint8_t THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

void logf(const char *fmt, ...);
void setStatus(const char *fmt, ...);
void invalidateUi(bool fullRedraw = false);
const ThemeDef &theme();
int themeIndexById(String id);
void cycleTheme();
bool parseBoolValue(String value, bool fallback);
bool parseIntValue(String value, int &out);
int boundedIntValue(String value, int minimum, int maximum, int fallback);
LedEffect parseLedEffect(String value, LedEffect fallback);
const char *ledEffectId(LedEffect effect);
void appendLedEffectOptions(String &html, LedEffect active);
void storeRuntimeConfigToPrefs();
void applyLedBrightness();
void initBatteryMonitor();
void updateSystemStatus(bool force = false);
uint16_t readBatteryMillivolts();
uint8_t batteryPercentFromMv(uint16_t mv);
int8_t wifiBarsFromRssi(int rssi);
void configureNtp(bool force = false);
void updateClock(bool force = false);
String clockStatusText();
void setLedMode(LedMode mode);
void updateLed();
void initPixel();
void initDebug();
void initApName();
void drawBoot(const char *line);
void drawBootProgress(const char *line, uint8_t percent, bool error = false);
void evaluateSdBootState();
bool storageErrorActive();
String storageErrorText();
String sdLabelText();
uint16_t sdLabelColor();
void drawUi();
void drawMain(bool fullRedraw);
void drawMenu(bool fullRedraw);
void drawHeader(const char *title);
void drawWifiBars(int16_t x, int16_t y);
void drawBatteryStatus(int16_t x, int16_t y);
void drawCoverArt(int16_t x, int16_t y, int16_t w, int16_t h);
bool drawBmpCover(const String &path, int16_t x, int16_t y, int16_t w, int16_t h);
uint16_t read16(File &f);
uint32_t read32(File &f);
void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String &label, uint16_t bg, uint16_t fg);
void drawTextClip(int16_t x, int16_t y, int16_t w, const String &text, uint16_t color, uint8_t size);
String clipped(const String &text, uint16_t pixels, uint8_t size);
String networkText();
String stationsAsText();
String htmlEscape(String value);
void initSd();
void ensureSdFiles();
void ensureDefaultStationFile();
void ensureDefaultConfigFile();
void loadRuntimeConfig();
void applyConfigText(const String &text);
void applyConfigLine(String line);
bool saveRuntimeConfig(bool immediate = false);
void markConfigDirty();
void serviceConfigSave();
void loadStations();
void loadStationsFromText(const String &text);
void addStationLine(String line);
void addDefaultStations();
bool saveStationsText(const String &text);
String stationCoverPath(const String &name, uint8_t index);
String sanitizePathPart(String value);
void prepareCoverForStation(int index);
bool downloadCoverToSd(const String &url, const String &path);
void initAudio();
bool initEs8311();
bool writeEs8311(uint8_t reg, uint8_t value);
bool readEs8311(uint8_t reg, uint8_t &value);
void setEs8311Volume(uint8_t volume);
void setEs8311Muted(bool muted);
void setEs8311Bits(uint8_t bits);
void dumpEs8311();
void printEs8311Summary();
void setupPortalRoutes();
void startPortal();
void stopPortal();
bool connectWifi(bool showUi);
void reconnectWifi();
void clearWifi();
void handleRoot();
void handleSettingsSave();
void handleWifiSave();
void handleStationsSave();
void handleReboot();
void handleClearWifi();
void handleApOff();
void handleNotFound();
void startStation(int index);
void stopAudio();
void togglePlay();
void stationNext(int delta);
void reloadSdStations();
void setVolumeLevel(uint8_t newVolume);
void audioInfoCallback(Audio::msg_t m);
void handleTouch();
void handleMainTouch(uint16_t x, uint16_t y);
void handleMenuTouch(uint16_t x, uint16_t y);
bool inRect(uint16_t x, uint16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh);
void handleSerial();
void processSerialCommand(String cmd);
void serviceNetwork();
void printHelp();
void printConfigSummary();
void printSdSummary();
void printStatus();
void traceI2sIfNeeded();

void logf(const char *fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  DEBUG_PORT.println(buffer);
}

void setStatus(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(audioStatus, sizeof(audioStatus), fmt, args);
  va_end(args);
  invalidateUi(false);
  logf("%s", audioStatus);
}

void invalidateUi(bool fullRedraw) {
  uiDirty = true;
  if (fullRedraw) {
    uiFullRedraw = true;
  }
}

const ThemeDef &theme() {
  if (themeIndex >= THEME_COUNT) {
    themeIndex = 0;
  }
  return THEMES[themeIndex];
}

int themeIndexById(String id) {
  id.trim();
  id.toLowerCase();
  for (uint8_t i = 0; i < THEME_COUNT; i++) {
    if (id == THEMES[i].id) {
      return i;
    }
  }
  return -1;
}

void cycleTheme() {
  themeIndex = (themeIndex + 1) % THEME_COUNT;
  prefs.putUChar("theme", themeIndex);
  markConfigDirty();
  setStatus("Theme: %s", theme().label);
  invalidateUi(true);
}

bool parseBoolValue(String value, bool fallback) {
  value.trim();
  value.toLowerCase();
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

bool parseIntValue(String value, int &out) {
  value.trim();
  if (!value.length()) {
    return false;
  }
  uint16_t pos = 0;
  bool negative = false;
  if (value[0] == '-' || value[0] == '+') {
    negative = value[0] == '-';
    pos = 1;
  }
  if (pos >= value.length()) {
    return false;
  }
  long parsed = 0;
  for (; pos < value.length(); pos++) {
    const char c = value[pos];
    if (c < '0' || c > '9') {
      return false;
    }
    parsed = parsed * 10 + (c - '0');
    if (parsed > 1000000L) {
      return false;
    }
  }
  out = negative ? -parsed : parsed;
  return true;
}

int boundedIntValue(String value, int minimum, int maximum, int fallback) {
  int parsed = 0;
  if (!parseIntValue(value, parsed)) {
    return fallback;
  }
  return constrain(parsed, minimum, maximum);
}

LedEffect parseLedEffect(String value, LedEffect fallback) {
  value.trim();
  value.toLowerCase();
  if (value == "off" || value == "none" || value == "0") {
    return LedEffect::Off;
  }
  if (value == "solid" || value == "on") {
    return LedEffect::Solid;
  }
  if (value == "breathe" || value == "breath" || value == "pulse") {
    return LedEffect::Breathe;
  }
  if (value == "blink" || value == "flash") {
    return LedEffect::Blink;
  }
  if (value == "vu" || value == "audio") {
    return LedEffect::Vu;
  }
  return fallback;
}

const char *ledEffectId(LedEffect effect) {
  switch (effect) {
    case LedEffect::Off:
      return "off";
    case LedEffect::Solid:
      return "solid";
    case LedEffect::Breathe:
      return "breathe";
    case LedEffect::Blink:
      return "blink";
    case LedEffect::Vu:
      return "vu";
  }
  return "breathe";
}

void appendLedEffectOptions(String &html, LedEffect active) {
  const LedEffect effects[] = {LedEffect::Off, LedEffect::Solid, LedEffect::Breathe, LedEffect::Blink, LedEffect::Vu};
  for (uint8_t i = 0; i < sizeof(effects) / sizeof(effects[0]); i++) {
    const char *id = ledEffectId(effects[i]);
    html += F("<option value='");
    html += id;
    html += "'";
    if (effects[i] == active) {
      html += F(" selected");
    }
    html += F(">");
    html += id;
    html += F("</option>");
  }
}

void storeRuntimeConfigToPrefs() {
  prefs.putUChar("theme", themeIndex);
  prefs.putUChar("volume", volumeLevel);
  prefs.putBool("autoplay", autoPlay);
  prefs.putBool("cover", coverDownload);
  prefs.putBool("ledEn", ledEnabled);
  prefs.putUChar("ledBright", ledBrightness);
  prefs.putUChar("ledBootFx", static_cast<uint8_t>(ledBootEffect));
  prefs.putUChar("ledWifiFx", static_cast<uint8_t>(ledWifiEffect));
  prefs.putUChar("ledApFx", static_cast<uint8_t>(ledApEffect));
  prefs.putUChar("ledStreamFx", static_cast<uint8_t>(ledStreamingEffect));
  prefs.putUChar("ledIdleFx", static_cast<uint8_t>(ledIdleEffect));
  prefs.putUChar("ledErrFx", static_cast<uint8_t>(ledErrorEffect));
  prefs.putUInt("ledPulseMs", ledPulseMs);
  prefs.putUInt("ledBlinkMs", ledBlinkMs);
  prefs.putUInt("wifiRetryMs", wifiRetryMs);
  prefs.putUInt("wifiConnMs", wifiConnectTimeoutMs);
  prefs.putUInt("statusRefMs", statusRefreshMs);
  prefs.putUInt("touchDebMs", touchDebounceMs);
  prefs.putBool("ntpEn", ntpEnabled);
  prefs.putString("ntpServer", ntpServer);
  prefs.putString("tz", timezoneSpec);
  prefs.putBool("clock24h", clock24h);
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPassword);
  prefs.putBool("batEn", batteryEnabled);
  prefs.putUInt("batMinMv", batteryMinMv);
  prefs.putUInt("batMaxMv", batteryMaxMv);
  prefs.putUInt("batScale", batteryScalePermille);
  prefs.putBool("startApBoot", startApOnBoot);
  prefs.putInt("startupSt", startupStation);
}

void applyLedBrightness() {
  const uint8_t raw = ledEnabled ? static_cast<uint8_t>((static_cast<uint16_t>(ledBrightness) * 255U) / 100U) : 0;
  pixel.setBrightness(raw);
}

void initBatteryMonitor() {
  pinMode(LCDWIKI_ES3C28P_BATTERY_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(LCDWIKI_ES3C28P_BATTERY_ADC, ADC_11db);
  updateSystemStatus(true);
}

uint16_t readBatteryMillivolts() {
  if (!batteryEnabled) {
    batteryAdcMv = 0;
    return 0;
  }

  uint32_t rawSum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    rawSum += analogReadMilliVolts(LCDWIKI_ES3C28P_BATTERY_ADC);
    delay(1);
  }
  batteryAdcMv = static_cast<uint16_t>(rawSum / 8U);
  const uint32_t scaled = (static_cast<uint32_t>(batteryAdcMv) * batteryScalePermille + 500U) / 1000U;
  return scaled > 6500U ? 6500U : static_cast<uint16_t>(scaled);
}

uint8_t batteryPercentFromMv(uint16_t mv) {
  if (batteryMaxMv <= batteryMinMv) {
    return 0;
  }
  if (mv <= batteryMinMv) {
    return 0;
  }
  if (mv >= batteryMaxMv) {
    return 100;
  }
  return static_cast<uint8_t>(((static_cast<uint32_t>(mv - batteryMinMv) * 100U) + ((batteryMaxMv - batteryMinMv) / 2U)) / (batteryMaxMv - batteryMinMv));
}

int8_t wifiBarsFromRssi(int rssi) {
  if (WiFi.status() != WL_CONNECTED) {
    return 0;
  }
  if (rssi >= -55) {
    return 4;
  }
  if (rssi >= -67) {
    return 3;
  }
  if (rssi >= -78) {
    return 2;
  }
  return 1;
}

void updateSystemStatus(bool force) {
  const uint32_t now = millis();
  if (!force && now - lastSystemStatusMs < statusRefreshMs) {
    return;
  }
  lastSystemStatusMs = now;

  const bool oldBatteryValid = batteryValid;
  const uint8_t oldBatteryPercent = batteryPercent;
  const uint16_t oldBatteryMv = batteryMv;
  const int8_t oldWifiBars = wifiSignalBars;

  if (batteryEnabled) {
    batteryMv = readBatteryMillivolts();
    batteryValid = batteryMv >= 2500 && batteryMv <= 6500;
    batteryPercent = batteryValid ? batteryPercentFromMv(batteryMv) : 0;
  } else {
    batteryAdcMv = 0;
    batteryMv = 0;
    batteryPercent = 0;
    batteryValid = false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiRssiDbm = static_cast<int8_t>(WiFi.RSSI());
    wifiSignalBars = wifiBarsFromRssi(wifiRssiDbm);
  } else {
    wifiRssiDbm = 0;
    wifiSignalBars = 0;
  }

  const uint16_t mvDelta = oldBatteryMv > batteryMv ? oldBatteryMv - batteryMv : batteryMv - oldBatteryMv;
  if (oldBatteryValid != batteryValid || oldBatteryPercent != batteryPercent || mvDelta >= 50 || oldWifiBars != wifiSignalBars) {
    invalidateUi(false);
  }
}

void configureNtp(bool force) {
  if (!ntpEnabled || WiFi.status() != WL_CONNECTED || !ntpServer.length() || !timezoneSpec.length()) {
    return;
  }
  const uint32_t now = millis();
  if (!force && ntpConfigured && now - lastNtpConfigMs < NTP_RECONFIGURE_MS) {
    return;
  }
  configTzTime(timezoneSpec.c_str(), ntpServer.c_str(), "time.google.com", "time.nist.gov");
  ntpConfigured = true;
  lastNtpConfigMs = now;
  logf("NTP configured: server=%s tz=%s", ntpServer.c_str(), timezoneSpec.c_str());
}

void updateClock(bool force) {
  if (ntpEnabled && WiFi.status() == WL_CONNECTED) {
    configureNtp(false);
  }

  const uint32_t now = millis();
  if (!force && now - lastClockMs < DEFAULT_CLOCK_REFRESH_MS) {
    return;
  }
  lastClockMs = now;

  char nextText[sizeof(clockText)] = "--:--";
  bool nextValid = false;
  if (ntpEnabled) {
    struct tm timeInfo;
    if (getLocalTime(&timeInfo, 5)) {
      strftime(nextText, sizeof(nextText), clock24h ? "%H:%M" : "%I:%M", &timeInfo);
      nextValid = true;
    }
  }

  if (nextValid != clockValid || strcmp(nextText, clockText) != 0) {
    clockValid = nextValid;
    strncpy(clockText, nextText, sizeof(clockText) - 1);
    clockText[sizeof(clockText) - 1] = 0;
    invalidateUi(false);
  }
}

String clockStatusText() {
  if (!ntpEnabled) {
    return "off";
  }
  String text = clockValid ? String(clockText) : String("--:--");
  text += " ";
  text += ntpServer;
  return text;
}

void setLedMode(LedMode mode) {
  ledMode = mode;
  lastLedMs = 0;
}

uint8_t triangleWave(uint32_t ms, uint16_t period, uint8_t low, uint8_t high) {
  const uint16_t phase = ms % period;
  const uint16_t half = period / 2;
  const uint16_t ramp = phase < half ? phase : period - phase;
  return low + ((high - low) * ramp) / half;
}

void showPixel(uint8_t r, uint8_t g, uint8_t b) {
  if (!ledEnabled || ledBrightness == 0) {
    r = 0;
    g = 0;
    b = 0;
  }
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void showScaledPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t scale) {
  if (scale > 100) {
    scale = 100;
  }
  showPixel((static_cast<uint16_t>(r) * scale) / 100,
            (static_cast<uint16_t>(g) * scale) / 100,
            (static_cast<uint16_t>(b) * scale) / 100);
}

void showLedEffect(LedEffect effect, uint32_t now, uint8_t r, uint8_t g, uint8_t b, bool allowVu) {
  switch (effect) {
    case LedEffect::Off:
      showPixel(0, 0, 0);
      break;
    case LedEffect::Solid:
      showScaledPixel(r, g, b, 82);
      break;
    case LedEffect::Breathe:
      showScaledPixel(r, g, b, triangleWave(now, ledPulseMs, 10, 82));
      break;
    case LedEffect::Blink:
      showScaledPixel(r, g, b, (now / ledBlinkMs) % 2 ? 86 : 6);
      break;
    case LedEffect::Vu: {
      uint8_t scale = triangleWave(now, ledPulseMs, 12, 68);
      if (allowVu && playing && audio.isRunning()) {
        const uint16_t vu = audio.getVUlevel();
        scale += vu > 32 ? 32 : vu;
      }
      showScaledPixel(r, g, b, scale > 100 ? 100 : scale);
      break;
    }
  }
}

void updateLed() {
  const uint32_t now = millis();
  if (now - lastLedMs < 40) {
    return;
  }
  lastLedMs = now;

  if (errorUntilMs && now < errorUntilMs) {
    showLedEffect(ledErrorEffect, now, 90, 0, 0, false);
    return;
  }
  if (storageErrorActive()) {
    showPixel((now / 500) % 2 ? 90 : 0, 0, 0);
    return;
  }

  switch (ledMode) {
    case LedMode::Boot:
      showPixel((now / 1000) % 2 ? 26 : 0, (now / 1000) % 2 ? 34 : 0, (now / 1000) % 2 ? 70 : 0);
      break;
    case LedMode::Wifi:
      showLedEffect(ledWifiEffect, now, 90, 38, 0, false);
      break;
    case LedMode::Ap:
      showLedEffect(ledApEffect, now, 0, 30, 90, false);
      break;
    case LedMode::Streaming: {
      const ThemeDef &t = theme();
      showLedEffect(ledStreamingEffect, now, t.ledR, t.ledG, t.ledB, true);
      break;
    }
    case LedMode::Idle:
      if (WiFi.status() == WL_CONNECTED) {
        showLedEffect(ledIdleEffect, now, 0, 70, 16, false);
      } else {
        showLedEffect(ledIdleEffect, now, 46, 28, 0, false);
      }
      break;
    case LedMode::Error:
      showLedEffect(ledErrorEffect, now, 90, 0, 0, false);
      break;
  }
}

void initPixel() {
  pixel.begin();
  applyLedBrightness();
  showPixel(8, 8, 8);
}

void initDebug() {
  DEBUG_PORT.begin(
    115200,
    SERIAL_8N1,
    DEBUG_UART_RX,
    DEBUG_UART_TX
  );
  delay(80);
  logf("");
  logf("LCDWiki ES3C28P Internet Radio");
  logf("Debug UART: GPIO43 TX / GPIO44 RX, 115200 baud");
  logf("Chip: %s, PSRAM: %u bytes", ESP.getChipModel(), ESP.getPsramSize());
}

void initApName() {
  const uint32_t chip = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(apName, sizeof(apName), "LCDWikiRadio-%04X", chip & 0xFFFF);
}

void drawBoot(const char *line) {
  if (!displayReady) {
    return;
  }
  Arduino_GFX *gfx = display.gfx();
  gfx->fillScreen(C_BLACK);
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  gfx->setTextColor(C_CYAN);
  gfx->setCursor(8, 16);
  gfx->print("LCDWiki Radio");
  gfx->setTextSize(1);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(8, 48);
  gfx->print(line);
}

void drawBootProgress(const char *line, uint8_t percent, bool error) {
  if (!displayReady) {
    return;
  }
  if (percent > 100) {
    percent = 100;
  }
  Arduino_GFX *gfx = display.gfx();
  gfx->fillScreen(C_BLACK);
  if (sdReady && SD_MMC.exists(BOOT_LOGO_FILE)) {
    drawBmpCover(String(BOOT_LOGO_FILE), 84, 34, 72, 72);
  } else {
    gfx->setTextWrap(false);
    gfx->setTextSize(2);
    gfx->setTextColor(error ? C_RED : C_CYAN);
    gfx->setCursor(16, 42);
    gfx->print("LCDWiki Radio");
  }
  gfx->setTextSize(1);
  gfx->setTextColor(error ? C_RED : C_WHITE);
  gfx->setCursor(14, 128);
  gfx->print(clipped(String(line), 212, 1));
  gfx->drawRect(14, 150, 212, 14, C_DIM);
  gfx->fillRect(16, 152, (208 * percent) / 100, 10, error ? C_RED : theme().accent);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(14, 176);
  gfx->print("SD config boot protocol");
  updateLed();
}

void evaluateSdBootState() {
  bootSdMissing = !sdReady;
  bootConfigMissing = sdReady && !SD_MMC.exists(CONFIG_FILE);
  bootStationsMissing = sdReady && !SD_MMC.exists(STATIONS_FILE);
  if (sdReady && !SD_MMC.exists(COVERS_DIR)) {
    SD_MMC.mkdir(COVERS_DIR);
  }
}

bool storageErrorActive() {
  return bootSdMissing || bootConfigMissing || bootStationsMissing || bootStationsInvalid;
}

String storageErrorText() {
  if (bootSdMissing) {
    return "Brak karty SD";
  }
  if (bootConfigMissing) {
    return "Brak /radio.cfg na SD";
  }
  if (bootStationsMissing) {
    return "Brak /stations.csv na SD";
  }
  if (bootStationsInvalid) {
    return "Bledny /stations.csv";
  }
  return "SD ok";
}

String sdLabelText() {
  if (storageErrorActive()) {
    return "SD";
  }
  return sdReady ? "SD" : "noSD";
}

uint16_t sdLabelColor() {
  if (storageErrorActive()) {
    return C_RED;
  }
  return sdReady ? C_GREEN : C_YELLOW;
}

String clipped(const String &text, uint16_t pixels, uint8_t size) {
  const uint16_t charWidth = 6 * size;
  if (charWidth == 0) {
    return text;
  }
  const uint16_t maxChars = pixels / charWidth;
  if (text.length() <= maxChars) {
    return text;
  }
  if (maxChars <= 3) {
    return text.substring(0, maxChars);
  }
  return text.substring(0, maxChars - 3) + "...";
}

void drawTextClip(int16_t x, int16_t y, int16_t w, const String &text, uint16_t color, uint8_t size) {
  Arduino_GFX *gfx = display.gfx();
  gfx->setTextWrap(false);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(x, y);
  gfx->print(clipped(text, w, size));
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String &label, uint16_t bg, uint16_t fg) {
  Arduino_GFX *gfx = display.gfx();
  gfx->fillRect(x, y, w, h, bg);
  gfx->drawRect(x, y, w, h, C_DIM);
  const uint8_t size = label.length() > 8 ? 1 : 2;
  const String text = clipped(label, w - 8, size);
  const int16_t textW = text.length() * 6 * size;
  const int16_t textH = 8 * size;
  gfx->setTextSize(size);
  gfx->setTextColor(fg);
  const int16_t textX = (w - textW) / 2;
  const int16_t textY = (h - textH) / 2;
  gfx->setCursor(x + (textX > 4 ? textX : 4), y + (textY > 4 ? textY : 4));
  gfx->print(text);
}

uint16_t read16(File &f) {
  uint16_t value = f.read();
  value |= static_cast<uint16_t>(f.read()) << 8;
  return value;
}

uint32_t read32(File &f) {
  uint32_t value = read16(f);
  value |= static_cast<uint32_t>(read16(f)) << 16;
  return value;
}

bool drawBmpCover(const String &path, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!sdReady || !path.length() || !SD_MMC.exists(path)) {
    return false;
  }

  File bmp = SD_MMC.open(path, FILE_READ);
  if (!bmp) {
    return false;
  }

  if (read16(bmp) != 0x4D42) {
    bmp.close();
    return false;
  }
  (void)read32(bmp);
  (void)read32(bmp);
  const uint32_t dataOffset = read32(bmp);
  const uint32_t headerSize = read32(bmp);
  if (headerSize < 40) {
    bmp.close();
    return false;
  }
  const int32_t bmpW = static_cast<int32_t>(read32(bmp));
  const int32_t bmpHRaw = static_cast<int32_t>(read32(bmp));
  const bool bottomUp = bmpHRaw > 0;
  const int32_t bmpH = bottomUp ? bmpHRaw : -bmpHRaw;
  const uint16_t planes = read16(bmp);
  const uint16_t bpp = read16(bmp);
  const uint32_t compression = read32(bmp);
  if (planes != 1 || bmpW <= 0 || bmpH <= 0 || (bpp != 16 && bpp != 24) || (compression != 0 && compression != 3)) {
    bmp.close();
    return false;
  }

  const uint32_t rowSize = ((static_cast<uint32_t>(bmpW) * bpp + 31) / 32) * 4;
  Arduino_GFX *gfx = display.gfx();
  for (int16_t oy = 0; oy < h; oy++) {
    const int32_t srcY = (static_cast<int32_t>(oy) * bmpH) / h;
    const int32_t fileY = bottomUp ? (bmpH - 1 - srcY) : srcY;
    for (int16_t ox = 0; ox < w; ox++) {
      const int32_t srcX = (static_cast<int32_t>(ox) * bmpW) / w;
      const uint32_t pos = dataOffset + fileY * rowSize + srcX * (bpp / 8);
      if (!bmp.seek(pos)) {
        bmp.close();
        return false;
      }
      uint16_t color = C_BLACK;
      if (bpp == 24) {
        const uint8_t b = bmp.read();
        const uint8_t g = bmp.read();
        const uint8_t r = bmp.read();
        color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      } else {
        color = read16(bmp);
      }
      gfx->drawPixel(x + ox, y + oy, color);
    }
  }

  bmp.close();
  return true;
}

bool isAsciiAlphaNum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

char upperAscii(char c) {
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 'A';
  }
  return c;
}

void drawCoverArt(int16_t x, int16_t y, int16_t w, int16_t h) {
  Arduino_GFX *gfx = display.gfx();
  gfx->fillRect(x, y, w, h, theme().panel);
  gfx->drawRect(x, y, w, h, C_DIM);

  if (stationCount && drawBmpCover(stations[currentStation].coverPath, x + 1, y + 1, w - 2, h - 2)) {
    gfx->drawRect(x, y, w, h, C_WHITE);
    return;
  }

  String label = stationCount ? stations[currentStation].name : String("?");
  label.trim();
  char a = '?';
  char b = 0;
  int pos = 0;
  while (pos < label.length() && !isAsciiAlphaNum(label[pos])) {
    pos++;
  }
  if (pos < label.length()) {
    a = upperAscii(label[pos]);
  }
  int space = label.indexOf(' ', pos + 1);
  while (space >= 0 && space + 1 < label.length() && !isAsciiAlphaNum(label[space + 1])) {
    space = label.indexOf(' ', space + 1);
  }
  if (space >= 0 && space + 1 < label.length()) {
    b = upperAscii(label[space + 1]);
  }

  uint32_t hash = 2166136261UL;
  for (uint16_t i = 0; i < label.length(); i++) {
    hash ^= static_cast<uint8_t>(label[i]);
    hash *= 16777619UL;
  }
  const uint8_t r = 40 + (hash & 0x5F);
  const uint8_t g = 40 + ((hash >> 8) & 0x5F);
  const uint8_t bl = 40 + ((hash >> 16) & 0x5F);
  const uint16_t bg = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (bl >> 3);
  gfx->fillRect(x + 3, y + 3, w - 6, h - 6, bg);
  gfx->setTextWrap(false);
  gfx->setTextColor(C_WHITE);
  gfx->setTextSize(2);
  char initials[3] = {a, b ? b : 0, 0};
  const int16_t textW = (b ? 2 : 1) * 12;
  gfx->setCursor(x + (w - textW) / 2, y + (h - 16) / 2);
  gfx->print(initials);
}

String networkText() {
  if (WiFi.status() == WL_CONNECTED) {
    String s = "STA ";
    s += WiFi.localIP().toString();
    if (apActive) {
      s += " +AP";
    }
    return s;
  }
  if (apActive) {
    String s = "AP ";
    s += AP_IP.toString();
    return s;
  }
  return "Offline";
}

void drawWifiBars(int16_t x, int16_t y) {
  Arduino_GFX *gfx = display.gfx();
  const int8_t bars = WiFi.status() == WL_CONNECTED ? wifiSignalBars : 0;
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t h = 3 + i * 3;
    const int16_t bx = x + i * 5;
    const int16_t by = y + 12 - h;
    const uint16_t color = i < bars ? (bars <= 1 ? C_RED : (bars == 2 ? C_YELLOW : C_GREEN)) : C_DARK;
    gfx->fillRect(bx, by, 3, h, color);
  }
}

void drawBatteryStatus(int16_t x, int16_t y) {
  Arduino_GFX *gfx = display.gfx();
  const uint16_t color = !batteryValid ? C_DIM : (batteryPercent <= 15 ? C_RED : (batteryPercent <= 35 ? C_YELLOW : C_GREEN));
  gfx->drawRect(x, y, 25, 12, C_WHITE);
  gfx->fillRect(x + 25, y + 4, 2, 4, C_WHITE);
  gfx->fillRect(x + 2, y + 2, 21, 8, C_BLACK);
  if (batteryValid) {
    const uint8_t fillW = (static_cast<uint16_t>(batteryPercent) * 21U) / 100U;
    if (fillW) {
      gfx->fillRect(x + 2, y + 2, fillW, 8, color);
    }
  } else {
    drawTextClip(x + 6, y + 2, 16, "?", C_DIM, 1);
  }

  String text = batteryValid ? String(batteryPercent) + "%" : String("--");
  drawTextClip(x + 32, y + 2, 26, text, color, 1);
}

void drawHeader(const char *title) {
  Arduino_GFX *gfx = display.gfx();
  const ThemeDef &t = theme();
  gfx->fillRect(0, 0, 240, 32, t.header);
  drawTextClip(6, 7, 84, title, C_WHITE, 2);
  drawTextClip(6, 22, 74, clockText, clockValid ? C_WHITE : C_DIM, 1);
  String vol = "V";
  vol += volumeLevel;
  drawTextClip(94, 4, 28, vol, C_WHITE, 1);
  drawWifiBars(126, 4);
  drawBatteryStatus(154, 3);
  drawTextClip(94, 20, 44, sdLabelText(), sdLabelColor(), 1);
  drawTextClip(142, 20, 92, playing ? "PLAY" : "STOP", playing ? C_CYAN : C_DIM, 1);
}

void drawMain(bool fullRedraw) {
  Arduino_GFX *gfx = display.gfx();
  const ThemeDef &t = theme();
  if (fullRedraw) {
    gfx->fillScreen(C_BLACK);
  }
  drawHeader("Radio");

  gfx->fillRect(0, 32, 240, 58, t.status);
  drawTextClip(6, 38, 172, networkText(), WiFi.status() == WL_CONNECTED ? C_GREEN : C_YELLOW, 1);
  const String statusLine = storageErrorActive() ? storageErrorText() : (streamTitle[0] ? String(streamTitle) : String(audioStatus));
  drawTextClip(6, 54, 172, statusLine, storageErrorActive() ? C_RED : C_WHITE, 1);
  drawTextClip(6, 70, 172, stationCount ? stations[currentStation].name : String("No stations"), C_DIM, 1);
  drawCoverArt(184, 36, 50, 50);

  const int listY = 94;
  const int rowH = 27;
  for (int i = 0; i < 5; i++) {
    const int idx = scrollStation + i;
    const int y = listY + i * rowH;
    uint16_t bg = t.panel;
    uint16_t fg = C_WHITE;
    if (idx == currentStation && playing) {
      bg = t.highlight;
      fg = C_BLACK;
    } else if (idx == currentStation) {
      bg = t.accent;
      fg = C_BLACK;
    }
    if (idx < stationCount) {
      drawButton(4, y, 232, rowH - 3, stations[idx].name, bg, fg);
    } else {
      gfx->fillRect(4, y, 232, rowH - 3, C_BLACK);
      gfx->drawRect(4, y, 232, rowH - 3, C_DARK);
    }
  }

  drawButton(4, 234, 70, 36, "Prev", t.button, C_WHITE);
  drawButton(84, 234, 72, 36, playing ? "Stop" : "Play", playing ? C_RED : t.accent, playing ? C_WHITE : C_BLACK);
  drawButton(166, 234, 70, 36, "Next", t.button, C_WHITE);
  drawButton(4, 276, 70, 36, "Vol-", t.button, C_WHITE);
  drawButton(84, 276, 72, 36, "Menu", t.header, C_WHITE);
  drawButton(166, 276, 70, 36, "Vol+", t.button, C_WHITE);
}

void drawMenu(bool fullRedraw) {
  Arduino_GFX *gfx = display.gfx();
  const ThemeDef &t = theme();
  if (fullRedraw) {
    gfx->fillScreen(C_BLACK);
  }
  drawHeader("Menu");
  drawTextClip(8, 40, 224, networkText(), WiFi.status() == WL_CONNECTED ? C_GREEN : C_YELLOW, 1);
  drawTextClip(8, 56, 224, storageErrorActive() ? storageErrorText() : String(audioStatus), storageErrorActive() ? C_RED : C_DIM, 1);

  drawButton(12, 72, 216, 32, apActive ? "AP Off" : "Start AP", apActive ? C_ORANGE : t.header, apActive ? C_BLACK : C_WHITE);
  drawButton(12, 112, 216, 32, "Reload SD", t.button, C_WHITE);
  drawButton(12, 152, 216, 32, "Reconnect WiFi", t.button, C_WHITE);
  drawButton(12, 192, 216, 32, String("Theme: ") + theme().label, t.accent, C_BLACK);
  drawButton(12, 232, 216, 32, "Clear WiFi", C_RED, C_WHITE);
  drawButton(12, 276, 216, 34, "Back", t.highlight, C_BLACK);
}

void drawUi() {
  if (!displayReady || !uiDirty) {
    return;
  }
  const bool fullRedraw = uiFullRedraw;
  uiDirty = false;
  uiFullRedraw = false;
  if (uiScreen == UiScreen::Menu) {
    drawMenu(fullRedraw);
  } else {
    drawMain(fullRedraw);
  }
}

String stationsAsText() {
  String text;
  text.reserve(1024);
  for (uint8_t i = 0; i < stationCount; i++) {
    text += stations[i].name;
    text += "|";
    text += stations[i].url;
    if (stations[i].coverUrl.length()) {
      text += "|";
      text += stations[i].coverUrl;
    }
    text += "\n";
  }
  if (!stationCount) {
    text = DEFAULT_STATIONS;
  }
  return text;
}

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  return value;
}

void initSd() {
  sdReady = false;
  SD_MMC.end();

  SD_MMC.setPins(
    LCDWIKI_ES3C28P_SD_CLK,
    LCDWIKI_ES3C28P_SD_CMD,
    LCDWIKI_ES3C28P_SD_D0,
    LCDWIKI_ES3C28P_SD_D1,
    LCDWIKI_ES3C28P_SD_D2,
    LCDWIKI_ES3C28P_SD_D3
  );

  sdReady = SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_DEFAULT, 8);
  if (!sdReady) {
    SD_MMC.end();
    SD_MMC.setPins(
      LCDWIKI_ES3C28P_SD_CLK,
      LCDWIKI_ES3C28P_SD_CMD,
      LCDWIKI_ES3C28P_SD_D0
    );
    sdReady = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 8);
  }

  if (sdReady) {
    logf("SD mounted: %llu MB", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    evaluateSdBootState();
    ensureSdFiles();
  } else {
    evaluateSdBootState();
    logf("SD mount failed");
  }
  uiDirty = true;
}

void ensureDefaultStationFile() {
  if (!sdReady || SD_MMC.exists(STATIONS_FILE)) {
    return;
  }
  File f = SD_MMC.open(STATIONS_FILE, "w");
  if (!f) {
    logf("Cannot create %s", STATIONS_FILE);
    return;
  }
  f.print(DEFAULT_STATIONS);
  f.close();
  logf("Created default %s", STATIONS_FILE);
}

void ensureDefaultConfigFile() {
  if (!sdReady || SD_MMC.exists(CONFIG_FILE)) {
    return;
  }
  File f = SD_MMC.open(CONFIG_FILE, "w");
  if (!f) {
    logf("Cannot create %s", CONFIG_FILE);
    return;
  }
  f.print(DEFAULT_CONFIG);
  f.close();
  logf("Created default %s", CONFIG_FILE);
}

void ensureSdFiles() {
  if (sdReady && !SD_MMC.exists(COVERS_DIR)) {
    SD_MMC.mkdir(COVERS_DIR);
  }
}

String sanitizePathPart(String value) {
  value.trim();
  value.toLowerCase();
  String out;
  out.reserve(value.length());
  for (uint16_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    } else if (c == ' ' || c == '-' || c == '_') {
      if (!out.endsWith("_")) {
        out += '_';
      }
    }
  }
  if (!out.length()) {
    out = "station";
  }
  return out.substring(0, 24);
}

String stationCoverPath(const String &name, uint8_t index) {
  String path = COVERS_DIR;
  path += "/";
  path += String(index);
  path += "_";
  path += sanitizePathPart(name);
  path += ".bmp";
  return path;
}

void addStationLine(String line) {
  line.trim();
  if (!line.length() || line.startsWith("#")) {
    return;
  }
  int sep = line.indexOf('|');
  if (sep < 0) {
    sep = line.indexOf(',');
  }
  if (sep <= 0) {
    return;
  }
  String name = line.substring(0, sep);
  String rest = line.substring(sep + 1);
  int coverSep = rest.indexOf('|');
  String url = coverSep >= 0 ? rest.substring(0, coverSep) : rest;
  String coverUrl = coverSep >= 0 ? rest.substring(coverSep + 1) : "";
  name.trim();
  url.trim();
  coverUrl.trim();
  if (!name.length() || !(url.startsWith("http://") || url.startsWith("https://"))) {
    return;
  }
  if (stationCount >= MAX_STATIONS) {
    return;
  }
  stations[stationCount] = {name, url, coverUrl, stationCoverPath(name, stationCount)};
  stationCount++;
}

void loadStationsFromText(const String &text) {
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) {
      end = text.length();
    }
    addStationLine(text.substring(start, end));
    start = end + 1;
  }
}

void addDefaultStations() {
  loadStationsFromText(String(DEFAULT_STATIONS));
}

void loadStations() {
  stationCount = 0;
  stationsLoadedFromSd = false;
  bootStationsInvalid = false;

  if (!sdReady) {
    setStatus("SD missing - stations not loaded");
    uiDirty = true;
    return;
  }
  if (!SD_MMC.exists(STATIONS_FILE)) {
    bootStationsMissing = true;
    setStatus("Missing %s", STATIONS_FILE);
    uiDirty = true;
    return;
  }

  File f = SD_MMC.open(STATIONS_FILE, FILE_READ);
  if (!f) {
    bootStationsInvalid = true;
    setStatus("Cannot read %s", STATIONS_FILE);
    uiDirty = true;
    return;
  }
  String text = f.readString();
  f.close();
  loadStationsFromText(text);
  stationsLoadedFromSd = stationCount > 0;
  bootStationsInvalid = !stationsLoadedFromSd;
  logf("Loaded %u stations from SD", stationCount);

  if (stationCount == 0) {
    setStatus("Invalid %s", STATIONS_FILE);
    uiDirty = true;
    return;
  }
  const int maxStationIndex = stationCount > 0 ? stationCount - 1 : 0;
  const int maxScrollIndex = stationCount > 5 ? stationCount - 5 : 0;
  const int savedStation = prefs.getInt("station", 0);
  currentStation = constrain(startupStation >= 0 ? startupStation : savedStation, 0, maxStationIndex);
  scrollStation = scrollStation < maxScrollIndex ? scrollStation : maxScrollIndex;
  if (currentStation < scrollStation) {
    scrollStation = currentStation;
  }
  if (currentStation >= scrollStation + 5) {
    scrollStation = currentStation > 4 ? currentStation - 4 : 0;
  }
  uiDirty = true;
}

bool saveStationsText(const String &text) {
  if (!sdReady) {
    setStatus("Cannot save stations: SD missing");
    return false;
  }
  Station oldStations[MAX_STATIONS];
  const uint8_t oldCount = stationCount;
  for (uint8_t i = 0; i < oldCount; i++) {
    oldStations[i] = stations[i];
  }

  stationCount = 0;
  loadStationsFromText(text);
  if (!stationCount) {
    for (uint8_t i = 0; i < oldCount; i++) {
      stations[i] = oldStations[i];
    }
    stationCount = oldCount;
    setStatus("Stations not saved: empty list");
    return false;
  }
  File f = SD_MMC.open(STATIONS_FILE, "w");
  if (!f) {
    for (uint8_t i = 0; i < oldCount; i++) {
      stations[i] = oldStations[i];
    }
    stationCount = oldCount;
    setStatus("Cannot write %s", STATIONS_FILE);
    return false;
  }
  f.print(text);
  if (!text.endsWith("\n")) {
    f.println();
  }
  f.close();

  stationCount = 0;
  loadStationsFromText(text);
  stationsLoadedFromSd = stationCount > 0;
  bootStationsMissing = false;
  bootStationsInvalid = !stationsLoadedFromSd;
  const int maxStationIndex = stationCount > 0 ? stationCount - 1 : 0;
  const int maxScrollIndex = stationCount > 5 ? stationCount - 5 : 0;
  currentStation = constrain(currentStation, 0, maxStationIndex);
  scrollStation = scrollStation < maxScrollIndex ? scrollStation : maxScrollIndex;
  uiDirty = true;
  return true;
}

void applyConfigLine(String line) {
  line.trim();
  if (!line.length() || line.startsWith("#")) {
    return;
  }
  const int sep = line.indexOf('=');
  if (sep <= 0) {
    return;
  }
  String key = line.substring(0, sep);
  String value = line.substring(sep + 1);
  key.trim();
  key.toLowerCase();
  value.trim();

  if (key == "theme") {
    const int idx = themeIndexById(value);
    if (idx >= 0) {
      themeIndex = idx;
      prefs.putUChar("theme", themeIndex);
    }
  } else if (key == "volume") {
    volumeLevel = static_cast<uint8_t>(boundedIntValue(value, 0, MAX_VOLUME, volumeLevel));
    prefs.putUChar("volume", volumeLevel);
  } else if (key == "autoplay") {
    autoPlay = parseBoolValue(value, autoPlay);
    prefs.putBool("autoplay", autoPlay);
  } else if (key == "cover_download") {
    coverDownload = parseBoolValue(value, coverDownload);
    prefs.putBool("cover", coverDownload);
  } else if (key == "startup_station") {
    String normalized = value;
    normalized.toLowerCase();
    if (normalized == "last") {
      startupStation = -1;
    } else {
      startupStation = boundedIntValue(value, 0, MAX_STATIONS - 1, startupStation);
    }
    prefs.putInt("startupSt", startupStation);
  } else if (key == "wifi_ssid" || key == "ssid") {
    wifiSsid = value;
    prefs.putString("ssid", wifiSsid);
  } else if (key == "wifi_password" || key == "wifi_pass" || key == "password") {
    wifiPassword = value;
    prefs.putString("pass", wifiPassword);
  } else if (key == "start_ap_on_boot") {
    startApOnBoot = parseBoolValue(value, startApOnBoot);
    prefs.putBool("startApBoot", startApOnBoot);
  } else if (key == "wifi_retry_seconds") {
    wifiRetryMs = static_cast<uint32_t>(boundedIntValue(value, 5, 120, wifiRetryMs / 1000)) * 1000UL;
    prefs.putUInt("wifiRetryMs", wifiRetryMs);
  } else if (key == "wifi_connect_timeout_seconds") {
    wifiConnectTimeoutMs = static_cast<uint32_t>(boundedIntValue(value, 5, 60, wifiConnectTimeoutMs / 1000)) * 1000UL;
    prefs.putUInt("wifiConnMs", wifiConnectTimeoutMs);
  } else if (key == "status_refresh_ms") {
    statusRefreshMs = static_cast<uint32_t>(boundedIntValue(value, 1000, 30000, statusRefreshMs));
    prefs.putUInt("statusRefMs", statusRefreshMs);
  } else if (key == "ntp_enabled") {
    ntpEnabled = parseBoolValue(value, ntpEnabled);
    ntpConfigured = false;
    prefs.putBool("ntpEn", ntpEnabled);
  } else if (key == "ntp_server") {
    if (value.length() && value.length() < 64) {
      ntpServer = value;
      ntpConfigured = false;
      prefs.putString("ntpServer", ntpServer);
    }
  } else if (key == "timezone" || key == "timezone_tz" || key == "tz") {
    if (value.length() && value.length() < 80) {
      timezoneSpec = value;
      ntpConfigured = false;
      prefs.putString("tz", timezoneSpec);
    }
  } else if (key == "clock_24h") {
    clock24h = parseBoolValue(value, clock24h);
    prefs.putBool("clock24h", clock24h);
  } else if (key == "touch_debounce_ms") {
    touchDebounceMs = static_cast<uint16_t>(boundedIntValue(value, 80, 800, touchDebounceMs));
    prefs.putUInt("touchDebMs", touchDebounceMs);
  } else if (key == "battery_enabled") {
    batteryEnabled = parseBoolValue(value, batteryEnabled);
    prefs.putBool("batEn", batteryEnabled);
  } else if (key == "battery_min_mv") {
    batteryMinMv = static_cast<uint16_t>(boundedIntValue(value, 2800, 3800, batteryMinMv));
    prefs.putUInt("batMinMv", batteryMinMv);
  } else if (key == "battery_max_mv") {
    batteryMaxMv = static_cast<uint16_t>(boundedIntValue(value, 3900, 4400, batteryMaxMv));
    prefs.putUInt("batMaxMv", batteryMaxMv);
  } else if (key == "battery_scale_permille") {
    batteryScalePermille = static_cast<uint16_t>(boundedIntValue(value, 1000, 4000, batteryScalePermille));
    prefs.putUInt("batScale", batteryScalePermille);
  } else if (key == "led_enabled") {
    ledEnabled = parseBoolValue(value, ledEnabled);
    prefs.putBool("ledEn", ledEnabled);
    applyLedBrightness();
  } else if (key == "led_brightness") {
    ledBrightness = static_cast<uint8_t>(boundedIntValue(value, 0, 100, ledBrightness));
    prefs.putUChar("ledBright", ledBrightness);
    applyLedBrightness();
  } else if (key == "led_boot_effect") {
    ledBootEffect = parseLedEffect(value, ledBootEffect);
    prefs.putUChar("ledBootFx", static_cast<uint8_t>(ledBootEffect));
  } else if (key == "led_wifi_effect") {
    ledWifiEffect = parseLedEffect(value, ledWifiEffect);
    prefs.putUChar("ledWifiFx", static_cast<uint8_t>(ledWifiEffect));
  } else if (key == "led_ap_effect") {
    ledApEffect = parseLedEffect(value, ledApEffect);
    prefs.putUChar("ledApFx", static_cast<uint8_t>(ledApEffect));
  } else if (key == "led_streaming_effect") {
    ledStreamingEffect = parseLedEffect(value, ledStreamingEffect);
    prefs.putUChar("ledStreamFx", static_cast<uint8_t>(ledStreamingEffect));
  } else if (key == "led_idle_effect") {
    ledIdleEffect = parseLedEffect(value, ledIdleEffect);
    prefs.putUChar("ledIdleFx", static_cast<uint8_t>(ledIdleEffect));
  } else if (key == "led_error_effect") {
    ledErrorEffect = parseLedEffect(value, ledErrorEffect);
    prefs.putUChar("ledErrFx", static_cast<uint8_t>(ledErrorEffect));
  } else if (key == "led_pulse_ms") {
    ledPulseMs = static_cast<uint16_t>(boundedIntValue(value, 600, 6000, ledPulseMs));
    prefs.putUInt("ledPulseMs", ledPulseMs);
  } else if (key == "led_blink_ms") {
    ledBlinkMs = static_cast<uint16_t>(boundedIntValue(value, 100, 2000, ledBlinkMs));
    prefs.putUInt("ledBlinkMs", ledBlinkMs);
  }
}

void applyConfigText(const String &text) {
  int start = 0;
  while (start < text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) {
      end = text.length();
    }
    applyConfigLine(text.substring(start, end));
    start = end + 1;
  }
}

void loadRuntimeConfig() {
  configLoadedFromSd = false;
  wifiSsid = "";
  wifiPassword = "";
  themeIndex = prefs.getUChar("theme", 0);
  if (themeIndex >= THEME_COUNT) {
    themeIndex = 0;
  }
  volumeLevel = prefs.getUChar("volume", DEFAULT_VOLUME);
  if (volumeLevel > MAX_VOLUME) {
    volumeLevel = DEFAULT_VOLUME;
  }
  autoPlay = prefs.getBool("autoplay", true);
  coverDownload = prefs.getBool("cover", true);
  ledEnabled = prefs.getBool("ledEn", true);
  ledBrightness = prefs.getUChar("ledBright", DEFAULT_LED_BRIGHTNESS);
  if (ledBrightness > 100) {
    ledBrightness = DEFAULT_LED_BRIGHTNESS;
  }
  uint8_t fx = prefs.getUChar("ledBootFx", static_cast<uint8_t>(LedEffect::Breathe));
  ledBootEffect = fx <= static_cast<uint8_t>(LedEffect::Vu) ? static_cast<LedEffect>(fx) : LedEffect::Breathe;
  fx = prefs.getUChar("ledWifiFx", static_cast<uint8_t>(LedEffect::Blink));
  ledWifiEffect = fx <= static_cast<uint8_t>(LedEffect::Vu) ? static_cast<LedEffect>(fx) : LedEffect::Blink;
  fx = prefs.getUChar("ledApFx", static_cast<uint8_t>(LedEffect::Breathe));
  ledApEffect = fx <= static_cast<uint8_t>(LedEffect::Vu) ? static_cast<LedEffect>(fx) : LedEffect::Breathe;
  fx = prefs.getUChar("ledStreamFx", static_cast<uint8_t>(LedEffect::Vu));
  ledStreamingEffect = fx <= static_cast<uint8_t>(LedEffect::Vu) ? static_cast<LedEffect>(fx) : LedEffect::Vu;
  fx = prefs.getUChar("ledIdleFx", static_cast<uint8_t>(LedEffect::Breathe));
  ledIdleEffect = fx <= static_cast<uint8_t>(LedEffect::Vu) ? static_cast<LedEffect>(fx) : LedEffect::Breathe;
  fx = prefs.getUChar("ledErrFx", static_cast<uint8_t>(LedEffect::Blink));
  ledErrorEffect = fx <= static_cast<uint8_t>(LedEffect::Vu) ? static_cast<LedEffect>(fx) : LedEffect::Blink;
  ledPulseMs = static_cast<uint16_t>(constrain(prefs.getUInt("ledPulseMs", DEFAULT_LED_PULSE_MS), 600U, 6000U));
  ledBlinkMs = static_cast<uint16_t>(constrain(prefs.getUInt("ledBlinkMs", DEFAULT_LED_BLINK_MS), 100U, 2000U));
  wifiRetryMs = constrain(prefs.getUInt("wifiRetryMs", DEFAULT_WIFI_RETRY_MS), 5000U, 120000U);
  wifiConnectTimeoutMs = constrain(prefs.getUInt("wifiConnMs", DEFAULT_WIFI_CONNECT_TIMEOUT_MS), 5000U, 60000U);
  statusRefreshMs = constrain(prefs.getUInt("statusRefMs", DEFAULT_STATUS_REFRESH_MS), 1000U, 30000U);
  ntpEnabled = prefs.getBool("ntpEn", true);
  ntpServer = prefs.getString("ntpServer", DEFAULT_NTP_SERVER);
  if (!ntpServer.length() || ntpServer.length() >= 64) {
    ntpServer = DEFAULT_NTP_SERVER;
  }
  timezoneSpec = prefs.getString("tz", DEFAULT_TIMEZONE);
  if (!timezoneSpec.length() || timezoneSpec.length() >= 80) {
    timezoneSpec = DEFAULT_TIMEZONE;
  }
  clock24h = prefs.getBool("clock24h", true);
  touchDebounceMs = static_cast<uint16_t>(constrain(prefs.getUInt("touchDebMs", DEFAULT_TOUCH_DEBOUNCE_MS), 80U, 800U));
  batteryEnabled = prefs.getBool("batEn", true);
  batteryMinMv = static_cast<uint16_t>(constrain(prefs.getUInt("batMinMv", DEFAULT_BATTERY_MIN_MV), 2800U, 3800U));
  batteryMaxMv = static_cast<uint16_t>(constrain(prefs.getUInt("batMaxMv", DEFAULT_BATTERY_MAX_MV), 3900U, 4400U));
  batteryScalePermille = static_cast<uint16_t>(constrain(prefs.getUInt("batScale", DEFAULT_BATTERY_SCALE_PERMILLE), 1000U, 4000U));
  if (batteryMaxMv <= batteryMinMv) {
    batteryMinMv = DEFAULT_BATTERY_MIN_MV;
    batteryMaxMv = DEFAULT_BATTERY_MAX_MV;
  }
  startApOnBoot = prefs.getBool("startApBoot", false);
  startupStation = prefs.getInt("startupSt", -1);
  if (startupStation < -1 || startupStation >= MAX_STATIONS) {
    startupStation = -1;
  }
  applyLedBrightness();

  if (!sdReady) {
    bootSdMissing = true;
    setStatus("SD missing - config not loaded");
    return;
  }
  if (!SD_MMC.exists(CONFIG_FILE)) {
    bootConfigMissing = true;
    setStatus("Missing %s", CONFIG_FILE);
    return;
  }

  File f = SD_MMC.open(CONFIG_FILE, FILE_READ);
  if (!f) {
    bootConfigMissing = true;
    setStatus("Cannot read %s", CONFIG_FILE);
    return;
  }
  const String text = f.readString();
  f.close();
  applyConfigText(text);
  configLoadedFromSd = true;
  bootConfigMissing = false;
  logf("Loaded config from SD: theme=%s volume=%u autoplay=%d cover=%d led=%d/%u%% wifi=%s",
       theme().id,
       volumeLevel,
       autoPlay ? 1 : 0,
       coverDownload ? 1 : 0,
       ledEnabled ? 1 : 0,
       ledBrightness,
       wifiSsid.length() ? wifiSsid.c_str() : "<empty>");
}

bool saveRuntimeConfig(bool immediate) {
  if (!immediate && (!configDirty || millis() - configDirtyMs < CONFIG_SAVE_DELAY_MS)) {
    return true;
  }
  storeRuntimeConfigToPrefs();
  if (!sdReady) {
    configDirty = false;
    setStatus("Cannot save config: SD missing");
    return false;
  }
  File f = SD_MMC.open(CONFIG_FILE, "w");
  if (!f) {
    logf("Cannot write %s", CONFIG_FILE);
    configDirty = false;
    setStatus("Cannot write %s", CONFIG_FILE);
    return false;
  }
  f.println("# LCDWiki Internet Radio config");
  f.println("# WiFi values are plain text on SD. Leave wifi_ssid empty to start setup AP.");
  f.print("# theme:");
  for (uint8_t i = 0; i < THEME_COUNT; i++) {
    f.print(i ? ", " : " ");
    f.print(THEMES[i].id);
  }
  f.println();
  f.println("# led effects: off, solid, breathe, blink, vu");
  f.println("# startup_station: last or station index");
  f.println("# safe ranges: volume 0..21, led_brightness 0..100, retry 5..120 s, timeout 5..60 s");
  f.println("# battery_scale_permille default 2000 means ADC voltage times 2.000");
  f.print("theme=");
  f.println(theme().id);
  f.print("volume=");
  f.println(volumeLevel);
  f.print("autoplay=");
  f.println(autoPlay ? 1 : 0);
  f.print("cover_download=");
  f.println(coverDownload ? 1 : 0);
  f.print("startup_station=");
  if (startupStation < 0) {
    f.println("last");
  } else {
    f.println(startupStation);
  }
  f.print("wifi_ssid=");
  f.println(wifiSsid);
  f.print("wifi_password=");
  f.println(wifiPassword);
  f.print("start_ap_on_boot=");
  f.println(startApOnBoot ? 1 : 0);
  f.print("wifi_retry_seconds=");
  f.println(wifiRetryMs / 1000UL);
  f.print("wifi_connect_timeout_seconds=");
  f.println(wifiConnectTimeoutMs / 1000UL);
  f.print("status_refresh_ms=");
  f.println(statusRefreshMs);
  f.print("ntp_enabled=");
  f.println(ntpEnabled ? 1 : 0);
  f.print("ntp_server=");
  f.println(ntpServer);
  f.print("timezone=");
  f.println(timezoneSpec);
  f.print("clock_24h=");
  f.println(clock24h ? 1 : 0);
  f.print("touch_debounce_ms=");
  f.println(touchDebounceMs);
  f.print("battery_enabled=");
  f.println(batteryEnabled ? 1 : 0);
  f.print("battery_min_mv=");
  f.println(batteryMinMv);
  f.print("battery_max_mv=");
  f.println(batteryMaxMv);
  f.print("battery_scale_permille=");
  f.println(batteryScalePermille);
  f.print("led_enabled=");
  f.println(ledEnabled ? 1 : 0);
  f.print("led_brightness=");
  f.println(ledBrightness);
  f.print("led_boot_effect=");
  f.println(ledEffectId(ledBootEffect));
  f.print("led_wifi_effect=");
  f.println(ledEffectId(ledWifiEffect));
  f.print("led_ap_effect=");
  f.println(ledEffectId(ledApEffect));
  f.print("led_streaming_effect=");
  f.println(ledEffectId(ledStreamingEffect));
  f.print("led_idle_effect=");
  f.println(ledEffectId(ledIdleEffect));
  f.print("led_error_effect=");
  f.println(ledEffectId(ledErrorEffect));
  f.print("led_pulse_ms=");
  f.println(ledPulseMs);
  f.print("led_blink_ms=");
  f.println(ledBlinkMs);
  f.close();
  configDirty = false;
  configLoadedFromSd = true;
  bootConfigMissing = false;
  logf("Saved %s", CONFIG_FILE);
  return true;
}

void markConfigDirty() {
  configDirty = true;
  configDirtyMs = millis();
  storeRuntimeConfigToPrefs();
}

void serviceConfigSave() {
  saveRuntimeConfig(false);
}

bool writeEs8311(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  Wire.write(reg);
  Wire.write(value);
  const bool ok = Wire.endTransmission() == 0;
  if (codecTrace) {
    DEBUG_PORT.printf("ES8311 I2C W reg=0x%02X val=0x%02X %s\n", reg, value, ok ? "ok" : "FAIL");
  }
  return ok;
}

bool readEs8311(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    if (codecTrace) {
      DEBUG_PORT.printf("ES8311 I2C R reg=0x%02X pointer FAIL\n", reg);
    }
    return false;
  }
  if (Wire.requestFrom(static_cast<uint8_t>(LCDWIKI_ES3C28P_AUDIO_ADDR), static_cast<uint8_t>(1)) != 1) {
    if (codecTrace) {
      DEBUG_PORT.printf("ES8311 I2C R reg=0x%02X data FAIL\n", reg);
    }
    return false;
  }
  value = Wire.read();
  if (codecTrace) {
    DEBUG_PORT.printf("ES8311 I2C R reg=0x%02X val=0x%02X ok\n", reg, value);
  }
  return true;
}

void setEs8311Volume(uint8_t volume) {
  volume = volume > 100 ? 100 : volume;
  const uint8_t reg32 = volume == 0 ? 0 : ((volume * 256U) / 100U) - 1U;
  writeEs8311(0x32, reg32);
}

void setEs8311Bits(uint8_t bits) {
  uint8_t code = 3;
  if (bits == 24) {
    code = 0;
  } else if (bits == 20) {
    code = 1;
  } else if (bits == 18) {
    code = 2;
  } else if (bits == 32) {
    code = 4;
  }
  const uint8_t value = code << 2;
  writeEs8311(0x09, value);
  writeEs8311(0x0A, value);
  setStatus("ES8311 bits %u", bits);
}

void setEs8311Muted(bool muted) {
  uint8_t reg31 = 0;
  if (!readEs8311(0x31, reg31)) {
    return;
  }
  if (muted) {
    reg31 |= 0x60;
  } else {
    reg31 &= static_cast<uint8_t>(~0x60);
  }
  writeEs8311(0x31, reg31);
}

bool initEs8311() {
  Wire.begin(LCDWIKI_ES3C28P_TOUCH_SDA, LCDWIKI_ES3C28P_TOUCH_SCL);
  Wire.setClock(LCDWIKI_ES3C28P_TOUCH_I2C_FREQUENCY);

  Wire.beginTransmission(LCDWIKI_ES3C28P_AUDIO_ADDR);
  if (Wire.endTransmission() != 0) {
    logf("ES8311 not detected at 0x%02X", LCDWIKI_ES3C28P_AUDIO_ADDR);
    return false;
  }

  bool ok = true;
  uint8_t reg = 0;

  // Mirrors the Espressif esp_codec_dev ES8311 open -> set_fs -> start sequence.
  if (readEs8311(0x0D, reg) && reg != 0xFA) {
    ok &= writeEs8311(0x0D, 0xFA);
  }
  ok &= writeEs8311(0x44, 0x08);
  ok &= writeEs8311(0x44, 0x08);
  ok &= writeEs8311(0x01, 0x30);
  ok &= writeEs8311(0x02, 0x00);
  ok &= writeEs8311(0x03, 0x10);
  ok &= writeEs8311(0x16, 0x24);
  ok &= writeEs8311(0x04, 0x10);
  ok &= writeEs8311(0x05, 0x00);
  ok &= writeEs8311(0x0B, 0x00);
  ok &= writeEs8311(0x0C, 0x00);
  ok &= writeEs8311(0x10, 0x1F);
  ok &= writeEs8311(0x11, 0x7F);
  ok &= writeEs8311(0x00, 0x80);

  if (readEs8311(0x00, reg)) {
    ok &= writeEs8311(0x00, reg & 0xBF);
  }
  ok &= writeEs8311(0x01, 0x3F);
  if (readEs8311(0x06, reg)) {
    ok &= writeEs8311(0x06, reg & static_cast<uint8_t>(~0x20));
  }
  ok &= writeEs8311(0x13, 0x10);
  ok &= writeEs8311(0x1B, 0x0A);
  ok &= writeEs8311(0x1C, 0x6A);
  ok &= writeEs8311(0x44, 0x58);

  // 48 kHz, 256fs MCLK = 12.288 MHz, 16-bit Philips I2S.
  ok &= writeEs8311(0x09, 0x0C);
  ok &= writeEs8311(0x0A, 0x0C);
  ok &= writeEs8311(0x02, 0x00);
  ok &= writeEs8311(0x05, 0x00);
  ok &= writeEs8311(0x03, 0x10);
  ok &= writeEs8311(0x04, 0x10);
  ok &= writeEs8311(0x06, 0x03);
  ok &= writeEs8311(0x07, 0x00);
  ok &= writeEs8311(0x08, 0xFF);

  ok &= writeEs8311(0x00, 0x80);
  ok &= writeEs8311(0x01, 0x3F);
  if (readEs8311(0x09, reg)) {
    ok &= writeEs8311(0x09, reg & static_cast<uint8_t>(~0x40));
  }
  if (readEs8311(0x0A, reg)) {
    ok &= writeEs8311(0x0A, reg & static_cast<uint8_t>(~0x40));
  }
  ok &= writeEs8311(0x17, 0xBF);
  ok &= writeEs8311(0x0E, 0x02);
  ok &= writeEs8311(0x12, 0x00);
  ok &= writeEs8311(0x14, 0x1A);
  if (readEs8311(0x14, reg)) {
    ok &= writeEs8311(0x14, reg & static_cast<uint8_t>(~0x40));
  }
  ok &= writeEs8311(0x0D, 0x01);
  ok &= writeEs8311(0x15, 0x40);
  ok &= writeEs8311(0x37, 0x08);
  ok &= writeEs8311(0x45, 0x00);
  ok &= writeEs8311(0x32, 0xF0);
  setEs8311Muted(false);

  logf("ES8311 init %s", ok ? "ok" : "with write errors");
  return ok;
}

void dumpEs8311() {
  DEBUG_PORT.println("ES8311 registers:");
  for (uint8_t i = 0; i < 0x4A; i++) {
    uint8_t value = 0;
    if (readEs8311(i, value)) {
      DEBUG_PORT.printf("0x%02X: 0x%02X\n", i, value);
    } else {
      DEBUG_PORT.printf("0x%02X: read failed\n", i);
    }
  }
}

void printEs8311Summary() {
  const uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0D, 0x0E, 0x12, 0x13, 0x14, 0x15, 0x17, 0x31, 0x32, 0x37, 0x44, 0x45};
  DEBUG_PORT.println("ES8311 key regs:");
  for (uint8_t i = 0; i < sizeof(regs); i++) {
    uint8_t value = 0;
    if (readEs8311(regs[i], value)) {
      DEBUG_PORT.printf("  0x%02X=0x%02X\n", regs[i], value);
    } else {
      DEBUG_PORT.printf("  0x%02X=read-fail\n", regs[i]);
    }
  }
  DEBUG_PORT.printf("Audio EN GPIO%u=%s (%s)\n",
                    LCDWIKI_ES3C28P_AUDIO_EN,
                    digitalRead(LCDWIKI_ES3C28P_AUDIO_EN) == LOW ? "LOW" : "HIGH",
                    LCDWIKI_ES3C28P_AUDIO_EN_ACTIVE == LOW ? "LOW enables amp" : "HIGH enables amp");
}

void initAudio() {
  pinMode(LCDWIKI_ES3C28P_AUDIO_EN, OUTPUT);
  digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LCDWIKI_ES3C28P_AUDIO_EN_ACTIVE);
  delay(10);

  Audio::audio_info_callback = audioInfoCallback;
  audio.setAudioTaskCore(0);
  audio.setConnectionTimeout(6000, 6000);
  audioPinoutReady = audio.setPinout(
    LCDWIKI_ES3C28P_AUDIO_BCLK,
    LCDWIKI_ES3C28P_AUDIO_LRCK,
    LCDWIKI_ES3C28P_AUDIO_DOUT,
    LCDWIKI_ES3C28P_AUDIO_MCLK
  );
  logf("Audio I2S pinout %s", audioPinoutReady ? "ok" : "failed");
  audio.setOutput48KHz(true);
  if (volumeLevel > MAX_VOLUME) {
    volumeLevel = DEFAULT_VOLUME;
  }
  audio.setVolume(volumeLevel);

  if (!initEs8311()) {
    setStatus("ES8311 init failed");
    errorUntilMs = millis() + 3500;
  }
}

void setupPortalRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/settings", HTTP_POST, handleSettingsSave);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/stations", HTTP_POST, handleStationsSave);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/clearwifi", HTTP_POST, handleClearWifi);
  server.on("/apoff", HTTP_POST, handleApOff);
  server.onNotFound(handleNotFound);
}

void startPortal() {
  if (apActive) {
    return;
  }
  WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  const bool ok = WiFi.softAP(apName, "radio1234");
  apActive = ok;
  if (ok) {
    dnsServer.start(DNS_PORT, "*", AP_IP);
    if (!portalStarted) {
      setupPortalRoutes();
      server.begin();
      portalStarted = true;
    }
    setStatus("AP %s pass radio1234", apName);
    setLedMode(LedMode::Ap);
  } else {
    setStatus("AP start failed");
    errorUntilMs = millis() + 3500;
  }
  uiDirty = true;
}

void stopPortal() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  apActive = false;
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    setLedMode(playing ? LedMode::Streaming : LedMode::Idle);
  } else {
    setLedMode(LedMode::Idle);
  }
  setStatus("AP off");
  uiDirty = true;
}

bool connectWifi(bool showUi) {
  if (!configLoadedFromSd) {
    setStatus("No /radio.cfg WiFi config");
    return false;
  }
  if (!wifiSsid.length()) {
    setStatus("No WiFi in /radio.cfg");
    return false;
  }

  if (showUi) {
    setStatus("WiFi connecting: %s", wifiSsid.c_str());
  }
  setLedMode(LedMode::Wifi);
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < wifiConnectTimeoutMs) {
    updateLed();
    if (portalStarted) {
      server.handleClient();
      dnsServer.processNextRequest();
    }
    delay(50);
  }

  if (WiFi.status() == WL_CONNECTED) {
    setStatus("WiFi connected %s", WiFi.localIP().toString().c_str());
    configureNtp(true);
    updateClock(true);
    setLedMode(apActive ? LedMode::Ap : LedMode::Idle);
    return true;
  }

  setStatus("WiFi connect failed");
  errorUntilMs = millis() + 3500;
  setLedMode(apActive ? LedMode::Ap : LedMode::Error);
  return false;
}

void reconnectWifi() {
  WiFi.disconnect(false);
  if (!connectWifi(true)) {
    startPortal();
  }
  uiDirty = true;
}

void clearWifi() {
  wifiSsid = "";
  wifiPassword = "";
  prefs.remove("ssid");
  prefs.remove("pass");
  markConfigDirty();
  const bool saved = saveRuntimeConfig(true);
  WiFi.disconnect(true);
  stopAudio();
  setStatus(saved ? "WiFi config cleared" : "WiFi clear not saved");
  startPortal();
}

void handleRoot() {
  String html;
  html.reserve(9000);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>LCDWiki Radio</title><style>");
  html += F("body{font-family:system-ui,Segoe UI,sans-serif;margin:20px;background:#101418;color:#e8eef2}");
  html += F("input,textarea,button,select{box-sizing:border-box;width:100%;font:inherit;margin:6px 0;padding:10px;border-radius:6px;border:1px solid #53616b;background:#172027;color:#fff}");
  html += F("button{background:#1b7f72;border:0;font-weight:700}.danger{background:#b23b3b}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.muted{color:#aab6bd;font-size:.92rem}");
  html += F("textarea{height:190px;font-family:ui-monospace,Consolas,monospace}.card{max-width:760px;margin:auto}");
  html += F("</style></head><body><main class='card'>");
  html += F("<h1>LCDWiki Radio</h1><p class='muted'>AP: ");
  html += htmlEscape(String(apName));
  html += F(" / pass: radio1234<br>Network: ");
  html += htmlEscape(networkText());
  html += F("<br>SD: ");
  html += sdReady ? F("mounted") : F("not mounted");
  html += F(" / ");
  html += htmlEscape(storageErrorText());
  html += F("<br>Battery: ");
  if (batteryValid) {
    html += htmlEscape(String(batteryMv) + " mV / " + String(batteryPercent) + "%");
  } else {
    html += F("unknown");
  }
  html += F("<br>WiFi signal: ");
  if (WiFi.status() == WL_CONNECTED) {
    html += htmlEscape(String(wifiRssiDbm) + " dBm / " + String(wifiSignalBars) + " bars");
  } else {
    html += F("not connected");
  }
  html += F("<br>Clock: ");
  html += htmlEscape(clockStatusText());
  html += F("</p><h2>WiFi</h2><form method='post' action='/wifi'>");
  html += F("<input name='ssid' placeholder='SSID' value='");
  html += htmlEscape(wifiSsid);
  html += F("'><input name='pass' placeholder='Password' type='password' value='");
  html += htmlEscape(wifiPassword);
  html += F("'><button>Save WiFi and connect</button></form>");
  html += F("<h2>Settings</h2><form method='post' action='/settings'><label class='muted'>Theme</label><select name='theme'>");
  for (uint8_t i = 0; i < THEME_COUNT; i++) {
    html += F("<option value='");
    html += THEMES[i].id;
    html += "'";
    if (i == themeIndex) {
      html += F(" selected");
    }
    html += F(">");
    html += THEMES[i].label;
    html += F("</option>");
  }
  html += F("</select><input name='volume' type='number' min='0' max='21' value='");
  html += String(volumeLevel);
  html += F("'><label><input name='autoplay' type='checkbox' style='width:auto' ");
  html += autoPlay ? F("checked") : F("");
  html += F("> Autoplay after boot</label><br><label><input name='cover' type='checkbox' style='width:auto' ");
  html += coverDownload ? F("checked") : F("");
  html += F("> Download BMP covers from stations.csv</label><br><label><input name='startap' type='checkbox' style='width:auto' ");
  html += startApOnBoot ? F("checked") : F("");
  html += F("> Start AP on boot</label><label class='muted'>Startup station</label><input name='startup' value='");
  html += startupStation < 0 ? String("last") : String(startupStation);
  html += F("'><div class='row'><label class='muted'>WiFi retry seconds<input name='retry' type='number' min='5' max='120' value='");
  html += String(wifiRetryMs / 1000UL);
  html += F("'></label><label class='muted'>WiFi timeout seconds<input name='timeout' type='number' min='5' max='60' value='");
  html += String(wifiConnectTimeoutMs / 1000UL);
  html += F("'></label></div><label class='muted'>Touch debounce ms</label><input name='touchdeb' type='number' min='80' max='800' value='");
  html += String(touchDebounceMs);
  html += F("'><label class='muted'>Status refresh ms</label><input name='statusrefresh' type='number' min='1000' max='30000' value='");
  html += String(statusRefreshMs);
  html += F("'><h2>Clock</h2><label><input name='ntpenabled' type='checkbox' style='width:auto' ");
  html += ntpEnabled ? F("checked") : F("");
  html += F("> NTP enabled</label><input name='ntpserver' placeholder='NTP server' value='");
  html += htmlEscape(ntpServer);
  html += F("'><input name='timezone' placeholder='POSIX timezone' value='");
  html += htmlEscape(timezoneSpec);
  html += F("'><label><input name='clock24h' type='checkbox' style='width:auto' ");
  html += clock24h ? F("checked") : F("");
  html += F("> 24-hour clock</label><h2>Battery</h2><label><input name='batenabled' type='checkbox' style='width:auto' ");
  html += batteryEnabled ? F("checked") : F("");
  html += F("> Enabled</label><div class='row'><label class='muted'>Min mV<input name='batmin' type='number' min='2800' max='3800' value='");
  html += String(batteryMinMv);
  html += F("'></label><label class='muted'>Max mV<input name='batmax' type='number' min='3900' max='4400' value='");
  html += String(batteryMaxMv);
  html += F("'></label></div><label class='muted'>ADC scale permille</label><input name='batscale' type='number' min='1000' max='4000' value='");
  html += String(batteryScalePermille);
  html += F("'><h2>LED</h2><label><input name='ledenabled' type='checkbox' style='width:auto' ");
  html += ledEnabled ? F("checked") : F("");
  html += F("> Enabled</label><input name='ledbrightness' type='number' min='0' max='100' value='");
  html += String(ledBrightness);
  html += F("'><div class='row'><label class='muted'>Pulse ms<input name='ledpulse' type='number' min='600' max='6000' value='");
  html += String(ledPulseMs);
  html += F("'></label><label class='muted'>Blink ms<input name='ledblink' type='number' min='100' max='2000' value='");
  html += String(ledBlinkMs);
  html += F("'></label></div><label class='muted'>Boot</label><select name='ledboot'>");
  appendLedEffectOptions(html, ledBootEffect);
  html += F("</select><label class='muted'>WiFi</label><select name='ledwifi'>");
  appendLedEffectOptions(html, ledWifiEffect);
  html += F("</select><label class='muted'>AP</label><select name='ledap'>");
  appendLedEffectOptions(html, ledApEffect);
  html += F("</select><label class='muted'>Streaming</label><select name='ledstream'>");
  appendLedEffectOptions(html, ledStreamingEffect);
  html += F("</select><label class='muted'>Idle</label><select name='ledidle'>");
  appendLedEffectOptions(html, ledIdleEffect);
  html += F("</select><label class='muted'>Error</label><select name='lederror'>");
  appendLedEffectOptions(html, ledErrorEffect);
  html += F("</select><button>Save settings</button></form>");
  html += F("<h2>Stations</h2><form method='post' action='/stations'><textarea name='stations'>");
  html += htmlEscape(stationsAsText());
  html += F("</textarea><button>Save stations</button></form>");
  html += F("<div class='row'><form method='post' action='/clearwifi'><button class='danger'>Clear WiFi</button></form>");
  html += F("<form method='post' action='/apoff'><button>Turn AP off</button></form></div>");
  html += F("<form method='post' action='/reboot'><button class='danger'>Reboot</button></form>");
  html += F("</main></body></html>");
  server.send(200, "text/html", html);
}

void handleWifiSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  pass.trim();
  if (ssid.length()) {
    if (!sdReady) {
      server.send(503, "text/html", "<p>Cannot save WiFi: SD card missing.</p><p><a href='/'>Back</a></p>");
      setStatus("WiFi not saved: SD missing");
      return;
    }
    wifiSsid = ssid;
    wifiPassword = pass;
    markConfigDirty();
    const bool wrote = saveRuntimeConfig(true);
    server.send(wrote ? 200 : 500, "text/html", wrote ? "<p>Saved to SD. Connecting...</p><p><a href='/'>Back</a></p>" : "<p>Cannot write /radio.cfg.</p><p><a href='/'>Back</a></p>");
    connectPending = wrote;
  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
}

void handleStationsSave() {
  const String text = server.arg("stations");
  const bool wroteSd = saveStationsText(text);
  String body = "<p>Stations saved ";
  body += wroteSd ? "to SD." : "failed. SD not available, write failed, or list is empty.";
  body += "</p><p><a href='/'>Back</a></p>";
  server.send(wroteSd ? 200 : 500, "text/html", body);
  if (wroteSd) {
    setStatus("Stations saved: %u", stationCount);
  } else {
    setStatus("Stations save failed");
  }
}

void handleReboot() {
  server.send(200, "text/html", "<p>Rebooting...</p>");
  delay(250);
  ESP.restart();
}

void handleClearWifi() {
  if (!sdReady) {
    server.send(503, "text/html", "<p>Cannot clear WiFi: SD card missing.</p><p><a href='/'>Back</a></p>");
    setStatus("WiFi clear failed: SD missing");
    return;
  }
  server.send(200, "text/html", "<p>WiFi cleared. AP remains active.</p><p><a href='/'>Back</a></p>");
  clearWifi();
}

void handleApOff() {
  server.send(200, "text/html", "<p>AP off.</p>");
  delay(100);
  stopPortal();
}

void handleNotFound() {
  if (apActive) {
    server.sendHeader("Location", String("http://") + AP_IP.toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void handleSettingsSave() {
  if (!sdReady) {
    server.send(503, "text/html", "<p>Cannot save settings: SD card missing.</p><p><a href='/'>Back</a></p>");
    setStatus("Settings not saved: SD missing");
    return;
  }
  const int idx = themeIndexById(server.arg("theme"));
  if (idx >= 0) {
    themeIndex = idx;
  }
  if (server.hasArg("volume")) {
    volumeLevel = static_cast<uint8_t>(boundedIntValue(server.arg("volume"), 0, MAX_VOLUME, volumeLevel));
    audio.setVolume(volumeLevel);
    setEs8311Muted(volumeLevel == 0);
  }
  autoPlay = server.hasArg("autoplay");
  coverDownload = server.hasArg("cover");
  startApOnBoot = server.hasArg("startap");
  if (server.hasArg("startup")) {
    String startup = server.arg("startup");
    startup.trim();
    startup.toLowerCase();
    startupStation = startup == "last" ? -1 : boundedIntValue(startup, 0, MAX_STATIONS - 1, startupStation);
  }
  if (server.hasArg("retry")) {
    wifiRetryMs = static_cast<uint32_t>(boundedIntValue(server.arg("retry"), 5, 120, wifiRetryMs / 1000UL)) * 1000UL;
  }
  if (server.hasArg("timeout")) {
    wifiConnectTimeoutMs = static_cast<uint32_t>(boundedIntValue(server.arg("timeout"), 5, 60, wifiConnectTimeoutMs / 1000UL)) * 1000UL;
  }
  if (server.hasArg("statusrefresh")) {
    statusRefreshMs = static_cast<uint32_t>(boundedIntValue(server.arg("statusrefresh"), 1000, 30000, statusRefreshMs));
  }
  ntpEnabled = server.hasArg("ntpenabled");
  if (server.hasArg("ntpserver")) {
    String value = server.arg("ntpserver");
    value.trim();
    if (value.length() && value.length() < 64) {
      ntpServer = value;
    }
  }
  if (server.hasArg("timezone")) {
    String value = server.arg("timezone");
    value.trim();
    if (value.length() && value.length() < 80) {
      timezoneSpec = value;
    }
  }
  clock24h = server.hasArg("clock24h");
  ntpConfigured = false;
  configureNtp(true);
  updateClock(true);
  if (server.hasArg("touchdeb")) {
    touchDebounceMs = static_cast<uint16_t>(boundedIntValue(server.arg("touchdeb"), 80, 800, touchDebounceMs));
  }
  batteryEnabled = server.hasArg("batenabled");
  if (server.hasArg("batmin")) {
    batteryMinMv = static_cast<uint16_t>(boundedIntValue(server.arg("batmin"), 2800, 3800, batteryMinMv));
  }
  if (server.hasArg("batmax")) {
    batteryMaxMv = static_cast<uint16_t>(boundedIntValue(server.arg("batmax"), 3900, 4400, batteryMaxMv));
  }
  if (batteryMaxMv <= batteryMinMv) {
    batteryMinMv = DEFAULT_BATTERY_MIN_MV;
    batteryMaxMv = DEFAULT_BATTERY_MAX_MV;
  }
  if (server.hasArg("batscale")) {
    batteryScalePermille = static_cast<uint16_t>(boundedIntValue(server.arg("batscale"), 1000, 4000, batteryScalePermille));
  }
  ledEnabled = server.hasArg("ledenabled");
  if (server.hasArg("ledbrightness")) {
    ledBrightness = static_cast<uint8_t>(boundedIntValue(server.arg("ledbrightness"), 0, 100, ledBrightness));
  }
  if (server.hasArg("ledpulse")) {
    ledPulseMs = static_cast<uint16_t>(boundedIntValue(server.arg("ledpulse"), 600, 6000, ledPulseMs));
  }
  if (server.hasArg("ledblink")) {
    ledBlinkMs = static_cast<uint16_t>(boundedIntValue(server.arg("ledblink"), 100, 2000, ledBlinkMs));
  }
  ledBootEffect = parseLedEffect(server.arg("ledboot"), ledBootEffect);
  ledWifiEffect = parseLedEffect(server.arg("ledwifi"), ledWifiEffect);
  ledApEffect = parseLedEffect(server.arg("ledap"), ledApEffect);
  ledStreamingEffect = parseLedEffect(server.arg("ledstream"), ledStreamingEffect);
  ledIdleEffect = parseLedEffect(server.arg("ledidle"), ledIdleEffect);
  ledErrorEffect = parseLedEffect(server.arg("lederror"), ledErrorEffect);
  applyLedBrightness();
  updateSystemStatus(true);
  markConfigDirty();
  const bool saved = saveRuntimeConfig(true);
  invalidateUi(true);
  server.send(saved ? 200 : 500, "text/html", saved ? "<p>Settings saved to SD.</p><p><a href='/'>Back</a></p>" : "<p>Cannot write /radio.cfg.</p><p><a href='/'>Back</a></p>");
  setStatus(saved ? "Settings saved" : "Settings save failed");
}

bool downloadCoverToSd(const String &url, const String &path) {
  if (!sdReady || !coverDownload || WiFi.status() != WL_CONNECTED || !url.length()) {
    return false;
  }
  String lower = url;
  lower.toLowerCase();
  if (!lower.startsWith("http://")) {
    logf("Cover skipped, only plain HTTP BMP is supported: %s", url.c_str());
    return false;
  }
  if (!lower.endsWith(".bmp")) {
    logf("Cover skipped, only BMP is supported now: %s", url.c_str());
    return false;
  }
  if (SD_MMC.exists(path)) {
    return true;
  }
  if (!SD_MMC.exists(COVERS_DIR)) {
    SD_MMC.mkdir(COVERS_DIR);
  }

  String rest = url.substring(7);
  const int slash = rest.indexOf('/');
  if (slash <= 0) {
    return false;
  }
  String hostPort = rest.substring(0, slash);
  String requestPath = rest.substring(slash);
  uint16_t port = 80;
  const int colon = hostPort.lastIndexOf(':');
  if (colon > 0) {
    port = static_cast<uint16_t>(hostPort.substring(colon + 1).toInt());
    hostPort = hostPort.substring(0, colon);
    if (!port) {
      port = 80;
    }
  }

  WiFiClient client;
  client.setTimeout(3500);
  if (!client.connect(hostPort.c_str(), port, 2500)) {
    logf("Cover connect failed: %s", hostPort.c_str());
    return false;
  }

  client.print(F("GET "));
  client.print(requestPath);
  client.print(F(" HTTP/1.1\r\nHost: "));
  client.print(hostPort);
  client.print(F("\r\nUser-Agent: LCDWikiRadio/1.0\r\nConnection: close\r\n\r\n"));

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  if (!statusLine.startsWith("HTTP/1.") || statusLine.indexOf(" 200 ") < 0) {
    logf("Cover HTTP rejected: %s", statusLine.c_str());
    client.stop();
    return false;
  }

  int contentLength = -1;
  bool chunked = false;
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (!line.length()) {
      break;
    }
    String header = line;
    header.toLowerCase();
    if (header.startsWith("content-length:")) {
      contentLength = line.substring(15).toInt();
    } else if (header.startsWith("transfer-encoding:") && header.indexOf("chunked") >= 0) {
      chunked = true;
    }
  }

  if (chunked) {
    logf("Cover skipped, chunked HTTP not supported: %s", url.c_str());
    client.stop();
    return false;
  }
  if (contentLength > 140000) {
    logf("Cover size rejected: %d", contentLength);
    client.stop();
    return false;
  }

  File f = SD_MMC.open(path, "w");
  if (!f) {
    client.stop();
    return false;
  }

  uint8_t buffer[512];
  int written = 0;
  uint32_t lastDataMs = millis();
  while ((client.connected() || client.available()) && written < 140000 && millis() - lastDataMs < 3500) {
    const int available = client.available();
    if (!available) {
      delay(5);
      continue;
    }
    const size_t chunk = static_cast<size_t>(available) > sizeof(buffer) ? sizeof(buffer) : static_cast<size_t>(available);
    const int n = client.readBytes(buffer, chunk);
    if (n > 0) {
      f.write(buffer, n);
      written += n;
      lastDataMs = millis();
    }
    if (contentLength >= 0 && written >= contentLength) {
      break;
    }
  }
  f.close();
  client.stop();
  const bool ok = written > 0 && (contentLength < 0 || written == contentLength) && written <= 140000;
  logf("Cover %s: %s (%d bytes)", ok ? "saved" : "partial", path.c_str(), written);
  if (!ok && SD_MMC.exists(path)) {
    SD_MMC.remove(path);
  }
  return ok;
}

void prepareCoverForStation(int index) {
  if (index < 0 || index >= stationCount) {
    return;
  }
  if (!stations[index].coverUrl.length()) {
    return;
  }
  downloadCoverToSd(stations[index].coverUrl, stations[index].coverPath);
}

void startStation(int index) {
  if (stationCount == 0) {
    setStatus("No stations");
    return;
  }
  index = (index + stationCount) % stationCount;
  currentStation = index;
  prefs.putInt("station", currentStation);
  if (currentStation < scrollStation) {
    scrollStation = currentStation;
  }
  if (currentStation >= scrollStation + 5) {
    scrollStation = currentStation > 4 ? currentStation - 4 : 0;
  }

  if (WiFi.status() != WL_CONNECTED) {
    setStatus("No WiFi, start AP");
    startPortal();
    uiDirty = true;
    return;
  }

  stopAudio();
  streamTitle[0] = 0;
  metadataDirty = true;
  prepareCoverForStation(currentStation);
  setStatus("Connecting: %s", stations[currentStation].name.c_str());
  setEs8311Muted(false);
  audio.setVolume(volumeLevel);
  const bool ok = audio.connecttohost(stations[currentStation].url.c_str());
  if (ok) {
    playing = true;
    setLedMode(apActive ? LedMode::Ap : LedMode::Streaming);
    setStatus("Playing: %s", stations[currentStation].name.c_str());
  } else {
    playing = false;
    setStatus("Stream failed");
    errorUntilMs = millis() + 3500;
    setLedMode(LedMode::Error);
  }
  uiDirty = true;
}

void stopAudio() {
  if (audio.isRunning()) {
    audio.stopSong();
  }
  playing = false;
  setEs8311Muted(true);
  setLedMode(apActive ? LedMode::Ap : LedMode::Idle);
  uiDirty = true;
}

void togglePlay() {
  if (playing || audio.isRunning()) {
    stopAudio();
    setStatus("Stopped");
  } else {
    startStation(currentStation);
  }
}

void stationNext(int delta) {
  if (stationCount == 0) {
    return;
  }
  startStation(currentStation + delta);
}

void reloadSdStations() {
  const bool wasPlaying = playing;
  const String oldName = stationCount ? stations[currentStation].name : "";
  initSd();
  loadRuntimeConfig();
  updateSystemStatus(true);
  loadStations();
  if (oldName.length()) {
    for (uint8_t i = 0; i < stationCount; i++) {
      if (stations[i].name == oldName) {
        currentStation = i;
        break;
      }
    }
  }
  if (storageErrorActive()) {
    setStatus("%s", storageErrorText().c_str());
  } else {
    setStatus("Stations loaded: %u", stationCount);
  }
  if (wasPlaying) {
    startStation(currentStation);
  }
  uiDirty = true;
}

void setVolumeLevel(uint8_t newVolume) {
  volumeLevel = newVolume > MAX_VOLUME ? MAX_VOLUME : newVolume;
  prefs.putUChar("volume", volumeLevel);
  markConfigDirty();
  audio.setVolume(volumeLevel);
  setEs8311Muted(volumeLevel == 0);
  setStatus("Volume %u/%u", volumeLevel, MAX_VOLUME);
  uiDirty = true;
}

void audioInfoCallback(Audio::msg_t m) {
  if (!m.msg) {
    return;
  }
  DEBUG_PORT.printf("audio %s: %s\n", m.s ? m.s : "msg", m.msg);
  if (m.e == Audio::evt_streamtitle || m.e == Audio::evt_name) {
    strncpy(streamTitle, m.msg, sizeof(streamTitle) - 1);
    streamTitle[sizeof(streamTitle) - 1] = 0;
    metadataDirty = true;
  } else if (m.e == Audio::evt_eof) {
    audioEnded = true;
  }
}

bool inRect(uint16_t x, uint16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void handleMainTouch(uint16_t x, uint16_t y) {
  const int listY = 94;
  const int rowH = 27;
  if (y >= listY && y < listY + 5 * rowH) {
    const int idx = scrollStation + (y - listY) / rowH;
    if (idx < stationCount) {
      startStation(idx);
      return;
    }
  }
  if (inRect(x, y, 4, 234, 70, 36)) {
    stationNext(-1);
  } else if (inRect(x, y, 84, 234, 72, 36)) {
    togglePlay();
  } else if (inRect(x, y, 166, 234, 70, 36)) {
    stationNext(1);
  } else if (inRect(x, y, 4, 276, 70, 36)) {
    setVolumeLevel(volumeLevel > 0 ? volumeLevel - 1 : 0);
  } else if (inRect(x, y, 84, 276, 72, 36)) {
    uiScreen = UiScreen::Menu;
    invalidateUi(true);
  } else if (inRect(x, y, 166, 276, 70, 36)) {
    setVolumeLevel(volumeLevel >= MAX_VOLUME ? MAX_VOLUME : volumeLevel + 1);
  }
}

void handleMenuTouch(uint16_t x, uint16_t y) {
  if (inRect(x, y, 12, 72, 216, 32)) {
    if (apActive) {
      stopPortal();
    } else {
      startPortal();
    }
  } else if (inRect(x, y, 12, 112, 216, 32)) {
    reloadSdStations();
  } else if (inRect(x, y, 12, 152, 216, 32)) {
    reconnectWifi();
  } else if (inRect(x, y, 12, 192, 216, 32)) {
    cycleTheme();
  } else if (inRect(x, y, 12, 232, 216, 32)) {
    clearWifi();
  } else if (inRect(x, y, 12, 276, 216, 34)) {
    uiScreen = UiScreen::Main;
    invalidateUi(true);
  }
}

void handleTouch() {
  if (!touchReady || millis() - lastTouchMs < touchDebounceMs) {
    return;
  }

  uint16_t x = 0;
  uint16_t y = 0;
  if (!touch.read(x, y)) {
    return;
  }
  lastTouchMs = millis();
  logf("touch %u %u", x, y);

  if (uiScreen == UiScreen::Menu) {
    handleMenuTouch(x, y);
  } else {
    handleMainTouch(x, y);
  }
}

void printHelp() {
  DEBUG_PORT.println();
  DEBUG_PORT.println("LCDWiki Radio UART menu");
  DEBUG_PORT.println("  help/menu/?            - this menu");
  DEBUG_PORT.println("  status                 - full radio/audio status");
  DEBUG_PORT.println("  config                 - SD/runtime config summary");
  DEBUG_PORT.println("  sd/files               - SD state and root file list");
  DEBUG_PORT.println("  saveconfig             - write current runtime config to /radio.cfg");
  DEBUG_PORT.println("  reload                 - remount SD, reload /radio.cfg and /stations.csv");
  DEBUG_PORT.println("  ap / apoff             - start/stop setup AP");
  DEBUG_PORT.println("  reconnect              - reconnect WiFi from /radio.cfg");
  DEBUG_PORT.println("  clearwifi              - clear WiFi in /radio.cfg");
  DEBUG_PORT.println("  play/stop/next/prev    - playback");
  DEBUG_PORT.println("  station N              - play station index N");
  DEBUG_PORT.println("  list                   - list loaded stations");
  DEBUG_PORT.println("  vol N                  - set volume 0..21");
  DEBUG_PORT.println("  theme [name]           - cycle or set theme");
  DEBUG_PORT.println("  toneon/toneoff         - I2S test tone");
  DEBUG_PORT.println("  i2slog on/off          - I2S transfer trace");
  DEBUG_PORT.println("  codecdebug on/off      - ES8311 I2C trace");
  DEBUG_PORT.println("  codecsummary/codecdump - codec registers");
  DEBUG_PORT.println("  codec16/codec32        - force ES8311 word length");
  DEBUG_PORT.println("  amp0/amp1              - toggle amp enable GPIO");
  DEBUG_PORT.println("  reboot                 - restart ESP");
  DEBUG_PORT.println();
}

void printConfigSummary() {
  DEBUG_PORT.printf("Config file: %s loaded=%d missing=%d\n", CONFIG_FILE, configLoadedFromSd ? 1 : 0, bootConfigMissing ? 1 : 0);
  DEBUG_PORT.printf("WiFi: ssid=%s password=%s connected=%d ip=%s\n",
                    wifiSsid.length() ? wifiSsid.c_str() : "<empty>",
                    wifiPassword.length() ? "<set>" : "<empty>",
                    WiFi.status() == WL_CONNECTED ? 1 : 0,
                    WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-");
  DEBUG_PORT.printf("Runtime: theme=%s volume=%u autoplay=%d cover=%d station=%d/%u startup=%d apBoot=%d\n",
                    theme().id,
                    volumeLevel,
                    autoPlay ? 1 : 0,
                    coverDownload ? 1 : 0,
                    currentStation,
                    stationCount,
                    startupStation,
                    startApOnBoot ? 1 : 0);
  DEBUG_PORT.printf("Network timing: retry=%lus timeout=%lus statusRefresh=%lums touchDebounce=%ums\n",
                    wifiRetryMs / 1000UL,
                    wifiConnectTimeoutMs / 1000UL,
                    statusRefreshMs,
                    touchDebounceMs);
  DEBUG_PORT.printf("Clock: ntp=%d valid=%d time=%s server=%s tz=%s 24h=%d\n",
                    ntpEnabled ? 1 : 0,
                    clockValid ? 1 : 0,
                    clockText,
                    ntpServer.c_str(),
                    timezoneSpec.c_str(),
                    clock24h ? 1 : 0);
  DEBUG_PORT.printf("LED: enabled=%d brightness=%u boot=%s wifi=%s ap=%s stream=%s idle=%s error=%s pulse=%ums blink=%ums\n",
                    ledEnabled ? 1 : 0,
                    ledBrightness,
                    ledEffectId(ledBootEffect),
                    ledEffectId(ledWifiEffect),
                    ledEffectId(ledApEffect),
                    ledEffectId(ledStreamingEffect),
                    ledEffectId(ledIdleEffect),
                    ledEffectId(ledErrorEffect),
                    ledPulseMs,
                    ledBlinkMs);
  DEBUG_PORT.printf("Battery: enabled=%d valid=%d adc=%umV scaled=%umV percent=%u min=%u max=%u scale=%u\n",
                    batteryEnabled ? 1 : 0,
                    batteryValid ? 1 : 0,
                    batteryAdcMv,
                    batteryMv,
                    batteryPercent,
                    batteryMinMv,
                    batteryMaxMv,
                    batteryScalePermille);
}

void printSdSummary() {
  DEBUG_PORT.printf("SD: ready=%d error=%d state=%s\n", sdReady ? 1 : 0, storageErrorActive() ? 1 : 0, storageErrorText().c_str());
  if (!sdReady) {
    return;
  }
  DEBUG_PORT.printf("Card: %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  DEBUG_PORT.printf("%s: %s\n", CONFIG_FILE, SD_MMC.exists(CONFIG_FILE) ? "present" : "missing");
  DEBUG_PORT.printf("%s: %s loaded=%d count=%u invalid=%d\n",
                    STATIONS_FILE,
                    SD_MMC.exists(STATIONS_FILE) ? "present" : "missing",
                    stationsLoadedFromSd ? 1 : 0,
                    stationCount,
                    bootStationsInvalid ? 1 : 0);
  DEBUG_PORT.printf("%s: %s\n", BOOT_LOGO_FILE, SD_MMC.exists(BOOT_LOGO_FILE) ? "present" : "missing");
  DEBUG_PORT.println("Root files:");
  File root = SD_MMC.open("/");
  if (!root) {
    DEBUG_PORT.println("  <cannot open root>");
    return;
  }
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    DEBUG_PORT.printf("  %s %s %lu bytes\n",
                      entry.isDirectory() ? "dir " : "file",
                      entry.name(),
                      static_cast<unsigned long>(entry.size()));
    entry.close();
  }
  root.close();
}

void processSerialCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (!cmd.length()) {
    return;
  }
  if (cmd == "help" || cmd == "menu" || cmd == "?") {
    printHelp();
  } else if (cmd == "ap") {
    startPortal();
  } else if (cmd == "apoff") {
    stopPortal();
  } else if (cmd == "next") {
    stationNext(1);
  } else if (cmd == "prev") {
    stationNext(-1);
  } else if (cmd == "play") {
    startStation(currentStation);
  } else if (cmd == "stop") {
    stopAudio();
  } else if (cmd == "reload") {
    reloadSdStations();
  } else if (cmd == "reconnect") {
    reconnectWifi();
  } else if (cmd == "config") {
    printConfigSummary();
  } else if (cmd == "sd" || cmd == "files") {
    printSdSummary();
  } else if (cmd == "saveconfig") {
    const bool saved = saveRuntimeConfig(true);
    DEBUG_PORT.println(saved ? "Config saved to /radio.cfg" : "Config save failed");
  } else if (cmd == "theme") {
    cycleTheme();
  } else if (cmd.startsWith("theme ")) {
    const int idx = themeIndexById(cmd.substring(6));
    if (idx >= 0) {
      themeIndex = idx;
      markConfigDirty();
      setStatus("Theme: %s", theme().label);
      invalidateUi(true);
    } else {
      DEBUG_PORT.print("Themes:");
      for (uint8_t i = 0; i < THEME_COUNT; i++) {
        DEBUG_PORT.print(' ');
        DEBUG_PORT.print(THEMES[i].id);
      }
      DEBUG_PORT.println();
    }
  } else if (cmd == "clearwifi") {
    clearWifi();
  } else if (cmd.startsWith("vol ")) {
    setVolumeLevel(cmd.substring(4).toInt());
  } else if (cmd.startsWith("station ")) {
    startStation(cmd.substring(8).toInt());
  } else if (cmd == "list") {
    for (uint8_t i = 0; i < stationCount; i++) {
      DEBUG_PORT.printf("%u: %s -> %s\n", i, stations[i].name.c_str(), stations[i].url.c_str());
    }
  } else if (cmd == "status") {
    printStatus();
  } else if (cmd == "unmute") {
    setEs8311Muted(false);
    setStatus("Codec unmuted");
  } else if (cmd == "toneon") {
    g_i2sForceTone = true;
    g_i2sTonePhase = 0;
    if (!playing && WiFi.status() == WL_CONNECTED) {
      startStation(currentStation);
    }
    setStatus("I2S test tone ON");
  } else if (cmd == "toneoff") {
    g_i2sForceTone = false;
    setStatus("I2S test tone OFF");
  } else if (cmd == "i2slog on") {
    i2sTrace = true;
    lastI2sTraceMs = millis();
    lastI2sTraceFrames = g_i2sFrameCount;
    DEBUG_PORT.println("I2S trace ON");
  } else if (cmd == "i2slog off") {
    i2sTrace = false;
    DEBUG_PORT.println("I2S trace OFF");
  } else if (cmd == "codecdebug on") {
    codecTrace = true;
    DEBUG_PORT.println("ES8311 I2C trace ON");
  } else if (cmd == "codecdebug off") {
    codecTrace = false;
    DEBUG_PORT.println("ES8311 I2C trace OFF");
  } else if (cmd == "codecsummary") {
    printEs8311Summary();
  } else if (cmd == "codec16") {
    setEs8311Bits(16);
  } else if (cmd == "codec32") {
    setEs8311Bits(32);
  } else if (cmd == "amp0") {
    digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, LOW);
    setStatus("Audio enable GPIO1 LOW");
  } else if (cmd == "amp1") {
    digitalWrite(LCDWIKI_ES3C28P_AUDIO_EN, HIGH);
    setStatus("Audio enable GPIO1 HIGH");
  } else if (cmd == "codecdump") {
    dumpEs8311();
  } else if (cmd == "reboot") {
    DEBUG_PORT.println("Rebooting");
    delay(100);
    ESP.restart();
  } else {
    printHelp();
  }
}

void handleSerial() {
  while (DEBUG_PORT.available()) {
    const char c = DEBUG_PORT.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      processSerialCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 120) {
      serialLine += c;
    }
  }
}

void serviceNetwork() {
  if (portalStarted) {
    server.handleClient();
  }
  if (apActive) {
    dnsServer.processNextRequest();
  }

  if (connectPending) {
    connectPending = false;
    if (!connectWifi(true)) {
      startPortal();
    }
  }

  if (WiFi.status() != WL_CONNECTED && !apActive && millis() - lastWifiRetryMs > wifiRetryMs) {
    lastWifiRetryMs = millis();
    if (!connectWifi(false)) {
      startPortal();
    }
  }
}

void printStatus() {
  DEBUG_PORT.printf("WiFi: %s", WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PORT.printf(" %s RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  DEBUG_PORT.println();
  DEBUG_PORT.printf("AP: %s, SD: %s, storageError=%d (%s), station: %d/%u loaded=%d\n",
                    apActive ? "on" : "off",
                    sdReady ? "ok" : "missing",
                    storageErrorActive() ? 1 : 0,
                    storageErrorText().c_str(),
                    currentStation,
                    stationCount,
                    stationsLoadedFromSd ? 1 : 0);
  DEBUG_PORT.printf("Config source: %s loaded=%d ssid=%s password=%s\n",
                    CONFIG_FILE,
                    configLoadedFromSd ? 1 : 0,
                    wifiSsid.length() ? wifiSsid.c_str() : "<empty>",
                    wifiPassword.length() ? "<set>" : "<empty>");
  DEBUG_PORT.printf("Config: theme=%s volume=%u autoplay=%d cover=%d startup=%d apBoot=%d retry=%lus timeout=%lus touch=%ums\n",
                    theme().id,
                    volumeLevel,
                    autoPlay ? 1 : 0,
                    coverDownload ? 1 : 0,
                    startupStation,
                    startApOnBoot ? 1 : 0,
                    wifiRetryMs / 1000UL,
                    wifiConnectTimeoutMs / 1000UL,
                    touchDebounceMs);
  DEBUG_PORT.printf("LED: enabled=%d brightness=%u boot=%s wifi=%s ap=%s stream=%s idle=%s error=%s pulse=%ums blink=%ums\n",
                    ledEnabled ? 1 : 0,
                    ledBrightness,
                    ledEffectId(ledBootEffect),
                    ledEffectId(ledWifiEffect),
                    ledEffectId(ledApEffect),
                    ledEffectId(ledStreamingEffect),
                    ledEffectId(ledIdleEffect),
                    ledEffectId(ledErrorEffect),
                    ledPulseMs,
                    ledBlinkMs);
  DEBUG_PORT.printf("Power: battery=%s adc=%umV scaled=%umV percent=%u%% scale=%u min=%u max=%u wifiRSSI=%d bars=%d\n",
                    batteryValid ? "ok" : "unknown",
                    batteryAdcMv,
                    batteryMv,
                    batteryPercent,
                    batteryScalePermille,
                    batteryMinMv,
                    batteryMaxMv,
                    wifiRssiDbm,
                    wifiSignalBars);
  DEBUG_PORT.printf("Clock: %s valid=%d ntp=%d configured=%d server=%s tz=%s 24h=%d\n",
                    clockText,
                    clockValid ? 1 : 0,
                    ntpEnabled ? 1 : 0,
                    ntpConfigured ? 1 : 0,
                    ntpServer.c_str(),
                    timezoneSpec.c_str(),
                    clock24h ? 1 : 0);
  DEBUG_PORT.printf("Audio: pinout=%d playing=%d running=%d codec=%s sample=%lu bits=%u channels=%u bitrate=%lu volume=%u VU=%u\n",
                    audioPinoutReady,
                    playing,
                    audio.isRunning(),
                    audio.getCodecname(),
                    audio.getSampleRate(),
                    audio.getBitsPerSample(),
                    audio.getChannels(),
                    audio.getBitRate(),
                    volumeLevel,
                    audio.getVUlevel());
  DEBUG_PORT.printf("Buffer: filled=%lu free=%lu total=%lu\n",
                    audio.inBufferFilled(),
                    audio.inBufferFree(),
                    audio.getInBufferSize());
  DEBUG_PORT.printf("I2S: callbacks=%lu frames=%lu peak=%lu ageMs=%lu forcedTone=%d taskWatermark=%lu\n",
                    g_i2sCallbackCount,
                    g_i2sFrameCount,
                    g_i2sPeak,
                    millis() - g_i2sLastMs,
                    g_i2sForceTone ? 1 : 0,
                    audio.getHighWatermark());
  printEs8311Summary();
  DEBUG_PORT.printf("Status: %s\n", audioStatus);
  if (streamTitle[0]) {
    DEBUG_PORT.printf("Title: %s\n", streamTitle);
  }
}

void traceI2sIfNeeded() {
  if (!i2sTrace) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastI2sTraceMs < 1000) {
    return;
  }

  const uint32_t frames = g_i2sFrameCount;
  const uint32_t deltaFrames = frames - lastI2sTraceFrames;
  const uint32_t elapsed = now - lastI2sTraceMs;
  const uint32_t fps = elapsed ? (deltaFrames * 1000UL) / elapsed : 0;
  DEBUG_PORT.printf("I2S trace: fps=%lu callbacks=%lu totalFrames=%lu peak=%lu ageMs=%lu tone=%d codec=%s running=%d buffer=%lu\n",
                    fps,
                    g_i2sCallbackCount,
                    frames,
                    g_i2sPeak,
                    now - g_i2sLastMs,
                    g_i2sForceTone ? 1 : 0,
                    audio.getCodecname(),
                    audio.isRunning() ? 1 : 0,
                    audio.inBufferFilled());

  lastI2sTraceMs = now;
  lastI2sTraceFrames = frames;
}

}  // namespace

void setupRadio() {
  initDebug();
  initPixel();
  initApName();
  setLedMode(LedMode::Boot);

  prefs.begin("lcd-radio", false);

  displayReady = display.begin();
  if (displayReady) {
    drawBootProgress("Display ok", 10);
  } else {
    logf("Display init failed");
  }

  touchReady = touch.begin();
  logf("Touch %s", touchReady ? "ok" : "not detected");
  drawBootProgress(touchReady ? "Touch ok" : "Touch not detected", 22);

  drawBootProgress("Mounting SD", 35);
  initSd();
  drawBootProgress(storageErrorActive() ? storageErrorText().c_str() : "SD mounted", 45, storageErrorActive());

  drawBootProgress("Loading /radio.cfg", 55, bootSdMissing);
  loadRuntimeConfig();
  drawBootProgress(configLoadedFromSd ? "Config loaded from SD" : storageErrorText().c_str(), 62, !configLoadedFromSd);

  initBatteryMonitor();
  updateSystemStatus(true);

  drawBootProgress("Loading /stations.csv", 72, bootSdMissing || bootConfigMissing);
  loadStations();
  String stationsLine = stationsLoadedFromSd ? String("Stations loaded: ") + String(stationCount) : storageErrorText();
  drawBootProgress(stationsLine.c_str(), 78, !stationsLoadedFromSd);

  drawBootProgress("Audio init", 86, storageErrorActive());
  initAudio();

  if (startApOnBoot || storageErrorActive()) {
    startPortal();
  }

  bool wifiOk = false;
  if (configLoadedFromSd && wifiSsid.length()) {
    drawBootProgress("Connecting WiFi", 92, storageErrorActive());
    wifiOk = connectWifi(true);
  } else if (!storageErrorActive()) {
    setStatus("No WiFi in /radio.cfg");
  }
  if (!wifiOk) {
    startPortal();
  }

  if (autoPlay && wifiOk && stationCount > 0) {
    startStation(currentStation);
  } else if (storageErrorActive()) {
    setStatus("%s", storageErrorText().c_str());
  } else if (wifiOk) {
    setStatus("Ready");
  }

  drawBootProgress(storageErrorActive() ? storageErrorText().c_str() : "Ready", 100, storageErrorActive());
  delay(200);
  invalidateUi(true);
  drawUi();
}

void loopRadio() {
  audio.loop();

  serviceNetwork();
  serviceConfigSave();
  updateSystemStatus(false);
  updateClock(false);
  handleSerial();
  traceI2sIfNeeded();
  handleTouch();
  updateLed();

  if (metadataDirty) {
    metadataDirty = false;
    uiDirty = true;
  }

  if (audioEnded) {
    audioEnded = false;
    playing = false;
    setStatus("Stream ended");
    if (WiFi.status() == WL_CONNECTED) {
      startStation(currentStation);
    }
  }

  if (playing && !audio.isRunning() && WiFi.status() == WL_CONNECTED && millis() - lastWifiRetryMs > wifiRetryMs) {
    lastWifiRetryMs = millis();
    setStatus("Reconnecting stream");
    startStation(currentStation);
  }

  drawUi();
  delay(1);
}
