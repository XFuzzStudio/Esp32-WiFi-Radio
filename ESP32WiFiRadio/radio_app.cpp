#include "radio_app.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Audio.h>
#include <DNSServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <lvgl.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <stdarg.h>
#include <time.h>

#include "../shared/lcdwiki_es3c28p/arduino_gfx_display.h"
#include "../shared/lcdwiki_es3c28p/lcdwiki_es3c28p_config.h"
#include "../shared/lcdwiki_es3c28p/touch_ft6336.h"
#include "../shared/esp32_bin_loader_return.h"
#include "../shared/radio_remote_protocol.h"

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

static constexpr uint8_t MAX_STATIONS = 50;
static constexpr char APP_NAME[] = "ESP32 WiFi Radio";
static constexpr char APP_VERSION[] = "1.1";
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
static constexpr uint16_t REMOTE_STATUS_MS = 1000;
static constexpr uint16_t REMOTE_PAIR_ADVERT_MS = 1000;
static constexpr uint32_t REMOTE_PAIR_WINDOW_MS = 120000UL;
static constexpr uint32_t REMOTE_LINK_TIMEOUT_MS = 10000UL;
static constexpr uint32_t SCREEN_SAVER_MS = 45000UL;
static constexpr uint32_t SCREEN_SAVER_REFRESH_MS = 60000UL;
static constexpr uint32_t TRACE_AUTO_OFF_MS = 60000UL;
static constexpr char DEFAULT_NTP_SERVER[] = "pool.ntp.org";
static constexpr char DEFAULT_TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
static constexpr uint8_t LVGL_BUFFER_ROWS = 36;
static constexpr uint16_t LVGL_SCREEN_W = LCDWIKI_ES3C28P_SCREEN_WIDTH;
static constexpr uint16_t LVGL_SCREEN_H = LCDWIKI_ES3C28P_SCREEN_HEIGHT;
static constexpr byte DNS_PORT = 53;
static constexpr int8_t DEBUG_UART_RX = 44;
static constexpr int8_t DEBUG_UART_TX = 43;
static constexpr char APP_DATA_DIR[] = "/apps_data/ESP32WiFiRadio";
static constexpr char STATIONS_FILE[] = "/apps_data/ESP32WiFiRadio/stations.csv";
static constexpr char CONFIG_FILE[] = "/apps_data/ESP32WiFiRadio/radio.cfg";
static constexpr char DEFAULT_AP_PASSWORD[] = "radio1234";

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_NETMASK(255, 255, 255, 0);
static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

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
static constexpr uint16_t C_APP_BG = 0xF7BE;
static constexpr uint16_t C_SURFACE = 0xFFFF;
static constexpr uint16_t C_SURFACE_2 = 0xEF7D;
static constexpr uint16_t C_SURFACE_3 = 0xC618;
static constexpr uint16_t C_TEXT_MAIN = 0x2104;
static constexpr uint16_t C_TEXT_MUTED = 0x8410;
static constexpr uint16_t C_APP_ACCENT = 0x24BE;
static constexpr uint16_t C_APP_ACCENT_2 = 0xBEFF;

struct Station {
  String name;
  String url;
};

struct DisplayPalette {
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

enum class LvglAction : intptr_t {
  Station0 = 1,
  Station1,
  Station2,
  Station3,
  Station4,
  Prev,
  PlayStop,
  Next,
  VolDown,
  Menu,
  VolUp,
  ApToggle,
  ReloadSd,
  ReconnectWifi,
  DarkMode,
  StationUp,
  StationDown,
  PairRemote,
  ClearWifi,
  Back,
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
bool darkMode = true;

UiScreen uiScreen = UiScreen::Main;
LedMode ledMode = LedMode::Boot;

bool displayReady = false;
bool touchReady = false;
bool sdReady = false;
bool sdDataReady = false;
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
bool configDirty = false;
bool ledEnabled = true;
bool startApOnBoot = false;
bool batteryEnabled = true;
bool batteryValid = false;
bool lvglReady = false;
bool lvglUiBuilt = false;
bool ntpEnabled = true;
bool ntpConfigured = false;
bool clockValid = false;
bool clock24h = true;
bool remoteEnabled = true;
bool remoteReady = false;
bool remotePeerValid = false;
bool remotePairingMode = false;
bool remotePilotBatteryValid = false;
bool remoteUiLinkActive = false;
bool otaInProgress = false;
bool screenSaverActive = false;

uint32_t lastTouchMs = 0;
uint32_t lastWifiRetryMs = 0;
uint32_t lastLedMs = 0;
uint32_t lastI2sTraceMs = 0;
uint32_t lastI2sTraceFrames = 0;
uint32_t errorUntilMs = 0;
uint32_t configDirtyMs = 0;
uint32_t lastSystemStatusMs = 0;
uint32_t lastLvglTickMs = 0;
uint32_t lastClockMs = 0;
uint32_t lastNtpConfigMs = 0;
uint32_t remotePairUntilMs = 0;
uint32_t remoteLastPairAdvertMs = 0;
uint32_t remoteLastStatusMs = 0;
uint32_t remoteLastSeenMs = 0;
uint32_t cpuStatsWindowMs = 0;
uint32_t cpuStatsBusyUs = 0;
uint32_t cpuLoopMaxUs = 0;
uint32_t lastUiActivityMs = 0;
uint32_t lastScreenSaverDrawMs = 0;
uint32_t i2sTraceUntilMs = 0;
uint32_t codecTraceUntilMs = 0;
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
uint8_t remotePilotBatteryPercent = 0;
uint8_t otaProgressPercent = 0;
uint16_t remoteSeq = 0;
uint16_t cpuMainLoadPermille = 0;
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
String apPassword = DEFAULT_AP_PASSWORD;
char streamTitle[128] = {0};
char audioStatus[96] = "Ready";
char clockText[16] = "--:--";
char otaStatusText[80] = "OTA idle";
String serialLine;
String ntpServer = DEFAULT_NTP_SERVER;
String timezoneSpec = DEFAULT_TIMEZONE;
String remotePairedMac;
uint8_t remotePeerMac[6] = {0};
uint8_t remoteRxMac[6] = {0};
RadioRemotePacket remoteRxPacket;
volatile bool remoteRxPending = false;
portMUX_TYPE remoteRxMux = portMUX_INITIALIZER_UNLOCKED;

lv_display_t *lvglDisplay = nullptr;
lv_indev_t *lvglInput = nullptr;
lv_obj_t *lvglRoot = nullptr;
lv_obj_t *lvTitle = nullptr;
lv_obj_t *lvClock = nullptr;
lv_obj_t *lvVolume = nullptr;
lv_obj_t *lvWifi = nullptr;
lv_obj_t *lvBattery = nullptr;
lv_obj_t *lvWifiBars[4] = {nullptr};
lv_obj_t *lvBatteryShell = nullptr;
lv_obj_t *lvBatteryFill = nullptr;
lv_obj_t *lvBatteryTip = nullptr;
lv_obj_t *lvBatteryCharge = nullptr;
lv_obj_t *lvSd = nullptr;
lv_obj_t *lvPlayState = nullptr;
lv_obj_t *lvRemote = nullptr;
lv_obj_t *lvNetwork = nullptr;
lv_obj_t *lvNowPlaying = nullptr;
lv_obj_t *lvStation = nullptr;
lv_obj_t *lvStationButtons[5] = {nullptr};
lv_obj_t *lvStationLabels[5] = {nullptr};
lv_obj_t *lvButtonPrev = nullptr;
lv_obj_t *lvButtonPlay = nullptr;
lv_obj_t *lvButtonNext = nullptr;
lv_obj_t *lvButtonVolDown = nullptr;
lv_obj_t *lvButtonMenu = nullptr;
lv_obj_t *lvButtonVolUp = nullptr;
lv_obj_t *lvMenuStatus = nullptr;
lv_obj_t *lvMenuDarkMode = nullptr;
lv_obj_t *lvStationScrollUp = nullptr;
lv_obj_t *lvStationScrollDown = nullptr;
lv_obj_t *lvMenuAp = nullptr;
lv_obj_t *lvMenuRemote = nullptr;
lv_obj_t *lvSaverClock = nullptr;
lv_obj_t *lvSaverStation = nullptr;
lv_obj_t *lvSaverStatus = nullptr;
lv_obj_t *lvSaverRadioBattery = nullptr;
lv_obj_t *lvSaverPilotBattery = nullptr;
lv_obj_t *lvSaverLink = nullptr;
lv_obj_t *lvBootTitle = nullptr;
lv_obj_t *lvBootLine = nullptr;
lv_obj_t *lvBootPercent = nullptr;
lv_obj_t *lvBootBar = nullptr;
lv_obj_t *lvBootScan = nullptr;
lv_obj_t *lvBootDots[3] = {nullptr};
bool lvglBootBuilt = false;

static uint16_t lvglDrawBuffer[LVGL_SCREEN_W * LVGL_BUFFER_ROWS];

const char DEFAULT_STATIONS[] =
  "# name|stream_url\n"
  "Groove Salad|http://ice5.somafm.com/groovesalad-128-mp3\n"
  "Drone Zone|http://ice5.somafm.com/dronezone-128-mp3\n"
  "Deep Space One|http://ice5.somafm.com/deepspaceone-128-mp3\n";

const char DEFAULT_CONFIG[] =
  "# ESP32 WiFi Radio config\n"
  "# WiFi values are plain text on SD. Leave wifi_ssid empty to keep NVS/AP settings.\n"
  "dark_mode=1\n"
  "ap_password=radio1234\n"
  "volume=13\n"
  "autoplay=1\n"
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
  "remote_enabled=1\n"
  "remote_paired_mac=\n"
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
  "# battery_scale_permille default 2000 means ADC voltage times 2.000\n"
  "# remote_paired_mac is written by the ESP32-C6 pilot pairing flow\n";

const DisplayPalette DISPLAY_PALETTES[] = {
  {"light", "Light", 0xFFFF, 0xEF7D, 0xFFFF, 0xEF7D, C_APP_ACCENT, C_APP_ACCENT_2, 0, 54, 70},
  {"dark", "Dark", 0x18E3, 0x0841, 0x2104, 0x3186, C_CYAN, C_GREEN, 0, 54, 70},
};

static constexpr uint8_t DISPLAY_PALETTE_COUNT = sizeof(DISPLAY_PALETTES) / sizeof(DISPLAY_PALETTES[0]);

void logf(const char *fmt, ...);
void setStatus(const char *fmt, ...);
void invalidateUi(bool fullRedraw = false);
const DisplayPalette &palette();
int displayModeIndexById(String id);
void toggleDarkMode();
bool validApPassword(const String &value);
void setApPassword(String value);
String apInfoText();
void scrollStations(int delta);
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
String macToString(const uint8_t mac[6]);
bool parseMacAddress(String value, uint8_t mac[6]);
bool macEquals(const uint8_t a[6], const uint8_t b[6]);
bool macIsEmpty(const uint8_t mac[6]);
uint8_t wifiPrimaryChannel();
void initRemoteLink();
void serviceRemoteLink();
void remoteOnReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len);
bool remoteAddPeer(const uint8_t mac[6]);
void remoteLoadPeerFromConfig();
void remoteBeginPairing();
void remoteStopPairing();
void remoteForgetPeer();
void remoteStorePeer(const uint8_t mac[6]);
void remoteHandlePacket(const RadioRemotePacket &packet, const uint8_t mac[6]);
void remoteFillStatusPacket(RadioRemotePacket &packet, uint8_t type, uint8_t command = RADIO_REMOTE_CMD_NONE);
bool remoteSendPacket(const uint8_t dest[6], uint8_t type, uint8_t command = RADIO_REMOTE_CMD_NONE);
void remoteSendPairAdvert(bool force = false);
void remoteSendStatus(bool force = false);
String remoteStatusText();
bool remoteLinkActive();
void updateCpuStats(uint32_t loopStartUs);
bool initLvglUi();
void serviceLvgl();
void lvglBuildBootScreen();
void lvglShowBootStep(const char *line, uint8_t percent);
void lvglBootPump(uint16_t durationMs);
void lvglFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap);
void lvglTouchRead(lv_indev_t *indev, lv_indev_data_t *data);
lv_color_t lvColor565(uint16_t color);
void lvStylePanel(lv_obj_t *obj, uint16_t bg, uint8_t radius = 0);
lv_obj_t *lvMakeLabel(lv_obj_t *parent, const char *text, int16_t x, int16_t y, int16_t w, uint8_t fontSize, uint16_t color);
lv_obj_t *lvMakeButton(lv_obj_t *parent, const String &text, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg, uint16_t fg, LvglAction action);
void lvSetButtonText(lv_obj_t *button, const String &text);
void lvglButtonEvent(lv_event_t *event);
lv_obj_t *lvMakeChip(lv_obj_t *parent, const String &text, int16_t x, int16_t y, int16_t w, uint16_t bg, uint16_t fg);
void lvBuildWifiIcon(lv_obj_t *parent, int16_t x, int16_t y);
void lvBuildBatteryIcon(lv_obj_t *parent, int16_t x, int16_t y);
bool batteryChargingLikely();
void lvSyncWifiIcon();
void lvSyncBatteryIcon();
void lvglBuildUi(bool fullRedraw);
void lvglBuildHeader(lv_obj_t *parent, const char *title);
void lvglBuildMain();
void lvglBuildMenu();
void lvglSyncUi();
void lvglSyncHeader();
void lvglSyncMain();
void lvglSyncMenu();
void registerUiActivity();
bool wakeScreenSaver();
void serviceScreenSaver();
void serviceTraceTimeouts();
void lvglBuildScreenSaver();
void lvglSyncScreenSaver();
void drawScreenSaver();
void setLedMode(LedMode mode);
void updateLed();
void initPixel();
void initDebug();
void preferBinLoaderOnNextReset();
void initApName();
void drawBoot(const char *line);
void drawOtaProgress(const char *line, uint8_t percent, bool error = false);
void drawUi();
void drawMain(bool fullRedraw);
void drawMenu(bool fullRedraw);
void drawHeader(const char *title);
void drawWifiBars(int16_t x, int16_t y);
void drawBatteryStatus(int16_t x, int16_t y);
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
void saveRuntimeConfig(bool immediate = false);
void markConfigDirty();
void serviceConfigSave();
void loadStations();
void loadStationsFromText(const String &text);
void addStationLine(String line);
void addDefaultStations();
bool saveStationsText(const String &text);
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
void handleRemotePair();
void handleRemoteForget();
void handleOtaPage();
void handleOtaDone();
void handleOtaUpload();
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

const DisplayPalette &palette() {
  return DISPLAY_PALETTES[darkMode ? 1 : 0];
}

uint16_t uiBgColor() { return darkMode ? C_BLACK : C_APP_BG; }
uint16_t uiSurfaceColor() { return darkMode ? 0x1082 : C_SURFACE; }
uint16_t uiSurface2Color() { return darkMode ? 0x2104 : C_SURFACE_2; }
uint16_t uiSurface3Color() { return darkMode ? 0x4208 : C_SURFACE_3; }
uint16_t uiTextColor() { return darkMode ? C_WHITE : C_TEXT_MAIN; }
uint16_t uiMutedColor() { return darkMode ? 0x9CF3 : C_TEXT_MUTED; }
uint16_t uiAccentColor() { return darkMode ? C_CYAN : C_APP_ACCENT; }

int displayModeIndexById(String id) {
  id.trim();
  id.toLowerCase();
  if (id == "1" || id == "true" || id == "yes" || id == "on" || id == "dark") {
    return 1;
  }
  if (id == "0" || id == "false" || id == "no" || id == "off" || id == "light") {
    return 0;
  }
  for (uint8_t i = 0; i < DISPLAY_PALETTE_COUNT; i++) {
    if (id == DISPLAY_PALETTES[i].id) {
      return i;
    }
  }
  return -1;
}

void toggleDarkMode() {
  darkMode = !darkMode;
  prefs.putBool("darkMode", darkMode);
  markConfigDirty();
  setStatus("Dark mode: %s", darkMode ? "on" : "off");
  invalidateUi(true);
}

bool validApPassword(const String &value) {
  return value.length() >= 8 && value.length() <= 63;
}

void setApPassword(String value) {
  value.trim();
  if (!validApPassword(value)) {
    value = DEFAULT_AP_PASSWORD;
  }
  apPassword = value;
  prefs.putString("apPass", apPassword);
}

String apInfoText() {
  String text = "AP ";
  text += AP_IP.toString();
  text += " pass ";
  text += apPassword;
  return text;
}

void scrollStations(int delta) {
  const int maxScroll = stationCount > 5 ? stationCount - 5 : 0;
  scrollStation = constrain(scrollStation + delta, 0, maxScroll);
  uiDirty = true;
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
  prefs.putBool("darkMode", darkMode);
  prefs.putString("apPass", apPassword);
  prefs.putUChar("volume", volumeLevel);
  prefs.putBool("autoplay", autoPlay);
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
  prefs.putBool("remoteEn", remoteEnabled);
  prefs.putString("remoteMac", remotePairedMac);
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

String macToString(const uint8_t mac[6]) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

bool parseMacAddress(String value, uint8_t mac[6]) {
  value.trim();
  if (!value.length()) {
    memset(mac, 0, 6);
    return false;
  }
  unsigned int parts[6] = {0};
  if (sscanf(value.c_str(), "%x:%x:%x:%x:%x:%x",
             &parts[0], &parts[1], &parts[2], &parts[3], &parts[4], &parts[5]) != 6) {
    memset(mac, 0, 6);
    return false;
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (parts[i] > 255) {
      memset(mac, 0, 6);
      return false;
    }
    mac[i] = static_cast<uint8_t>(parts[i]);
  }
  return !macIsEmpty(mac);
}

bool macEquals(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

bool macIsEmpty(const uint8_t mac[6]) {
  const uint8_t empty[6] = {0};
  return memcmp(mac, empty, 6) == 0;
}

uint8_t wifiPrimaryChannel() {
  uint8_t channel = 0;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&channel, &second) != ESP_OK || channel == 0) {
    return 1;
  }
  return channel;
}

void remoteLoadPeerFromConfig() {
  remotePeerValid = parseMacAddress(remotePairedMac, remotePeerMac);
  if (!remotePeerValid) {
    remotePairedMac = "";
  }
}

bool remoteAddPeer(const uint8_t mac[6]) {
  if (!remoteReady || macIsEmpty(mac)) {
    return false;
  }
  if (esp_now_is_peer_exist(mac)) {
    return true;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  const esp_err_t err = esp_now_add_peer(&peer);
  if (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST) {
    return true;
  }
  logf("ESP-NOW add peer failed %s err=0x%X", macToString(mac).c_str(), static_cast<unsigned>(err));
  return false;
}

void initRemoteLink() {
  if (!remoteEnabled) {
    return;
  }
  if (remoteReady) {
    if (remotePeerValid) {
      remoteAddPeer(remotePeerMac);
    }
    return;
  }

  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  const esp_err_t err = esp_now_init();
  if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
    logf("ESP-NOW init failed err=0x%X", static_cast<unsigned>(err));
    return;
  }

  remoteReady = true;
  esp_now_register_recv_cb(remoteOnReceive);
  remoteAddPeer(ESPNOW_BROADCAST_MAC);
  if (remotePeerValid) {
    remoteAddPeer(remotePeerMac);
  }
  logf("ESP-NOW remote ready channel=%u peer=%s",
       wifiPrimaryChannel(),
       remotePeerValid ? remotePairedMac.c_str() : "<none>");
}

bool remoteLinkActive() {
  return remotePeerValid && remoteLastSeenMs != 0 && millis() - remoteLastSeenMs < REMOTE_LINK_TIMEOUT_MS;
}

String remoteStatusText() {
  if (!remoteEnabled) {
    return "disabled";
  }
  if (remotePairingMode) {
    const uint32_t left = remotePairUntilMs > millis() ? (remotePairUntilMs - millis()) / 1000UL : 0;
    return String("pairing ") + String(left) + "s";
  }
  if (!remotePeerValid) {
    return "not paired";
  }
  String text = remotePairedMac;
  text += remoteLinkActive() ? " online" : " offline";
  if (remotePilotBatteryValid) {
    text += " pilot ";
    text += remotePilotBatteryPercent;
    text += "%";
  }
  return text;
}

uint8_t remoteLinkBars() {
  if (!remotePeerValid || remoteLastSeenMs == 0) {
    return 0;
  }
  const uint32_t age = millis() - remoteLastSeenMs;
  if (age >= REMOTE_LINK_TIMEOUT_MS) {
    return 0;
  }
  if (age < 3000UL) {
    return 3;
  }
  if (age < 7000UL) {
    return 2;
  }
  return 1;
}

void remoteFillStatusPacket(RadioRemotePacket &packet, uint8_t type, uint8_t command) {
  radioRemoteClearPacket(packet);
  packet.type = type;
  packet.seq = ++remoteSeq;
  packet.stationIndex = currentStation < 0 ? 0 : static_cast<uint8_t>(currentStation);
  packet.stationCount = stationCount;
  packet.volume = volumeLevel;
  packet.radioBatteryPercent = batteryPercent;
  packet.radioBatteryValid = batteryValid ? 1 : 0;
  packet.pilotBatteryPercent = remotePilotBatteryPercent;
  packet.pilotBatteryValid = remotePilotBatteryValid ? 1 : 0;
  packet.wifiBars = wifiSignalBars;
  packet.wifiRssi = wifiRssiDbm;
  packet.command = command;
  packet.channel = wifiPrimaryChannel();
  WiFi.macAddress(packet.radioMac);
  if (remotePeerValid) {
    memcpy(packet.pilotMac, remotePeerMac, 6);
  }
  if (WiFi.status() == WL_CONNECTED) {
    packet.flags |= RADIO_REMOTE_FLAG_WIFI_CONNECTED;
  }
  if (playing) {
    packet.flags |= RADIO_REMOTE_FLAG_PLAYING;
  }
  if (sdReady) {
    packet.flags |= RADIO_REMOTE_FLAG_SD_READY;
  }
  if (apActive) {
    packet.flags |= RADIO_REMOTE_FLAG_AP_ACTIVE;
  }
  if (remotePairingMode) {
    packet.flags |= RADIO_REMOTE_FLAG_PAIRING;
  }
  if (remoteLinkActive()) {
    packet.flags |= RADIO_REMOTE_FLAG_REMOTE_LINK;
  }

  const String stationName = stationCount ? stations[currentStation].name : String("No stations");
  const String title = streamTitle[0] ? String(streamTitle) : String(audioStatus);
  snprintf(packet.clock, sizeof(packet.clock), "%s", clockValid ? clockText : "--:--");
  snprintf(packet.station, sizeof(packet.station), "%s", stationName.c_str());
  snprintf(packet.title, sizeof(packet.title), "%s", title.c_str());
}

bool remoteSendPacket(const uint8_t dest[6], uint8_t type, uint8_t command) {
  if (!remoteEnabled || !remoteReady) {
    return false;
  }
  if (!remoteAddPeer(dest)) {
    return false;
  }
  RadioRemotePacket packet;
  remoteFillStatusPacket(packet, type, command);
  const esp_err_t err = esp_now_send(dest, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  return err == ESP_OK;
}

void remoteSendPairAdvert(bool force) {
  if (!remotePairingMode || !remoteReady) {
    return;
  }
  const uint32_t now = millis();
  if (!force && now - remoteLastPairAdvertMs < REMOTE_PAIR_ADVERT_MS) {
    return;
  }
  remoteLastPairAdvertMs = now;
  remoteSendPacket(ESPNOW_BROADCAST_MAC, RADIO_REMOTE_PAIR_ADVERT);
}

void remoteSendStatus(bool force) {
  if (!remotePeerValid || !remoteReady) {
    return;
  }
  const uint32_t now = millis();
  if (!force && now - remoteLastStatusMs < REMOTE_STATUS_MS) {
    return;
  }
  remoteLastStatusMs = now;
  remoteSendPacket(remotePeerMac, RADIO_REMOTE_STATUS);
}

void remoteBeginPairing() {
  if (!remoteEnabled) {
    setStatus("Pilot pairing disabled");
    return;
  }
  initRemoteLink();
  if (!remoteReady) {
    setStatus("ESP-NOW remote not ready");
    errorUntilMs = millis() + 3500;
    return;
  }
  remotePairingMode = true;
  remotePairUntilMs = millis() + REMOTE_PAIR_WINDOW_MS;
  remoteLastPairAdvertMs = 0;
  remoteSendPairAdvert(true);
  setStatus("Pilot pairing 120s");
  invalidateUi(true);
}

void remoteStopPairing() {
  if (!remotePairingMode) {
    return;
  }
  remotePairingMode = false;
  remotePairUntilMs = 0;
  setStatus(remotePeerValid ? "Pilot paired" : "Pilot pairing ended");
  invalidateUi(true);
}

void remoteForgetPeer() {
  remotePeerValid = false;
  remotePairedMac = "";
  memset(remotePeerMac, 0, sizeof(remotePeerMac));
  remoteLastSeenMs = 0;
  remotePilotBatteryValid = false;
  prefs.remove("remoteMac");
  markConfigDirty();
  saveRuntimeConfig(true);
  setStatus("Pilot pairing cleared");
  invalidateUi(true);
}

void remoteStorePeer(const uint8_t mac[6]) {
  if (macIsEmpty(mac)) {
    return;
  }
  memcpy(remotePeerMac, mac, 6);
  remotePeerValid = true;
  remotePairedMac = macToString(mac);
  prefs.putString("remoteMac", remotePairedMac);
  remoteAddPeer(remotePeerMac);
  remoteLastSeenMs = millis();
  markConfigDirty();
  saveRuntimeConfig(true);
  setStatus("Pilot paired %s", remotePairedMac.c_str());
  invalidateUi(true);
}

void remoteHandlePacket(const RadioRemotePacket &packet, const uint8_t mac[6]) {
  if (!radioRemotePacketValid(packet)) {
    return;
  }

  if (packet.pilotBatteryValid) {
    remotePilotBatteryValid = true;
    remotePilotBatteryPercent = packet.pilotBatteryPercent;
  }

  if (packet.type == RADIO_REMOTE_PAIR_REQUEST) {
    if (remotePairingMode || (remotePeerValid && macEquals(mac, remotePeerMac))) {
      remoteStorePeer(mac);
      remotePairingMode = false;
      remoteSendPacket(remotePeerMac, RADIO_REMOTE_PAIR_ACK);
      remoteSendStatus(true);
    }
    return;
  }

  if (!remotePeerValid || !macEquals(mac, remotePeerMac)) {
    return;
  }

  remoteLastSeenMs = millis();
  if (packet.type == RADIO_REMOTE_PING) {
    remoteSendStatus(true);
    return;
  }
  if (packet.type != RADIO_REMOTE_COMMAND) {
    return;
  }

  registerUiActivity();
  switch (packet.command) {
    case RADIO_REMOTE_CMD_PREV:
      stationNext(-1);
      break;
    case RADIO_REMOTE_CMD_NEXT:
      stationNext(1);
      break;
    case RADIO_REMOTE_CMD_VOL_DOWN:
      setVolumeLevel(volumeLevel > 0 ? volumeLevel - 1 : 0);
      break;
    case RADIO_REMOTE_CMD_VOL_UP:
      setVolumeLevel(volumeLevel >= MAX_VOLUME ? MAX_VOLUME : volumeLevel + 1);
      break;
    case RADIO_REMOTE_CMD_TOGGLE:
      togglePlay();
      break;
    case RADIO_REMOTE_CMD_PLAY:
      startStation(currentStation);
      break;
    case RADIO_REMOTE_CMD_STOP:
      stopAudio();
      break;
    default:
      break;
  }
  remoteSendStatus(true);
}

void remoteOnReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !info->src_addr || !data || len != static_cast<int>(sizeof(RadioRemotePacket))) {
    return;
  }
  RadioRemotePacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (!radioRemotePacketValid(packet)) {
    return;
  }

  portENTER_CRITICAL(&remoteRxMux);
  memcpy(&remoteRxPacket, &packet, sizeof(remoteRxPacket));
  memcpy(remoteRxMac, info->src_addr, 6);
  remoteRxPending = true;
  portEXIT_CRITICAL(&remoteRxMux);
}

void serviceRemoteLink() {
  if (!remoteEnabled) {
    if (remoteReady) {
      esp_now_deinit();
      remoteReady = false;
      remotePairingMode = false;
    }
    return;
  }

  if (!remoteReady) {
    initRemoteLink();
  }

  if (remoteRxPending) {
    RadioRemotePacket packet;
    uint8_t mac[6];
    portENTER_CRITICAL(&remoteRxMux);
    memcpy(&packet, &remoteRxPacket, sizeof(packet));
    memcpy(mac, remoteRxMac, 6);
    remoteRxPending = false;
    portEXIT_CRITICAL(&remoteRxMux);
    remoteHandlePacket(packet, mac);
  }

  const uint32_t now = millis();
  if (remotePairingMode && now > remotePairUntilMs) {
    remotePairingMode = false;
    setStatus("Pilot pairing timeout");
    invalidateUi(true);
  }
  remoteSendPairAdvert(false);
  remoteSendStatus(false);
  const bool active = remoteLinkActive();
  if (active != remoteUiLinkActive) {
    remoteUiLinkActive = active;
    invalidateUi(false);
  }
}

void updateCpuStats(uint32_t loopStartUs) {
  const uint32_t busyUs = micros() - loopStartUs;
  const uint32_t nowMs = millis();
  if (cpuStatsWindowMs == 0) {
    cpuStatsWindowMs = nowMs;
  }
  cpuStatsBusyUs += busyUs;
  if (busyUs > cpuLoopMaxUs) {
    cpuLoopMaxUs = busyUs;
  }

  const uint32_t elapsedMs = nowMs - cpuStatsWindowMs;
  if (elapsedMs >= 1000) {
    const uint64_t elapsedUs = static_cast<uint64_t>(elapsedMs) * 1000ULL;
    uint32_t permille = elapsedUs ? static_cast<uint32_t>((static_cast<uint64_t>(cpuStatsBusyUs) * 1000ULL) / elapsedUs) : 0;
    if (permille > 1000) {
      permille = 1000;
    }
    cpuMainLoadPermille = static_cast<uint16_t>(permille);
    cpuStatsBusyUs = 0;
    cpuStatsWindowMs = nowMs;
  }
}

bool initLvglUi() {
  if (!displayReady) {
    return false;
  }

  lv_init();
  lvglDisplay = lv_display_create(LVGL_SCREEN_W, LVGL_SCREEN_H);
  if (!lvglDisplay) {
    return false;
  }
  lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvglDisplay, lvglFlush);
  lv_display_set_buffers(lvglDisplay, lvglDrawBuffer, nullptr, sizeof(lvglDrawBuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);

  if (touchReady) {
    lvglInput = lv_indev_create();
    if (lvglInput) {
      lv_indev_set_type(lvglInput, LV_INDEV_TYPE_POINTER);
      lv_indev_set_read_cb(lvglInput, lvglTouchRead);
    }
  }

  lastLvglTickMs = millis();
  lvglReady = true;
  lvglUiBuilt = false;
  invalidateUi(true);
  return true;
}

void serviceLvgl() {
  if (!lvglReady) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t elapsed = now - lastLvglTickMs;
  if (elapsed) {
    lv_tick_inc(elapsed);
    lastLvglTickMs = now;
  }
  lv_timer_handler();
}

void lvglBuildBootScreen() {
  if (!lvglReady) {
    return;
  }

  lvglRoot = lv_screen_active();
  lv_obj_clean(lvglRoot);
  lv_obj_remove_style_all(lvglRoot);
  lv_obj_set_style_bg_opa(lvglRoot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(lvglRoot, lvColor565(uiBgColor()), 0);
  lv_obj_clear_flag(lvglRoot, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *top = lv_obj_create(lvglRoot);
  lvStylePanel(top, C_SURFACE);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_size(top, LVGL_SCREEN_W, 70);
  lv_obj_set_style_border_width(top, 1, 0);
  lv_obj_set_style_border_color(top, lvColor565(C_SURFACE_3), 0);

  lvBootTitle = lvMakeLabel(lvglRoot, APP_NAME, 18, 28, 204, 2, C_TEXT_MAIN);
  lv_obj_set_style_text_align(lvBootTitle, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *panel = lv_obj_create(lvglRoot);
  lvStylePanel(panel, C_SURFACE, 8);
  lv_obj_set_pos(panel, 14, 96);
  lv_obj_set_size(panel, 212, 142);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lvColor565(C_SURFACE_3), 0);

  lvBootLine = lvMakeLabel(panel, "Starting", 12, 20, 188, 1, C_TEXT_MAIN);
  lvBootPercent = lvMakeLabel(panel, "0%", 12, 48, 188, 1, C_TEXT_MUTED);

  lvBootBar = lv_bar_create(panel);
  lv_obj_set_pos(lvBootBar, 12, 78);
  lv_obj_set_size(lvBootBar, 188, 14);
  lv_bar_set_range(lvBootBar, 0, 100);
  lv_bar_set_value(lvBootBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(lvBootBar, lvColor565(C_SURFACE_2), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(lvBootBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(lvBootBar, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(lvBootBar, lvColor565(C_APP_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(lvBootBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(lvBootBar, 3, LV_PART_INDICATOR);

  for (uint8_t i = 0; i < 3; i++) {
    lvBootDots[i] = lv_obj_create(panel);
    lvStylePanel(lvBootDots[i], i == 0 ? C_APP_ACCENT : C_SURFACE_3, 3);
    lv_obj_set_size(lvBootDots[i], 6, 6);
    lv_obj_set_pos(lvBootDots[i], 88 + i * 18, 112);
  }

  lvglBootBuilt = true;
  lvglUiBuilt = false;
}

void lvglBootPump(uint16_t durationMs) {
  if (!lvglReady) {
    delay(durationMs);
    return;
  }

  const uint32_t start = millis();
  do {
    const uint32_t now = millis();
    const uint8_t active = (now / 180) % 3;
    for (uint8_t i = 0; i < 3; i++) {
      if (lvBootDots[i]) {
        lv_obj_set_style_bg_color(lvBootDots[i], lvColor565(i == active ? C_APP_ACCENT : C_SURFACE_3), 0);
      }
    }
    updateLed();
    serviceLvgl();
    delay(8);
  } while (millis() - start < durationMs);
}

void lvglShowBootStep(const char *line, uint8_t percent) {
  if (!lvglReady) {
    drawBoot(line);
    return;
  }
  if (percent > 100) {
    percent = 100;
  }
  if (!lvglBootBuilt) {
    lvglBuildBootScreen();
  }
  if (lvBootLine) {
    lv_label_set_text(lvBootLine, line);
  }
  if (lvBootPercent) {
    lv_label_set_text(lvBootPercent, (String(percent) + "%").c_str());
  }
  if (lvBootBar) {
    lv_bar_set_value(lvBootBar, percent, LV_ANIM_ON);
  }
  lvglBootPump(160);
}

void lvglFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  const int16_t w = area->x2 - area->x1 + 1;
  const int16_t h = area->y2 - area->y1 + 1;
  display.gfx()->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(pxMap), w, h);
  lv_display_flush_ready(disp);
}

void lvglTouchRead(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  static uint16_t lastX = 0;
  static uint16_t lastY = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  if (touchReady && touch.read(x, y)) {
    lastX = x;
    lastY = y;
    if (screenSaverActive) {
      wakeScreenSaver();
      data->point.x = x;
      data->point.y = y;
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    } else {
      registerUiActivity();
    }
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->point.x = lastX;
    data->point.y = lastY;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

lv_color_t lvColor565(uint16_t color) {
  const uint8_t r = ((color >> 11) & 0x1F) << 3;
  const uint8_t g = ((color >> 5) & 0x3F) << 2;
  const uint8_t b = (color & 0x1F) << 3;
  return lv_color_make(r, g, b);
}

void lvStylePanel(lv_obj_t *obj, uint16_t bg, uint8_t radius) {
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(obj, lvColor565(bg), 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *lvMakeLabel(lv_obj_t *parent, const char *text, int16_t x, int16_t y, int16_t w, uint8_t fontSize, uint16_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_width(label, w);
  lv_obj_set_height(label, fontSize >= 4 ? 42 : (fontSize > 1 ? 22 : 18));
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lvColor565(color), 0);
  if (fontSize >= 4) {
    lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
  } else if (fontSize > 1) {
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
  }
  lv_obj_set_style_text_line_space(label, 0, 0);
  return label;
}

lv_obj_t *lvMakeButton(lv_obj_t *parent, const String &text, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t bg, uint16_t fg, LvglAction action) {
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_bg_color(button, lvColor565(bg), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_color(button, lvColor565(C_SURFACE_3), 0);
  lv_obj_set_style_radius(button, 7, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_add_event_cb(button, lvglButtonEvent, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(action)));

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_text(label, text.c_str());
  lv_obj_set_style_text_color(label, lvColor565(fg), 0);
  lv_obj_set_width(label, w - 10);
  lv_obj_set_height(label, 18);
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_obj_center(label);
  return button;
}

lv_obj_t *lvMakeChip(lv_obj_t *parent, const String &text, int16_t x, int16_t y, int16_t w, uint16_t bg, uint16_t fg) {
  lv_obj_t *chip = lv_obj_create(parent);
  lvStylePanel(chip, bg, 6);
  lv_obj_set_pos(chip, x, y);
  lv_obj_set_size(chip, w, 22);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_border_color(chip, lvColor565(C_SURFACE_3), 0);
  lv_obj_t *label = lvMakeLabel(chip, text.c_str(), 0, 3, w, 1, fg);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return chip;
}

void lvBuildWifiIcon(lv_obj_t *parent, int16_t x, int16_t y) {
  for (uint8_t i = 0; i < 4; i++) {
    lvWifiBars[i] = lv_obj_create(parent);
    lvStylePanel(lvWifiBars[i], C_DARK, 1);
    const int16_t h = 3 + i * 2;
    lv_obj_set_size(lvWifiBars[i], 2, h);
    lv_obj_set_pos(lvWifiBars[i], x + i * 4, y + 10 - h);
  }
}

void lvBuildBatteryIcon(lv_obj_t *parent, int16_t x, int16_t y) {
  lvBatteryShell = lv_obj_create(parent);
  lvStylePanel(lvBatteryShell, C_SURFACE, 2);
  lv_obj_set_pos(lvBatteryShell, x, y);
  lv_obj_set_size(lvBatteryShell, 19, 9);
  lv_obj_set_style_border_width(lvBatteryShell, 1, 0);
  lv_obj_set_style_border_color(lvBatteryShell, lvColor565(C_TEXT_MUTED), 0);

  lvBatteryFill = lv_obj_create(parent);
  lvStylePanel(lvBatteryFill, C_GREEN, 1);
  lv_obj_set_pos(lvBatteryFill, x + 2, y + 2);
  lv_obj_set_size(lvBatteryFill, 1, 5);

  lvBatteryTip = lv_obj_create(parent);
  lvStylePanel(lvBatteryTip, C_TEXT_MUTED, 1);
  lv_obj_set_pos(lvBatteryTip, x + 19, y + 3);
  lv_obj_set_size(lvBatteryTip, 2, 3);

  lvBatteryCharge = lvMakeLabel(parent, "+", x + 24, y - 4, 8, 1, C_GREEN);
}

bool batteryChargingLikely() {
  return batteryValid && batteryPercent >= 95;
}

void lvSyncWifiIcon() {
  const int8_t bars = WiFi.status() == WL_CONNECTED ? wifiSignalBars : 0;
  for (uint8_t i = 0; i < 4; i++) {
    if (!lvWifiBars[i]) {
      continue;
    }
    const uint16_t color = i < bars ? (bars <= 1 ? C_RED : (bars == 2 ? C_YELLOW : C_GREEN)) : C_SURFACE_3;
    lv_obj_set_style_bg_color(lvWifiBars[i], lvColor565(color), 0);
  }
}

void lvSyncBatteryIcon() {
  if (!lvBatteryFill || !lvBatteryShell || !lvBatteryCharge) {
    return;
  }
  const uint16_t color = !batteryValid ? C_SURFACE_3 : (batteryPercent <= 15 ? C_RED : (batteryPercent <= 35 ? C_YELLOW : C_GREEN));
  const uint8_t fillW = batteryValid ? (static_cast<uint16_t>(batteryPercent) * 15U) / 100U : 1;
  lv_obj_set_width(lvBatteryFill, fillW ? fillW : 1);
  lv_obj_set_style_bg_color(lvBatteryFill, lvColor565(color), 0);
  lv_obj_set_style_border_color(lvBatteryShell, lvColor565(batteryValid ? C_TEXT_MUTED : C_SURFACE_3), 0);
  lv_label_set_text(lvBatteryCharge, batteryChargingLikely() ? "+" : "");
  lv_obj_set_style_text_color(lvBatteryCharge, lvColor565(C_GREEN), 0);
}

void lvSetButtonText(lv_obj_t *button, const String &text) {
  if (!button) {
    return;
  }
  lv_obj_t *label = lv_obj_get_child(button, 0);
  if (label) {
    lv_label_set_text(label, text.c_str());
    lv_obj_center(label);
  }
}

void lvglButtonEvent(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  if (screenSaverActive) {
    wakeScreenSaver();
    return;
  }
  registerUiActivity();
  const LvglAction action = static_cast<LvglAction>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  const int stationBase = static_cast<int>(LvglAction::Station0);
  const int actionValue = static_cast<int>(action);
  if (actionValue >= stationBase && actionValue <= static_cast<int>(LvglAction::Station4)) {
    const int idx = scrollStation + (actionValue - stationBase);
    if (idx < stationCount) {
      startStation(idx);
    }
    return;
  }

  switch (action) {
    case LvglAction::Prev:
      stationNext(-1);
      break;
    case LvglAction::PlayStop:
      togglePlay();
      break;
    case LvglAction::Next:
      stationNext(1);
      break;
    case LvglAction::VolDown:
      setVolumeLevel(volumeLevel > 0 ? volumeLevel - 1 : 0);
      break;
    case LvglAction::Menu:
      uiScreen = UiScreen::Menu;
      invalidateUi(true);
      break;
    case LvglAction::VolUp:
      setVolumeLevel(volumeLevel >= MAX_VOLUME ? MAX_VOLUME : volumeLevel + 1);
      break;
    case LvglAction::ApToggle:
      if (apActive) {
        stopPortal();
      } else {
        startPortal();
      }
      break;
    case LvglAction::ReloadSd:
      reloadSdStations();
      break;
    case LvglAction::ReconnectWifi:
      reconnectWifi();
      break;
    case LvglAction::DarkMode:
      toggleDarkMode();
      break;
    case LvglAction::StationUp:
      scrollStations(-1);
      break;
    case LvglAction::StationDown:
      scrollStations(1);
      break;
    case LvglAction::PairRemote:
      remoteBeginPairing();
      break;
    case LvglAction::ClearWifi:
      clearWifi();
      break;
    case LvglAction::Back:
      uiScreen = UiScreen::Main;
      invalidateUi(true);
      break;
    default:
      break;
  }
}

void lvglBuildHeader(lv_obj_t *parent, const char *title) {
  lv_obj_t *header = lv_obj_create(parent);
  lvStylePanel(header, uiSurfaceColor());
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_size(header, LVGL_SCREEN_W, 50);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_color(header, lvColor565(uiSurface3Color()), 0);

  lvTitle = lvMakeLabel(header, title, 10, 7, 70, 2, uiTextColor());
  lvClock = lvMakeLabel(header, "--:--", 10, 31, 58, 1, uiMutedColor());

  lv_obj_t *volumeChip = lv_obj_create(header);
  lvStylePanel(volumeChip, uiSurface2Color(), 7);
  lv_obj_set_pos(volumeChip, 84, 14);
  lv_obj_set_size(volumeChip, 30, 20);
  lv_obj_set_style_border_width(volumeChip, 1, 0);
  lv_obj_set_style_border_color(volumeChip, lvColor565(uiSurface3Color()), 0);
  lvVolume = lvMakeLabel(volumeChip, "", 0, 2, 30, 1, uiTextColor());
  lv_obj_set_style_text_align(lvVolume, LV_TEXT_ALIGN_CENTER, 0);

  lvBuildWifiIcon(header, 126, 17);
  lvBuildBatteryIcon(header, 148, 18);
  lvSd = lvMakeLabel(header, "", 190, 7, 42, 1, C_GREEN);
  lvRemote = lvMakeLabel(header, "", 174, 29, 22, 1, C_TEXT_MUTED);
  lvPlayState = lvMakeLabel(header, "", 200, 29, 32, 1, uiAccentColor());
}

void lvglBuildMain() {
  lvglBuildHeader(lvglRoot, "Radio");

  lv_obj_t *status = lv_obj_create(lvglRoot);
  lvStylePanel(status, uiSurfaceColor(), 8);
  lv_obj_set_pos(status, 8, 58);
  lv_obj_set_size(status, 224, 78);
  lv_obj_set_style_border_width(status, 1, 0);
  lv_obj_set_style_border_color(status, lvColor565(uiSurface3Color()), 0);
  lvNetwork = lvMakeLabel(status, "", 12, 9, 200, 1, C_GREEN);
  lvNowPlaying = lvMakeLabel(status, "", 12, 30, 200, 1, uiTextColor());
  lvStation = lvMakeLabel(status, "", 12, 52, 200, 1, uiMutedColor());

  const int listY = 144;
  const int rowH = 21;
  for (int i = 0; i < 5; i++) {
    lvStationButtons[i] = lvMakeButton(lvglRoot, "", 8, listY + i * rowH, 190, rowH - 3, uiSurfaceColor(), uiTextColor(), static_cast<LvglAction>(static_cast<int>(LvglAction::Station0) + i));
    lvStationLabels[i] = lv_obj_get_child(lvStationButtons[i], 0);
  }
  lvStationScrollUp = lvMakeButton(lvglRoot, "^", 204, 144, 28, 47, uiSurface2Color(), uiTextColor(), LvglAction::StationUp);
  lvStationScrollDown = lvMakeButton(lvglRoot, "v", 204, 197, 28, 47, uiSurface2Color(), uiTextColor(), LvglAction::StationDown);

  lvButtonPrev = lvMakeButton(lvglRoot, "Prev", 8, 246, 68, 30, uiSurface2Color(), uiTextColor(), LvglAction::Prev);
  lvButtonPlay = lvMakeButton(lvglRoot, "Play", 86, 246, 68, 30, uiAccentColor(), C_BLACK, LvglAction::PlayStop);
  lvButtonNext = lvMakeButton(lvglRoot, "Next", 164, 246, 68, 30, uiSurface2Color(), uiTextColor(), LvglAction::Next);
  lvButtonVolDown = lvMakeButton(lvglRoot, "Vol-", 8, 284, 68, 30, uiSurface2Color(), uiTextColor(), LvglAction::VolDown);
  lvButtonMenu = lvMakeButton(lvglRoot, "Menu", 86, 284, 68, 30, uiSurfaceColor(), uiTextColor(), LvglAction::Menu);
  lvButtonVolUp = lvMakeButton(lvglRoot, "Vol+", 164, 284, 68, 30, uiSurface2Color(), uiTextColor(), LvglAction::VolUp);
}

void lvglBuildMenu() {
  lvglBuildHeader(lvglRoot, "Menu");
  lv_obj_t *panel = lv_obj_create(lvglRoot);
  lvStylePanel(panel, uiSurfaceColor(), 8);
  lv_obj_set_pos(panel, 8, 60);
  lv_obj_set_size(panel, 224, 46);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lvColor565(uiSurface3Color()), 0);
  lvMenuStatus = lvMakeLabel(panel, "", 12, 14, 200, 1, uiMutedColor());

  lvMenuAp = lvMakeButton(lvglRoot, apActive ? "AP Off" : "Start AP", 12, 110, 216, 30, apActive ? C_ORANGE : uiSurface2Color(), uiTextColor(), LvglAction::ApToggle);
  lvMakeButton(lvglRoot, "Reload SD", 12, 144, 216, 30, uiSurface2Color(), uiTextColor(), LvglAction::ReloadSd);
  lvMakeButton(lvglRoot, "Reconnect WiFi", 12, 178, 216, 30, uiSurface2Color(), uiTextColor(), LvglAction::ReconnectWifi);
  lvMenuDarkMode = lvMakeButton(lvglRoot, String("Dark mode: ") + (darkMode ? "ON" : "OFF"), 12, 212, 216, 30, C_APP_ACCENT, C_BLACK, LvglAction::DarkMode);
  lvMenuRemote = lvMakeButton(lvglRoot, remotePairingMode ? "Pairing..." : "Pair Pilot", 12, 246, 104, 30, remotePairingMode ? C_ORANGE : uiSurface2Color(), uiTextColor(), LvglAction::PairRemote);
  lvMakeButton(lvglRoot, "Clear WiFi", 124, 246, 104, 30, C_RED, C_WHITE, LvglAction::ClearWifi);
  lvMakeButton(lvglRoot, "Back", 12, 282, 216, 30, uiSurfaceColor(), uiTextColor(), LvglAction::Back);
}

void lvglBuildUi(bool fullRedraw) {
  (void)fullRedraw;
  if (!lvglReady) {
    return;
  }

  lvglRoot = lv_screen_active();
  lv_obj_clean(lvglRoot);
  lv_obj_remove_style_all(lvglRoot);
  lv_obj_set_style_bg_opa(lvglRoot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(lvglRoot, lvColor565(uiBgColor()), 0);
  lv_obj_clear_flag(lvglRoot, LV_OBJ_FLAG_SCROLLABLE);

  lvTitle = nullptr;
  lvClock = nullptr;
  lvVolume = nullptr;
  lvWifi = nullptr;
  lvBattery = nullptr;
  for (uint8_t i = 0; i < 4; i++) {
    lvWifiBars[i] = nullptr;
  }
  lvBatteryShell = nullptr;
  lvBatteryFill = nullptr;
  lvBatteryTip = nullptr;
  lvBatteryCharge = nullptr;
  lvSd = nullptr;
  lvPlayState = nullptr;
  lvRemote = nullptr;
  lvNetwork = nullptr;
  lvNowPlaying = nullptr;
  lvStation = nullptr;
  lvMenuStatus = nullptr;
  lvMenuDarkMode = nullptr;
  lvStationScrollUp = nullptr;
  lvStationScrollDown = nullptr;
  lvMenuAp = nullptr;
  lvMenuRemote = nullptr;
  lvSaverClock = nullptr;
  lvSaverStation = nullptr;
  lvSaverStatus = nullptr;
  lvSaverRadioBattery = nullptr;
  lvSaverPilotBattery = nullptr;
  lvSaverLink = nullptr;
  lvBootTitle = nullptr;
  lvBootLine = nullptr;
  lvBootPercent = nullptr;
  lvBootBar = nullptr;
  lvBootScan = nullptr;
  for (uint8_t i = 0; i < 3; i++) {
    lvBootDots[i] = nullptr;
  }
  lvglBootBuilt = false;
  for (uint8_t i = 0; i < 5; i++) {
    lvStationButtons[i] = nullptr;
    lvStationLabels[i] = nullptr;
  }

  if (uiScreen == UiScreen::Menu) {
    lvglBuildMenu();
  } else {
    lvglBuildMain();
  }
  lvglUiBuilt = true;
}

void lvglSyncHeader() {
  if (!lvTitle) {
    return;
  }
  lv_label_set_text(lvTitle, uiScreen == UiScreen::Menu ? "Menu" : "Radio");
  if (lvClock) {
    lv_label_set_text(lvClock, clockText);
    lv_obj_set_style_text_color(lvClock, lvColor565(clockValid ? uiTextColor() : uiMutedColor()), 0);
  }
  if (lvVolume) {
    lv_label_set_text(lvVolume, (String("V") + String(volumeLevel)).c_str());
  }
  lvSyncWifiIcon();
  lvSyncBatteryIcon();
  if (lvSd) {
    lv_label_set_text(lvSd, sdReady ? "SD" : "--");
    lv_obj_set_style_text_color(lvSd, lvColor565(sdReady ? C_GREEN : C_RED), 0);
  }
  if (lvPlayState) {
    lv_label_set_text(lvPlayState, playing ? "PLAY" : "STOP");
    lv_obj_set_style_text_color(lvPlayState, lvColor565(playing ? uiAccentColor() : uiMutedColor()), 0);
  }
  if (lvRemote) {
    const uint8_t bars = remoteLinkBars();
    lv_label_set_text(lvRemote, bars ? (String("P") + String(bars)).c_str() : (remotePeerValid ? "P-" : "P?"));
    lv_obj_set_style_text_color(lvRemote, lvColor565(bars >= 2 ? C_GREEN : (bars == 1 || remotePeerValid ? C_YELLOW : C_TEXT_MUTED)), 0);
  }
}

void lvglSyncMain() {
  if (!lvNetwork) {
    return;
  }
  lv_label_set_text(lvNetwork, networkText().c_str());
  lv_obj_set_style_text_color(lvNetwork, lvColor565(WiFi.status() == WL_CONNECTED ? C_GREEN : C_YELLOW), 0);
  lv_label_set_text(lvNowPlaying, streamTitle[0] ? streamTitle : audioStatus);
  lv_obj_set_style_text_color(lvNowPlaying, lvColor565(uiTextColor()), 0);
  lv_label_set_text(lvStation, stationCount ? stations[currentStation].name.c_str() : "No stations");
  lv_obj_set_style_text_color(lvStation, lvColor565(uiMutedColor()), 0);
  for (uint8_t i = 0; i < 5; i++) {
    const int idx = scrollStation + i;
    if (!lvStationButtons[i]) {
      continue;
    }
    if (idx < stationCount) {
      lv_obj_clear_flag(lvStationButtons[i], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(lvStationLabels[i], stations[idx].name.c_str());
      uint16_t bg = uiSurfaceColor();
      uint16_t fg = uiTextColor();
      if (idx == currentStation && playing) {
        bg = uiAccentColor();
        fg = C_WHITE;
      } else if (idx == currentStation) {
        bg = darkMode ? 0x0451 : C_APP_ACCENT_2;
        fg = uiTextColor();
      }
      lv_obj_set_style_bg_color(lvStationButtons[i], lvColor565(bg), 0);
      lv_obj_set_style_text_color(lvStationLabels[i], lvColor565(fg), 0);
    } else {
      lv_obj_add_flag(lvStationButtons[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  lvSetButtonText(lvButtonPlay, playing ? "Stop" : "Play");
  if (lvButtonPlay) {
    lv_obj_set_style_bg_color(lvButtonPlay, lvColor565(playing ? C_RED : uiAccentColor()), 0);
  }
  const bool canScrollUp = scrollStation > 0;
  const bool canScrollDown = stationCount > 5 && scrollStation < static_cast<int>(stationCount) - 5;
  if (lvStationScrollUp) {
    lv_obj_set_style_bg_color(lvStationScrollUp, lvColor565(canScrollUp ? uiSurface2Color() : uiSurface3Color()), 0);
  }
  if (lvStationScrollDown) {
    lv_obj_set_style_bg_color(lvStationScrollDown, lvColor565(canScrollDown ? uiSurface2Color() : uiSurface3Color()), 0);
  }
}

void lvglSyncMenu() {
  if (!lvMenuStatus) {
    return;
  }
  lv_label_set_text(lvMenuStatus, (networkText() + "  " + String(audioStatus)).c_str());
  lvSetButtonText(lvMenuAp, apActive ? "AP Off" : "Start AP");
  lvSetButtonText(lvMenuDarkMode, String("Dark mode: ") + (darkMode ? "ON" : "OFF"));
  lvSetButtonText(lvMenuRemote, remotePairingMode ? "Pairing..." : "Pair Pilot");
  if (lvMenuRemote) {
    lv_obj_set_style_bg_color(lvMenuRemote, lvColor565(remotePairingMode ? C_ORANGE : uiSurface2Color()), 0);
  }
}

void lvglSyncUi() {
  if (!lvglReady) {
    return;
  }
  if (screenSaverActive) {
    lvglSyncScreenSaver();
    return;
  }
  lvglSyncHeader();
  if (uiScreen == UiScreen::Menu) {
    lvglSyncMenu();
  } else {
    lvglSyncMain();
  }
}

void lvglBuildScreenSaver() {
  if (!lvglReady) {
    return;
  }
  lvglRoot = lv_screen_active();
  lv_obj_clean(lvglRoot);
  lv_obj_remove_style_all(lvglRoot);
  lv_obj_set_style_bg_opa(lvglRoot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(lvglRoot, lvColor565(C_BLACK), 0);
  lv_obj_clear_flag(lvglRoot, LV_OBJ_FLAG_SCROLLABLE);

  lvSaverClock = lvMakeLabel(lvglRoot, "--:--", 8, 42, 224, 4, C_WHITE);
  lv_obj_set_style_text_align(lvSaverClock, LV_TEXT_ALIGN_CENTER, 0);
  lvSaverStation = lvMakeLabel(lvglRoot, "", 16, 118, 208, 2, C_APP_ACCENT);
  lv_obj_set_style_text_align(lvSaverStation, LV_TEXT_ALIGN_CENTER, 0);
  lvSaverStatus = lvMakeLabel(lvglRoot, "", 16, 154, 208, 1, C_TEXT_MUTED);
  lv_obj_set_style_text_align(lvSaverStatus, LV_TEXT_ALIGN_CENTER, 0);
  lvSaverRadioBattery = lvMakeLabel(lvglRoot, "", 16, 194, 96, 1, C_GREEN);
  lvSaverPilotBattery = lvMakeLabel(lvglRoot, "", 128, 194, 96, 1, C_GREEN);
  lvSaverLink = lvMakeLabel(lvglRoot, "", 16, 224, 208, 1, C_YELLOW);
  lv_obj_set_style_text_align(lvSaverLink, LV_TEXT_ALIGN_CENTER, 0);
  lvMakeLabel(lvglRoot, "touch to wake", 70, 292, 120, 1, C_TEXT_MUTED);
  lvglUiBuilt = true;
}

void lvglSyncScreenSaver() {
  if (!lvSaverClock) {
    return;
  }
  lv_label_set_text(lvSaverClock, clockValid ? clockText : "--:--");
  lv_obj_set_style_text_color(lvSaverClock, lvColor565(clockValid ? C_WHITE : C_TEXT_MUTED), 0);
  lv_label_set_text(lvSaverStation, stationCount ? stations[currentStation].name.c_str() : "No stations");
  lv_label_set_text(lvSaverStatus, (String(playing ? "PLAY" : "STOP") + "  " + networkText()).c_str());
  lv_label_set_text(lvSaverRadioBattery, (String("Radio ") + (batteryValid ? String(batteryPercent) + "%" : "--")).c_str());
  lv_label_set_text(lvSaverPilotBattery, (String("Pilot ") + (remotePilotBatteryValid ? String(remotePilotBatteryPercent) + "%" : "--")).c_str());
  const String link = String("WiFi ") + String(wifiSignalBars) + "/4  " + (remoteLinkActive() ? "PILOT ON" : (remotePeerValid ? "PILOT OFF" : "NO PILOT"));
  lv_label_set_text(lvSaverLink, link.c_str());
  lv_obj_set_style_text_color(lvSaverLink, lvColor565(remoteLinkActive() ? C_GREEN : C_YELLOW), 0);
  lastScreenSaverDrawMs = millis();
}

void registerUiActivity() {
  lastUiActivityMs = millis();
  if (screenSaverActive) {
    screenSaverActive = false;
    display.setBacklight(true);
    lvglUiBuilt = false;
    invalidateUi(true);
  }
}

bool wakeScreenSaver() {
  if (!screenSaverActive) {
    registerUiActivity();
    return false;
  }
  registerUiActivity();
  return true;
}

void serviceScreenSaver() {
  if (!displayReady || otaInProgress) {
    return;
  }
  const uint32_t now = millis();
  if (!screenSaverActive && now - lastUiActivityMs >= SCREEN_SAVER_MS) {
    screenSaverActive = true;
    lvglUiBuilt = false;
    lastScreenSaverDrawMs = 0;
    uiDirty = true;
    uiFullRedraw = true;
  } else if (screenSaverActive && now - lastScreenSaverDrawMs >= SCREEN_SAVER_REFRESH_MS) {
    uiDirty = true;
  }
}

void serviceTraceTimeouts() {
  const uint32_t now = millis();
  if (i2sTrace && i2sTraceUntilMs && now > i2sTraceUntilMs) {
    i2sTrace = false;
    i2sTraceUntilMs = 0;
    DEBUG_PORT.println("I2S trace auto OFF");
  }
  if (codecTrace && codecTraceUntilMs && now > codecTraceUntilMs) {
    codecTrace = false;
    codecTraceUntilMs = 0;
    DEBUG_PORT.println("ES8311 I2C trace auto OFF");
  }
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

  switch (ledMode) {
    case LedMode::Boot:
      showLedEffect(ledBootEffect, now, 46, 56, 82, false);
      break;
    case LedMode::Wifi:
      showLedEffect(ledWifiEffect, now, 90, 38, 0, false);
      break;
    case LedMode::Ap:
      showLedEffect(ledApEffect, now, 0, 30, 90, false);
      break;
    case LedMode::Streaming: {
      const DisplayPalette &t = palette();
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
  logf("%s %s", APP_NAME, APP_VERSION);
  logf("Debug UART: GPIO43 TX / GPIO44 RX, 115200 baud");
  logf("Chip: %s, PSRAM: %u bytes", ESP.getChipModel(), ESP.getPsramSize());
}

void preferBinLoaderOnNextReset() {
  const esp_err_t err = esp32BinLoaderReturnToFactoryOnNextBoot();
  if (err == ESP_OK) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
      logf("Next reset target: ESP32 Bin Loader factory partition");
    }
  } else if (err != ESP_ERR_NOT_FOUND) {
    logf("Cannot set factory loader as next boot target: %d", static_cast<int>(err));
  }
}

void initApName() {
  const uint32_t chip = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(apName, sizeof(apName), "ESP32Radio-%04X", chip & 0xFFFF);
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
  gfx->print(APP_NAME);
  gfx->setTextSize(1);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(8, 48);
  gfx->print(line);
}

void drawOtaProgress(const char *line, uint8_t percent, bool error) {
  if (percent > 100) {
    percent = 100;
  }
  if (lvglReady) {
    if (!lvglBootBuilt) {
      lvglBuildBootScreen();
    }
    if (lvBootTitle) {
      lv_label_set_text(lvBootTitle, "OTA update");
      lv_obj_set_style_text_color(lvBootTitle, lvColor565(error ? C_RED : C_TEXT_MAIN), 0);
    }
    if (lvBootLine) {
      lv_label_set_text(lvBootLine, line);
      lv_obj_set_style_text_color(lvBootLine, lvColor565(error ? C_RED : C_TEXT_MAIN), 0);
    }
    if (lvBootPercent) {
      lv_label_set_text(lvBootPercent, (String(percent) + "%").c_str());
    }
    if (lvBootBar) {
      lv_obj_set_style_bg_color(lvBootBar, lvColor565(error ? C_RED : C_GREEN), LV_PART_INDICATOR);
      lv_bar_set_value(lvBootBar, percent, LV_ANIM_OFF);
    }
    lvglBootPump(40);
    return;
  }

  if (!displayReady) {
    return;
  }
  Arduino_GFX *gfx = display.gfx();
  gfx->fillScreen(C_BLACK);
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  gfx->setTextColor(error ? C_RED : C_CYAN);
  gfx->setCursor(18, 48);
  gfx->print("OTA update");
  gfx->setTextSize(1);
  gfx->setTextColor(error ? C_RED : C_WHITE);
  gfx->setCursor(18, 92);
  gfx->print(clipped(String(line), 204, 1));
  gfx->drawRect(18, 124, 204, 16, C_DIM);
  gfx->fillRect(20, 126, (200 * percent) / 100, 12, error ? C_RED : C_GREEN);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(18, 154);
  gfx->print(percent);
  gfx->print("%");
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

void drawScreenSaver() {
  Arduino_GFX *gfx = display.gfx();
  gfx->fillScreen(C_BLACK);
  gfx->setTextWrap(false);

  const String clock = clockValid ? String(clockText) : String("--:--");
  gfx->setTextSize(5);
  gfx->setTextColor(clockValid ? C_WHITE : C_DIM);
  gfx->setCursor((LVGL_SCREEN_W - static_cast<int16_t>(clock.length()) * 30) / 2, 54);
  gfx->print(clock);

  gfx->setTextSize(2);
  gfx->setTextColor(C_APP_ACCENT);
  const String station = stationCount ? stations[currentStation].name : String("No stations");
  drawTextClip(18, 136, 204, station, C_APP_ACCENT, 2);

  gfx->setTextSize(1);
  drawTextClip(18, 170, 204, String(playing ? "PLAY" : "STOP") + "  " + networkText(), C_TEXT_MUTED, 1);
  drawTextClip(18, 196, 96, String("Radio ") + (batteryValid ? String(batteryPercent) + "%" : "--"), C_GREEN, 1);
  drawTextClip(126, 196, 96, String("Pilot ") + (remotePilotBatteryValid ? String(remotePilotBatteryPercent) + "%" : "--"), C_GREEN, 1);
  const String link = String("WiFi ") + String(wifiSignalBars) + "/4  " + (remoteLinkActive() ? "PILOT ON" : (remotePeerValid ? "PILOT OFF" : "NO PILOT"));
  drawTextClip(18, 224, 204, link, remoteLinkActive() ? C_GREEN : C_YELLOW, 1);
  drawTextClip(70, 292, 120, "touch to wake", C_DIM, 1);
  lastScreenSaverDrawMs = millis();
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
    return apInfoText();
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
  const DisplayPalette &t = palette();
  gfx->fillRect(0, 0, 240, 32, t.header);
  drawTextClip(6, 7, 84, title, C_WHITE, 2);
  drawTextClip(6, 22, 74, clockText, clockValid ? C_WHITE : C_DIM, 1);
  String vol = "V";
  vol += volumeLevel;
  drawTextClip(94, 4, 28, vol, C_WHITE, 1);
  drawWifiBars(126, 4);
  drawBatteryStatus(154, 3);
  drawTextClip(94, 20, 44, sdReady ? "SD" : "noSD", sdReady ? C_GREEN : C_YELLOW, 1);
  drawTextClip(142, 20, 28, remoteLinkActive() ? "P+" : (remotePeerValid ? "P-" : "P?"), remoteLinkActive() ? C_GREEN : (remotePeerValid ? C_YELLOW : C_DIM), 1);
  drawTextClip(172, 20, 62, playing ? "PLAY" : "STOP", playing ? C_CYAN : C_DIM, 1);
}

void drawMain(bool fullRedraw) {
  Arduino_GFX *gfx = display.gfx();
  const DisplayPalette &t = palette();
  if (fullRedraw) {
    gfx->fillScreen(C_BLACK);
  }
  drawHeader("Radio");

  gfx->fillRect(0, 32, 240, 58, t.status);
  drawTextClip(6, 38, 172, networkText(), WiFi.status() == WL_CONNECTED ? C_GREEN : C_YELLOW, 1);
  drawTextClip(6, 54, 172, streamTitle[0] ? String(streamTitle) : String(audioStatus), C_WHITE, 1);
  drawTextClip(6, 70, 172, stationCount ? stations[currentStation].name : String("No stations"), C_DIM, 1);

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
  const DisplayPalette &t = palette();
  if (fullRedraw) {
    gfx->fillScreen(C_BLACK);
  }
  drawHeader("Menu");
  drawTextClip(8, 40, 224, networkText(), WiFi.status() == WL_CONNECTED ? C_GREEN : C_YELLOW, 1);
  drawTextClip(8, 56, 224, audioStatus, C_DIM, 1);

  drawButton(12, 72, 216, 32, apActive ? "AP Off" : "Start AP", apActive ? C_ORANGE : t.header, apActive ? C_BLACK : C_WHITE);
  drawButton(12, 112, 216, 32, "Reload SD", t.button, C_WHITE);
  drawButton(12, 152, 216, 32, "Reconnect WiFi", t.button, C_WHITE);
  drawButton(12, 192, 216, 32, String("Dark mode: ") + (darkMode ? "ON" : "OFF"), t.accent, C_BLACK);
  drawButton(12, 232, 104, 32, remotePairingMode ? "Pairing..." : "Pair Pilot", remotePairingMode ? C_ORANGE : t.button, C_WHITE);
  drawButton(124, 232, 104, 32, "Clear WiFi", C_RED, C_WHITE);
  drawButton(12, 276, 216, 34, "Back", t.highlight, C_BLACK);
}

void drawUi() {
  if (!displayReady || !uiDirty) {
    return;
  }
  if (screenSaverActive) {
    uiDirty = false;
    uiFullRedraw = false;
    if (lvglReady) {
      if (!lvglUiBuilt || !lvSaverClock) {
        lvglBuildScreenSaver();
      }
      lvglSyncScreenSaver();
    } else {
      drawScreenSaver();
    }
    return;
  }
  if (lvglReady) {
    const bool fullRedraw = uiFullRedraw || !lvglUiBuilt;
    uiDirty = false;
    uiFullRedraw = false;
    if (fullRedraw) {
      lvglBuildUi(true);
    }
    lvglSyncUi();
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
  text.reserve(6144);
  for (uint8_t i = 0; i < stationCount; i++) {
    text += stations[i].name;
    text += "|";
    text += stations[i].url;
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
  sdDataReady = false;
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
    ensureSdFiles();
  } else {
    logf("SD mount failed; %s is required", APP_DATA_DIR);
    setStatus("SD card missing");
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
  sdDataReady = false;
  if (!sdReady) {
    return;
  }
  if (!SD_MMC.exists("/apps_data")) {
    SD_MMC.mkdir("/apps_data");
  }
  if (!SD_MMC.exists(APP_DATA_DIR)) {
    SD_MMC.mkdir(APP_DATA_DIR);
  }
  const bool hasStations = SD_MMC.exists(STATIONS_FILE);
  const bool hasConfig = SD_MMC.exists(CONFIG_FILE);
  sdDataReady = hasStations && hasConfig;
  if (!sdDataReady) {
    logf("Missing required radio SD files: %s%s%s",
         hasConfig ? "" : CONFIG_FILE,
         (!hasConfig && !hasStations) ? " and " : "",
         hasStations ? "" : STATIONS_FILE);
    setStatus("Missing SD files in %s", APP_DATA_DIR);
  }
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
  String url = line.substring(sep + 1);
  const int extraSep = url.indexOf('|');
  if (extraSep >= 0) {
    url = url.substring(0, extraSep);
  }
  name.trim();
  url.trim();
  if (!name.length() || !(url.startsWith("http://") || url.startsWith("https://"))) {
    return;
  }
  if (stationCount >= MAX_STATIONS) {
    return;
  }
  stations[stationCount] = {name, url};
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
  bool loaded = false;

  if (sdReady && SD_MMC.exists(STATIONS_FILE)) {
    File f = SD_MMC.open(STATIONS_FILE, FILE_READ);
    if (f) {
      String text = f.readString();
      f.close();
      loadStationsFromText(text);
      loaded = stationCount > 0;
      logf("Loaded %u stations from SD", stationCount);
    }
  }

  if (!loaded) {
    logf("No stations loaded; %s is required on SD", STATIONS_FILE);
    setStatus("Missing stations.csv on SD");
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
  bool ok = false;
  if (sdReady) {
    File f = SD_MMC.open(STATIONS_FILE, "w");
    if (f) {
      f.print(text);
      if (!text.endsWith("\n")) {
        f.println();
      }
      f.close();
      ok = true;
    }
  }
  stationCount = 0;
  loadStationsFromText(text);
  const int maxStationIndex = stationCount > 0 ? stationCount - 1 : 0;
  const int maxScrollIndex = stationCount > 5 ? stationCount - 5 : 0;
  currentStation = constrain(currentStation, 0, maxStationIndex);
  scrollStation = scrollStation < maxScrollIndex ? scrollStation : maxScrollIndex;
  uiDirty = true;
  return ok;
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
    const int idx = displayModeIndexById(value);
    if (idx >= 0) {
      darkMode = idx == 1;
      prefs.putBool("darkMode", darkMode);
    }
  } else if (key == "dark_mode") {
    darkMode = parseBoolValue(value, darkMode);
    prefs.putBool("darkMode", darkMode);
  } else if (key == "ap_password") {
    setApPassword(value);
  } else if (key == "volume") {
    volumeLevel = static_cast<uint8_t>(boundedIntValue(value, 0, MAX_VOLUME, volumeLevel));
    prefs.putUChar("volume", volumeLevel);
  } else if (key == "autoplay") {
    autoPlay = parseBoolValue(value, autoPlay);
    prefs.putBool("autoplay", autoPlay);
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
    if (value.length()) {
      prefs.putString("ssid", value);
    }
  } else if (key == "wifi_password" || key == "wifi_pass" || key == "password") {
    prefs.putString("pass", value);
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
  } else if (key == "remote_enabled") {
    remoteEnabled = parseBoolValue(value, remoteEnabled);
    prefs.putBool("remoteEn", remoteEnabled);
  } else if (key == "remote_paired_mac") {
    remotePairedMac = value;
    remoteLoadPeerFromConfig();
    prefs.putString("remoteMac", remotePairedMac);
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
  darkMode = prefs.getBool("darkMode", true);
  apPassword = prefs.getString("apPass", DEFAULT_AP_PASSWORD);
  if (!validApPassword(apPassword)) {
    apPassword = DEFAULT_AP_PASSWORD;
  }
  volumeLevel = prefs.getUChar("volume", DEFAULT_VOLUME);
  if (volumeLevel > MAX_VOLUME) {
    volumeLevel = DEFAULT_VOLUME;
  }
  autoPlay = prefs.getBool("autoplay", true);
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
  remoteEnabled = prefs.getBool("remoteEn", true);
  remotePairedMac = prefs.getString("remoteMac", "");
  remoteLoadPeerFromConfig();
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

  if (sdReady && SD_MMC.exists(CONFIG_FILE)) {
    File f = SD_MMC.open(CONFIG_FILE, FILE_READ);
    if (f) {
      const String text = f.readString();
      f.close();
      applyConfigText(text);
      logf("Loaded config from SD: dark=%d volume=%u autoplay=%d led=%d/%u%%",
           darkMode ? 1 : 0,
           volumeLevel,
           autoPlay ? 1 : 0,
           ledEnabled ? 1 : 0,
           ledBrightness);
    }
  }
}

void saveRuntimeConfig(bool immediate) {
  storeRuntimeConfigToPrefs();

  if (!sdReady) {
    configDirty = false;
    setStatus("Cannot save config: SD missing");
    return;
  }
  if (!immediate && (!configDirty || millis() - configDirtyMs < CONFIG_SAVE_DELAY_MS)) {
    return;
  }
  File f = SD_MMC.open(CONFIG_FILE, "w");
  if (!f) {
    logf("Cannot write %s", CONFIG_FILE);
    configDirty = false;
    return;
  }
  f.println("# ESP32 WiFi Radio config");
  f.println("# WiFi values are plain text on SD. Leave wifi_ssid empty to keep NVS/AP settings.");
  f.println("# dark_mode: 1 for dark UI, 0 for light UI");
  f.println("# ap_password: setup AP password, 8..63 characters");
  f.println("# led effects: off, solid, breathe, blink, vu");
  f.println("# startup_station: last or station index");
  f.println("# safe ranges: volume 0..21, led_brightness 0..100, retry 5..120 s, timeout 5..60 s");
  f.println("# battery_scale_permille default 2000 means ADC voltage times 2.000");
  f.println("# remote_paired_mac is written by the ESP32-C6 pilot pairing flow");
  f.print("dark_mode=");
  f.println(darkMode ? 1 : 0);
  f.print("ap_password=");
  f.println(apPassword);
  f.print("volume=");
  f.println(volumeLevel);
  f.print("autoplay=");
  f.println(autoPlay ? 1 : 0);
  f.print("startup_station=");
  if (startupStation < 0) {
    f.println("last");
  } else {
    f.println(startupStation);
  }
  f.print("wifi_ssid=");
  f.println(prefs.getString("ssid", ""));
  f.print("wifi_password=");
  f.println(prefs.getString("pass", ""));
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
  f.print("remote_enabled=");
  f.println(remoteEnabled ? 1 : 0);
  f.print("remote_paired_mac=");
  f.println(remotePairedMac);
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
  logf("Saved %s", CONFIG_FILE);
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
  volumeLevel = prefs.getUChar("volume", DEFAULT_VOLUME);
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
  server.on("/ota", HTTP_GET, handleOtaPage);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.on("/settings", HTTP_POST, handleSettingsSave);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/stations", HTTP_POST, handleStationsSave);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/clearwifi", HTTP_POST, handleClearWifi);
  server.on("/apoff", HTTP_POST, handleApOff);
  server.on("/remote/pair", HTTP_POST, handleRemotePair);
  server.on("/remote/forget", HTTP_POST, handleRemoteForget);
  server.onNotFound(handleNotFound);
}

void startPortal() {
  if (apActive) {
    return;
  }
  WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  const bool ok = WiFi.softAP(apName, apPassword.c_str());
  apActive = ok;
  if (ok) {
    dnsServer.start(DNS_PORT, "*", AP_IP);
    if (!portalStarted) {
      setupPortalRoutes();
      server.begin();
      portalStarted = true;
    }
    setStatus("AP %s pass %s", apName, apPassword.c_str());
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
  const String ssid = prefs.getString("ssid", "");
  const String pass = prefs.getString("pass", "");
  if (!ssid.length()) {
    setStatus("No WiFi config");
    return false;
  }

  if (showUi) {
    setStatus("WiFi connecting: %s", ssid.c_str());
  }
  setLedMode(LedMode::Wifi);
  WiFi.mode(apActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < wifiConnectTimeoutMs) {
    updateLed();
    serviceLvgl();
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
  prefs.remove("ssid");
  prefs.remove("pass");
  markConfigDirty();
  saveRuntimeConfig(true);
  WiFi.disconnect(true);
  stopAudio();
  setStatus("WiFi config cleared");
  startPortal();
}

void handleRoot() {
  String html;
  html.reserve(18000);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP32 WiFi Radio</title><style>");
  html += F("body{font-family:system-ui,Segoe UI,sans-serif;margin:20px;background:#101418;color:#e8eef2}");
  html += F("input,textarea,button,select{box-sizing:border-box;width:100%;font:inherit;margin:6px 0;padding:10px;border-radius:6px;border:1px solid #53616b;background:#172027;color:#fff}");
  html += F("button{background:#1b7f72;border:0;font-weight:700}.danger{background:#b23b3b}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.muted{color:#aab6bd;font-size:.92rem}");
  html += F("textarea{height:190px;font-family:ui-monospace,Consolas,monospace}.card{max-width:760px;margin:auto}");
  html += F("</style></head><body><main class='card'>");
  html += F("<h1>ESP32 WiFi Radio</h1><p class='muted'>Version ");
  html += APP_VERSION;
  html += F("<br>AP: ");
  html += htmlEscape(String(apName));
  html += F("<br>AP IP: ");
  html += AP_IP.toString();
  html += F("<br>AP password: ");
  html += htmlEscape(apPassword);
  html += F("<br>Network: ");
  html += htmlEscape(networkText());
  html += F("<br>SD: ");
  html += sdReady ? F("mounted") : F("not mounted");
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
  html += F("<br>Pilot: ");
  html += htmlEscape(remoteStatusText());
  html += F("</p><h2>WiFi</h2><form method='post' action='/wifi'>");
  html += F("<input name='ssid' placeholder='SSID' value='");
  html += htmlEscape(prefs.getString("ssid", ""));
  html += F("'><input name='pass' placeholder='Password' type='password' value='");
  html += htmlEscape(prefs.getString("pass", ""));
  html += F("'><button>Save WiFi and connect</button></form>");
  html += F("<h2>Settings</h2><form method='post' action='/settings'><label><input name='darkmode' type='checkbox' style='width:auto' ");
  html += darkMode ? F("checked") : F("");
  html += F("> Dark mode</label><label class='muted'>AP password</label><input name='appass' minlength='8' maxlength='63' value='");
  html += htmlEscape(apPassword);
  html += F("'><input name='volume' type='number' min='0' max='21' value='");
  html += String(volumeLevel);
  html += F("'><label><input name='autoplay' type='checkbox' style='width:auto' ");
  html += autoPlay ? F("checked") : F("");
  html += F("> Autoplay after boot</label><br><label><input name='startap' type='checkbox' style='width:auto' ");
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
  html += F("> 24-hour clock</label><h2>Pilot</h2><label><input name='remoteenabled' type='checkbox' style='width:auto' ");
  html += remoteEnabled ? F("checked") : F("");
  html += F("> ESP-NOW pilot enabled</label><h2>Battery</h2><label><input name='batenabled' type='checkbox' style='width:auto' ");
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
  html += F("<h2>ESP32-C6 Pilot</h2><p class='muted'>Status: ");
  html += htmlEscape(remoteStatusText());
  html += F("<br>Radio MAC: ");
  html += htmlEscape(WiFi.macAddress());
  html += F("<br>Channel: ");
  html += String(wifiPrimaryChannel());
  html += F("</p><div class='row'><form method='post' action='/remote/pair'><button>Pair for 120 s</button></form>");
  html += F("<form method='post' action='/remote/forget'><button class='danger'>Forget pilot</button></form></div>");
  const uint32_t sketchSize = ESP.getSketchSize();
  const uint32_t sketchSpace = sketchSize + ESP.getFreeSketchSpace();
  const uint32_t heapSize = ESP.getHeapSize();
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t psramSize = ESP.getPsramSize();
  html += F("<h2>Radio resources</h2><p class='muted'>CPU: ");
  html += String(ESP.getCpuFreqMHz());
  html += F(" MHz / loop load ");
  html += String(cpuMainLoadPermille / 10);
  html += F(".");
  html += String(cpuMainLoadPermille % 10);
  html += F("% / max loop ");
  html += String(cpuLoopMaxUs / 1000.0f, 2);
  html += F(" ms<br>Flash chip: ");
  html += String(ESP.getFlashChipSize() / 1024UL);
  html += F(" KB / sketch ");
  html += String(sketchSize / 1024UL);
  html += F(" KB");
  if (sketchSpace) {
    html += F(" / app ");
    html += String((sketchSize * 100UL) / sketchSpace);
    html += F("%");
  }
  html += F("<br>RAM heap: ");
  html += String((heapSize - freeHeap) / 1024UL);
  html += F(" KB used / ");
  html += String(heapSize / 1024UL);
  html += F(" KB total / min free ");
  html += String(ESP.getMinFreeHeap() / 1024UL);
  html += F(" KB");
  if (psramSize) {
    html += F("<br>PSRAM: ");
    html += String((psramSize - ESP.getFreePsram()) / 1024UL);
    html += F(" KB used / ");
    html += String(psramSize / 1024UL);
    html += F(" KB total");
  }
  html += F("</p>");
  html += F("<h2>OTA</h2><p class='muted'>Upload a compiled firmware .bin. The radio screen shows progress.</p>");
  html += F("<form method='get' action='/ota'><button>Open OTA update</button></form>");
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
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");
  if (ssid.length()) {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    markConfigDirty();
    saveRuntimeConfig(true);
    server.send(200, "text/html", "<p>Saved. Connecting...</p><p><a href='/'>Back</a></p>");
    connectPending = true;
  } else {
    server.send(400, "text/plain", "Missing SSID");
  }
}

void handleStationsSave() {
  const String text = server.arg("stations");
  const bool wroteSd = saveStationsText(text);
  String body = "<p>Stations saved ";
  body += wroteSd ? "to SD and NVS." : "to NVS. SD not available or write failed.";
  body += "</p><p><a href='/'>Back</a></p>";
  server.send(200, "text/html", body);
  setStatus("Stations saved: %u", stationCount);
}

void handleReboot() {
  server.send(200, "text/html", "<p>Rebooting...</p>");
  delay(250);
  ESP.restart();
}

void handleClearWifi() {
  server.send(200, "text/html", "<p>WiFi cleared. AP remains active.</p><p><a href='/'>Back</a></p>");
  clearWifi();
}

void handleApOff() {
  server.send(200, "text/html", "<p>AP off.</p>");
  delay(100);
  stopPortal();
}

void handleRemotePair() {
  remoteBeginPairing();
  server.send(200, "text/html", "<p>Pairing window started for 120 seconds.</p><p><a href='/'>Back</a></p>");
}

void handleRemoteForget() {
  remoteForgetPeer();
  server.send(200, "text/html", "<p>Pilot pairing cleared.</p><p><a href='/'>Back</a></p>");
}

void handleOtaPage() {
  String html;
  html.reserve(1800);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP32 WiFi Radio OTA</title><style>body{font-family:system-ui,Segoe UI,sans-serif;margin:20px;background:#101418;color:#e8eef2}");
  html += F("input,button{box-sizing:border-box;width:100%;font:inherit;margin:8px 0;padding:10px;border-radius:6px;border:1px solid #53616b;background:#172027;color:#fff}");
  html += F("button{background:#1b7f72;border:0;font-weight:700}.card{max-width:620px;margin:auto}.muted{color:#aab6bd}</style></head><body><main class='card'>");
  html += F("<h1>ESP32 WiFi Radio OTA</h1><p class='muted'>Upload firmware .bin. Do not power off during update.</p>");
  html += F("<form method='post' action='/ota' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><button>Upload firmware</button></form>");
  html += F("<p><a href='/'>Back</a></p></main></body></html>");
  server.send(200, "text/html", html);
}

void handleOtaDone() {
  const bool ok = !Update.hasError();
  String body = ok ? "<p>OTA complete. Rebooting...</p>" : "<p>OTA failed. Check UART logs.</p>";
  body += "<p><a href='/'>Back</a></p>";
  server.send(ok ? 200 : 500, "text/html", body);
  if (ok) {
    drawOtaProgress("Rebooting", 100, false);
    delay(500);
    ESP.restart();
  } else {
    otaInProgress = false;
    drawOtaProgress("OTA failed", otaProgressPercent, true);
  }
}

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaInProgress = true;
    otaProgressPercent = 0;
    snprintf(otaStatusText, sizeof(otaStatusText), "Receiving %s", upload.filename.c_str());
    stopAudio();
    drawOtaProgress(otaStatusText, 0, false);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(DEBUG_PORT);
      drawOtaProgress("Cannot start OTA", 0, true);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(DEBUG_PORT);
      drawOtaProgress("OTA write failed", otaProgressPercent, true);
      return;
    }
    const size_t updateSize = Update.size();
    if (updateSize > 0) {
      uint8_t next = static_cast<uint8_t>((Update.progress() * 100ULL) / updateSize);
      if (next > 99) {
        next = 99;
      }
      if (next >= otaProgressPercent + 3) {
        otaProgressPercent = next;
        snprintf(otaStatusText, sizeof(otaStatusText), "Written %u KB", static_cast<unsigned>(Update.progress() / 1024U));
        drawOtaProgress(otaStatusText, otaProgressPercent, false);
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      otaProgressPercent = 100;
      drawOtaProgress("OTA complete", 100, false);
      logf("OTA complete: %u bytes", static_cast<unsigned>(upload.totalSize));
    } else {
      Update.printError(DEBUG_PORT);
      drawOtaProgress("OTA end failed", otaProgressPercent, true);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaInProgress = false;
    drawOtaProgress("OTA aborted", otaProgressPercent, true);
  }
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
  darkMode = server.hasArg("darkmode");
  if (server.hasArg("appass")) {
    const bool wasActive = apActive;
    setApPassword(server.arg("appass"));
    if (wasActive) {
      stopPortal();
      startPortal();
    }
  }
  if (server.hasArg("volume")) {
    volumeLevel = static_cast<uint8_t>(boundedIntValue(server.arg("volume"), 0, MAX_VOLUME, volumeLevel));
    audio.setVolume(volumeLevel);
    setEs8311Muted(volumeLevel == 0);
  }
  autoPlay = server.hasArg("autoplay");
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
  remoteEnabled = server.hasArg("remoteenabled");
  if (remoteEnabled) {
    initRemoteLink();
  } else {
    remotePairingMode = false;
  }
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
  saveRuntimeConfig(true);
  invalidateUi(true);
  server.send(200, "text/html", "<p>Settings saved.</p><p><a href='/'>Back</a></p>");
  setStatus("Settings saved");
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
  loadStations();
  if (oldName.length()) {
    for (uint8_t i = 0; i < stationCount; i++) {
      if (stations[i].name == oldName) {
        currentStation = i;
        break;
      }
    }
  }
  setStatus("Stations loaded: %u", stationCount);
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
    toggleDarkMode();
  } else if (inRect(x, y, 12, 232, 104, 32)) {
    remoteBeginPairing();
  } else if (inRect(x, y, 124, 232, 104, 32)) {
    clearWifi();
  } else if (inRect(x, y, 12, 276, 216, 34)) {
    uiScreen = UiScreen::Main;
    invalidateUi(true);
  }
}

void handleTouch() {
  if (lvglReady) {
    return;
  }
  if (!touchReady || millis() - lastTouchMs < touchDebounceMs) {
    return;
  }

  uint16_t x = 0;
  uint16_t y = 0;
  if (!touch.read(x, y)) {
    return;
  }
  lastTouchMs = millis();
  if (wakeScreenSaver()) {
    return;
  }
  logf("touch %u %u", x, y);

  if (uiScreen == UiScreen::Menu) {
    handleMenuTouch(x, y);
  } else {
    handleMainTouch(x, y);
  }
}

void processSerialCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (!cmd.length()) {
    return;
  }
  if (cmd == "help" || cmd == "menu" || cmd == "?") {
    DEBUG_PORT.println("Commands: help, ap, apoff, reconnect, play, stop, next, prev, reload, saveconfig, dark [on/off], clearwifi, pairremote, unpairremote, remote, vol N, station N, list, status, unmute, toneon, toneoff, i2slog on/off, codecdebug on/off, codecsummary, codec16, codec32, amp0, amp1, codecdump, reboot");
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
  } else if (cmd == "saveconfig") {
    saveRuntimeConfig(true);
    DEBUG_PORT.println("Config saved");
  } else if (cmd == "dark") {
    toggleDarkMode();
  } else if (cmd.startsWith("dark ")) {
    darkMode = parseBoolValue(cmd.substring(5), darkMode);
    prefs.putBool("darkMode", darkMode);
    markConfigDirty();
    setStatus("Dark mode: %s", darkMode ? "on" : "off");
    invalidateUi(true);
  } else if (cmd == "clearwifi") {
    clearWifi();
  } else if (cmd == "pairremote" || cmd == "pair" || cmd == "remote pair") {
    remoteBeginPairing();
  } else if (cmd == "unpairremote" || cmd == "remote forget") {
    remoteForgetPeer();
  } else if (cmd == "remote") {
    DEBUG_PORT.printf("Pilot: %s enabled=%d ready=%d paired=%d peer=%s channel=%u radioMac=%s\n",
                      remoteStatusText().c_str(),
                      remoteEnabled ? 1 : 0,
                      remoteReady ? 1 : 0,
                      remotePeerValid ? 1 : 0,
                      remotePairedMac.length() ? remotePairedMac.c_str() : "<none>",
                      wifiPrimaryChannel(),
                      WiFi.macAddress().c_str());
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
    i2sTraceUntilMs = millis() + TRACE_AUTO_OFF_MS;
    lastI2sTraceMs = millis();
    lastI2sTraceFrames = g_i2sFrameCount;
    DEBUG_PORT.println("I2S trace ON for 60s");
  } else if (cmd == "i2slog off") {
    i2sTrace = false;
    i2sTraceUntilMs = 0;
    DEBUG_PORT.println("I2S trace OFF");
  } else if (cmd == "codecdebug on") {
    codecTrace = true;
    codecTraceUntilMs = millis() + TRACE_AUTO_OFF_MS;
    DEBUG_PORT.println("ES8311 I2C trace ON for 60s");
  } else if (cmd == "codecdebug off") {
    codecTrace = false;
    codecTraceUntilMs = 0;
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
    DEBUG_PORT.println("Unknown command. Type help.");
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
  DEBUG_PORT.printf("AP: %s, SD: %s, station: %d/%u\n", apActive ? "on" : "off", sdReady ? "ok" : "missing", currentStation, stationCount);
  DEBUG_PORT.printf("Config: dark=%d apPassLen=%u volume=%u autoplay=%d startup=%d apBoot=%d retry=%lus timeout=%lus touch=%ums\n",
                    darkMode ? 1 : 0,
                    apPassword.length(),
                    volumeLevel,
                    autoPlay ? 1 : 0,
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
  DEBUG_PORT.printf("Pilot: %s enabled=%d ready=%d paired=%d peer=%s channel=%u radioMac=%s lastSeenMs=%lu\n",
                    remoteStatusText().c_str(),
                    remoteEnabled ? 1 : 0,
                    remoteReady ? 1 : 0,
                    remotePeerValid ? 1 : 0,
                    remotePairedMac.length() ? remotePairedMac.c_str() : "<none>",
                    wifiPrimaryChannel(),
                    WiFi.macAddress().c_str(),
                    remoteLastSeenMs);
  DEBUG_PORT.printf("Resources: cpuLoad=%u.%u%% maxLoop=%.2fms heap=%lu/%lu sketch=%lu freeSketch=%lu\n",
                    cpuMainLoadPermille / 10,
                    cpuMainLoadPermille % 10,
                    cpuLoopMaxUs / 1000.0f,
                    ESP.getFreeHeap(),
                    ESP.getHeapSize(),
                    ESP.getSketchSize(),
                    ESP.getFreeSketchSpace());
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
  preferBinLoaderOnNextReset();
  initPixel();
  initApName();
  setLedMode(LedMode::Boot);

  prefs.begin("lcd-radio", false);
  loadRuntimeConfig();
  initBatteryMonitor();

  displayReady = display.begin();
  if (displayReady) {
    drawBoot("Display ok");
  } else {
    logf("Display init failed");
  }

  touchReady = touch.begin();
  logf("Touch %s", touchReady ? "ok" : "not detected");
  drawBoot(touchReady ? "Touch ok" : "Touch not detected");
  if (initLvglUi()) {
    logf("LVGL UI ready");
    lvglShowBootStep("LVGL UI ready", 20);
  } else {
    logf("LVGL UI init failed, using fallback renderer");
  }

  lvglShowBootStep("Mounting SD", 34);
  initSd();
  lvglShowBootStep(sdDataReady ? "SD data ready" : "SD data missing", 44);

  lvglShowBootStep("Loading config", 54);
  loadRuntimeConfig();
  updateSystemStatus(true);
  lvglShowBootStep("Loading stations", 64);
  loadStations();
  lvglShowBootStep(stationCount ? "Stations ready" : "No stations loaded", 72);

  lvglShowBootStep("Audio init", 80);
  initAudio();

  if (startApOnBoot) {
    lvglShowBootStep("Starting AP", 84);
    startPortal();
  }

  lvglShowBootStep("Connecting WiFi", 88);
  if (!connectWifi(true)) {
    lvglShowBootStep("Starting setup AP", 92);
    startPortal();
  }

  lvglShowBootStep("Remote pilot link", 94);
  initRemoteLink();

  if (autoPlay && WiFi.status() == WL_CONNECTED && stationCount > 0) {
    lvglShowBootStep("Starting stream", 96);
    startStation(currentStation);
  }

  lvglShowBootStep("Ready", 100);
  registerUiActivity();
  invalidateUi(true);
  drawUi();
}

void loopRadio() {
  const uint32_t loopStartUs = micros();
  audio.loop();

  serviceNetwork();
  if (otaInProgress) {
    handleSerial();
    updateLed();
    serviceLvgl();
    delay(1);
    return;
  }
  serviceRemoteLink();
  serviceConfigSave();
  updateSystemStatus(false);
  updateClock(false);
  handleSerial();
  serviceTraceTimeouts();
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

  serviceScreenSaver();
  drawUi();
  updateCpuStats(loopStartUs);
  serviceLvgl();
  delay(1);
}
