#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "../shared/radio_remote_protocol.h"

static constexpr int LCD_DC = 15;
static constexpr int LCD_CS = 14;
static constexpr int LCD_SCK = 1;
static constexpr int LCD_MOSI = 2;
static constexpr int LCD_RST = 22;
static constexpr int LCD_BL = 23;
static constexpr int TOUCH_SDA = 18;
static constexpr int TOUCH_SCL = 19;
static constexpr int TOUCH_RST = 20;
static constexpr int TOUCH_INT = 21;
static constexpr int BATTERY_ADC = 0;
static constexpr uint16_t BATTERY_MIN_MV = 3300;
static constexpr uint16_t BATTERY_MAX_MV = 4200;
static constexpr uint16_t BATTERY_SCALE_PERMILLE = 2000;
static constexpr uint16_t STATUS_TIMEOUT_MS = 10000;
static constexpr uint16_t PING_MS = 2000;
static constexpr uint16_t CHANNEL_SCAN_MS = 350;
static constexpr uint16_t TOUCH_DEBOUNCE_MS = 180;
static constexpr uint32_t PILOT_SAVER_MS = 20000UL;
static constexpr uint32_t PILOT_SCREEN_OFF_MS = 60000UL;
static constexpr uint32_t PILOT_SAVER_REFRESH_MS = 60000UL;
static constexpr char PILOT_OTA_SSID[] = "ESP32RadioPilot-OTA";
static constexpr char PILOT_OTA_PASS[] = "pilot1234";

static const uint8_t BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

Arduino_DataBus *bus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  LCD_RST,
  0,
  false,
  172,
  320,
  34,
  0,
  34,
  0
);

Preferences prefs;
WebServer otaServer(80);
RadioRemotePacket rxPacket;
uint8_t rxMac[6] = {0};
volatile bool rxPending = false;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

uint8_t radioMac[6] = {0};
bool paired = false;
bool espNowReady = false;
bool uiDirty = true;
bool uiFullRedraw = true;
bool uiLayoutDrawn = false;
bool uiLayoutPaired = false;
bool lastLinkUi = false;
bool otaActive = false;
bool otaRoutesReady = false;
enum class ScreenMode : uint8_t {
  Active,
  Saver,
  Off
};
ScreenMode screenMode = ScreenMode::Active;
bool backlightEnabled = true;
uint8_t radioChannel = 1;
uint8_t scanChannel = 1;
uint16_t seqNo = 0;
uint32_t lastSeenMs = 0;
uint32_t lastPingMs = 0;
uint32_t lastScanMs = 0;
uint32_t lastTouchMs = 0;
uint32_t lastUiActivityMs = 0;
uint32_t lastSaverRefreshMs = 0;
uint32_t lastBatteryMs = 0;
uint8_t pilotBatteryPercent = 0;
uint16_t pilotBatteryMv = 0;
bool pilotBatteryValid = false;
uint8_t otaProgressPercent = 0;
char otaStatus[72] = "OTA idle";
RadioRemotePacket currentStatus;

volatile bool touchIntFlag = false;
struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t C_BG = 0xEF7D;
static constexpr uint16_t C_BLACK = 0x0000;
static constexpr uint16_t C_CARD = 0xFFFF;
static constexpr uint16_t C_LINE = 0xC618;
static constexpr uint16_t C_TEXT = 0x2104;
static constexpr uint16_t C_MUTED = 0x7BEF;
static constexpr uint16_t C_BLUE = 0x04BF;
static constexpr uint16_t C_GREEN = 0x07E0;
static constexpr uint16_t C_RED = 0xF800;
static constexpr uint16_t C_AMBER = 0xFD20;

void lcdRegInit() {
  static const uint8_t initOperations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,
    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,
    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,
    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,
    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,
    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,
    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,
    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,
    DELAY, 10,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
  bus->batchOperation(initOperations, sizeof(initOperations));
}

void IRAM_ATTR touchIsr() {
  touchIntFlag = true;
}

bool touchReadReg(uint8_t reg, uint8_t *data, uint8_t length) {
  Wire.beginTransmission(0x63);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(0x63, length) != length) {
    return false;
  }
  Wire.readBytes(data, length);
  return true;
}

void initTouch() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  pinMode(TOUCH_RST, OUTPUT);
  pinMode(TOUCH_INT, INPUT_PULLUP);
  digitalWrite(TOUCH_RST, LOW);
  delay(200);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);
  attachInterrupt(TOUCH_INT, touchIsr, FALLING);
}

bool readTouch(TouchPoint &point) {
  if (!touchIntFlag) {
    return false;
  }
  touchIntFlag = false;
  uint8_t data[14] = {0};
  if (!touchReadReg(0x01, data, sizeof(data)) || data[1] == 0) {
    return false;
  }
  uint16_t rawX = (static_cast<uint16_t>(data[2] & 0x0F) << 8) | data[3];
  uint16_t rawY = (static_cast<uint16_t>(data[4] & 0x0F) << 8) | data[5];
  point.x = gfx->width() - 1 - rawX;
  point.y = rawY;
  if (point.x >= gfx->width() || point.y >= gfx->height()) {
    return false;
  }
  return true;
}

bool macEmpty(const uint8_t mac[6]) {
  const uint8_t empty[6] = {0};
  return memcmp(mac, empty, 6) == 0;
}

String macText(const uint8_t mac[6]) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(text);
}

bool linkOnline() {
  return paired && lastSeenMs && millis() - lastSeenMs < STATUS_TIMEOUT_MS;
}

void invalidateUi(bool fullRedraw = false) {
  uiDirty = true;
  if (fullRedraw) {
    uiFullRedraw = true;
  }
}

void setBacklight(bool enabled) {
  if (backlightEnabled == enabled) {
    return;
  }
  backlightEnabled = enabled;
  digitalWrite(LCD_BL, enabled ? HIGH : LOW);
}

void noteUiActivity() {
  lastUiActivityMs = millis();
  if (screenMode != ScreenMode::Active) {
    screenMode = ScreenMode::Active;
    setBacklight(true);
    uiLayoutDrawn = false;
    invalidateUi(true);
  }
}

bool wakeDisplayOnly() {
  if (screenMode == ScreenMode::Active) {
    noteUiActivity();
    return false;
  }
  noteUiActivity();
  return true;
}

void serviceScreenPower() {
  if (otaActive) {
    screenMode = ScreenMode::Active;
    setBacklight(true);
    return;
  }
  const uint32_t now = millis();
  if (screenMode == ScreenMode::Active && now - lastUiActivityMs >= PILOT_SAVER_MS) {
    screenMode = ScreenMode::Saver;
    setBacklight(true);
    uiLayoutDrawn = false;
    lastSaverRefreshMs = 0;
    invalidateUi(true);
  } else if (screenMode == ScreenMode::Saver && now - lastUiActivityMs >= PILOT_SCREEN_OFF_MS) {
    screenMode = ScreenMode::Off;
    setBacklight(false);
    invalidateUi(false);
  } else if (screenMode == ScreenMode::Saver && now - lastSaverRefreshMs >= PILOT_SAVER_REFRESH_MS) {
    invalidateUi(false);
  }
}

uint8_t batteryPercentFromMv(uint16_t mv) {
  if (mv <= BATTERY_MIN_MV) {
    return 0;
  }
  if (mv >= BATTERY_MAX_MV) {
    return 100;
  }
  return ((static_cast<uint32_t>(mv - BATTERY_MIN_MV) * 100U) + ((BATTERY_MAX_MV - BATTERY_MIN_MV) / 2U)) / (BATTERY_MAX_MV - BATTERY_MIN_MV);
}

void updateBattery(bool force = false) {
  const uint32_t now = millis();
  if (!force && now - lastBatteryMs < 5000) {
    return;
  }
  lastBatteryMs = now;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(BATTERY_ADC);
    delay(1);
  }
  const uint16_t adcMv = sum / 8;
  pilotBatteryMv = (static_cast<uint32_t>(adcMv) * BATTERY_SCALE_PERMILLE + 500U) / 1000U;
  pilotBatteryValid = pilotBatteryMv >= 2500 && pilotBatteryMv <= 6500;
  pilotBatteryPercent = pilotBatteryValid ? batteryPercentFromMv(pilotBatteryMv) : 0;
  invalidateUi(false);
}

void setChannel(uint8_t channel) {
  if (channel < 1 || channel > 13) {
    channel = 1;
  }
  radioChannel = channel;
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

bool addPeer(const uint8_t mac[6]) {
  if (!espNowReady || macEmpty(mac)) {
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
  return err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST;
}

void savePairing() {
  prefs.putBool("paired", paired);
  prefs.putBytes("radioMac", radioMac, 6);
  prefs.putUChar("channel", radioChannel);
}

void loadPairing() {
  paired = prefs.getBool("paired", false);
  prefs.getBytes("radioMac", radioMac, 6);
  radioChannel = prefs.getUChar("channel", 1);
  if (macEmpty(radioMac)) {
    paired = false;
  }
}

void fillOutgoing(RadioRemotePacket &packet, uint8_t type, uint8_t command) {
  radioRemoteClearPacket(packet);
  packet.type = type;
  packet.seq = ++seqNo;
  packet.command = command;
  packet.channel = radioChannel;
  packet.pilotBatteryValid = pilotBatteryValid ? 1 : 0;
  packet.pilotBatteryPercent = pilotBatteryPercent;
  WiFi.macAddress(packet.pilotMac);
  memcpy(packet.radioMac, radioMac, 6);
}

bool sendPacket(uint8_t type, uint8_t command = RADIO_REMOTE_CMD_NONE) {
  if (!espNowReady || !paired || macEmpty(radioMac)) {
    return false;
  }
  addPeer(radioMac);
  RadioRemotePacket packet;
  fillOutgoing(packet, type, command);
  return esp_now_send(radioMac, reinterpret_cast<uint8_t *>(&packet), sizeof(packet)) == ESP_OK;
}

void sendPairRequest(const uint8_t dest[6]) {
  if (!espNowReady || macEmpty(dest)) {
    return;
  }
  addPeer(dest);
  RadioRemotePacket packet;
  fillOutgoing(packet, RADIO_REMOTE_PAIR_REQUEST, RADIO_REMOTE_CMD_NONE);
  memcpy(packet.radioMac, dest, 6);
  esp_now_send(dest, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!info || !info->src_addr || !data || len != static_cast<int>(sizeof(RadioRemotePacket))) {
    return;
  }
  RadioRemotePacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (!radioRemotePacketValid(packet)) {
    return;
  }

  portENTER_CRITICAL(&rxMux);
  memcpy(&rxPacket, &packet, sizeof(rxPacket));
  memcpy(rxMac, info->src_addr, 6);
  rxPending = true;
  portEXIT_CRITICAL(&rxMux);
}

void initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  setChannel(radioChannel);
  if (esp_now_init() == ESP_OK) {
    espNowReady = true;
    esp_now_register_recv_cb(onReceive);
    addPeer(BROADCAST_MAC);
    if (paired) {
      addPeer(radioMac);
    }
  }
}

void handleIncoming(const RadioRemotePacket &packet, const uint8_t mac[6]) {
  if (packet.channel >= 1 && packet.channel <= 13) {
    setChannel(packet.channel);
  }

  if (packet.type == RADIO_REMOTE_PAIR_ADVERT && !paired) {
    memcpy(radioMac, macEmpty(packet.radioMac) ? mac : packet.radioMac, 6);
    addPeer(radioMac);
    sendPairRequest(radioMac);
    invalidateUi(false);
    return;
  }

  if ((packet.type == RADIO_REMOTE_PAIR_ACK || packet.type == RADIO_REMOTE_STATUS) && !macEmpty(mac)) {
    const bool wasPaired = paired;
    memcpy(radioMac, mac, 6);
    paired = true;
    savePairing();
    addPeer(radioMac);
    if (!wasPaired) {
      invalidateUi(true);
    }
  }

  if (paired && memcmp(mac, radioMac, 6) == 0 &&
      (packet.type == RADIO_REMOTE_STATUS || packet.type == RADIO_REMOTE_PAIR_ACK)) {
    memcpy(&currentStatus, &packet, sizeof(currentStatus));
    lastSeenMs = millis();
    invalidateUi(false);
  }
}

void serviceRadioLink() {
  if (rxPending) {
    RadioRemotePacket packet;
    uint8_t mac[6];
    portENTER_CRITICAL(&rxMux);
    memcpy(&packet, &rxPacket, sizeof(packet));
    memcpy(mac, rxMac, 6);
    rxPending = false;
    portEXIT_CRITICAL(&rxMux);
    handleIncoming(packet, mac);
  }

  const uint32_t now = millis();
  if (!paired && now - lastScanMs >= CHANNEL_SCAN_MS) {
    lastScanMs = now;
    scanChannel++;
    if (scanChannel > 13) {
      scanChannel = 1;
    }
    setChannel(scanChannel);
  }

  if (paired && !linkOnline() && now - lastScanMs >= CHANNEL_SCAN_MS) {
    lastScanMs = now;
    scanChannel++;
    if (scanChannel > 13) {
      scanChannel = 1;
    }
    setChannel(scanChannel);
  }

  if (paired && now - lastPingMs >= PING_MS) {
    lastPingMs = now;
    sendPacket(RADIO_REMOTE_PING);
  }

  const bool active = linkOnline();
  if (active != lastLinkUi) {
    lastLinkUi = active;
    invalidateUi(false);
  }
}

String clipped(String text, uint8_t chars) {
  text.trim();
  if (text.length() <= chars) {
    return text;
  }
  if (chars <= 3) {
    return text.substring(0, chars);
  }
  return text.substring(0, chars - 3) + "...";
}

void drawText(int16_t x, int16_t y, uint16_t color, uint8_t size, const String &text) {
  gfx->setTextWrap(false);
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  gfx->setCursor(x, y);
  gfx->print(text);
}

void drawBattery(int16_t x, int16_t y, uint8_t percent, bool valid) {
  uint16_t color = !valid ? C_MUTED : (percent <= 15 ? C_RED : (percent <= 35 ? C_AMBER : C_GREEN));
  gfx->drawRect(x, y, 26, 12, C_TEXT);
  gfx->fillRect(x + 26, y + 4, 2, 4, C_TEXT);
  gfx->fillRect(x + 2, y + 2, 22, 8, C_CARD);
  if (valid) {
    gfx->fillRect(x + 2, y + 2, (static_cast<uint16_t>(percent) * 22U) / 100U, 8, color);
  }
}

void drawWifi(int16_t x, int16_t y, int8_t bars, bool connected) {
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t h = 3 + i * 3;
    uint16_t color = connected && i < bars ? C_GREEN : C_LINE;
    gfx->fillRect(x + i * 5, y + 12 - h, 3, h, color);
  }
}

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const String &label, uint16_t bg, uint16_t fg) {
  gfx->fillRoundRect(x, y, w, h, 8, bg);
  gfx->drawRoundRect(x, y, w, h, 8, C_LINE);
  const int16_t tw = label.length() * 6 * 2;
  drawText(x + (w - tw) / 2, y + (h - 16) / 2, fg, 2, label);
}

void drawSmallButton(int16_t x, int16_t y, int16_t w, int16_t h, const String &label, uint16_t bg, uint16_t fg) {
  gfx->fillRoundRect(x, y, w, h, 6, bg);
  gfx->drawRoundRect(x, y, w, h, 6, C_LINE);
  const int16_t tw = label.length() * 6;
  drawText(x + (w - tw) / 2, y + (h - 8) / 2, fg, 1, label);
}

void drawOtaScreen(uint8_t percent, const char *line, bool error = false) {
  if (percent > 100) {
    percent = 100;
  }
  gfx->fillScreen(C_BG);
  gfx->fillRoundRect(8, 36, 156, 214, 12, C_CARD);
  gfx->drawRoundRect(8, 36, 156, 214, 12, C_LINE);
  drawText(24, 54, error ? C_RED : C_BLUE, 2, "OTA");
  drawText(24, 84, C_TEXT, 1, "SSID:");
  drawText(24, 100, C_TEXT, 1, PILOT_OTA_SSID);
  drawText(24, 120, C_TEXT, 1, "PASS: pilot1234");
  drawText(24, 140, C_TEXT, 1, "http://192.168.4.1");
  drawText(24, 166, error ? C_RED : C_MUTED, 1, clipped(String(line), 20));
  gfx->drawRect(24, 190, 124, 14, C_LINE);
  gfx->fillRect(26, 192, (120 * percent) / 100, 10, error ? C_RED : C_GREEN);
  drawText(24, 214, C_MUTED, 1, String(percent) + "%");
}

void drawSaverScreen() {
  gfx->fillScreen(C_BLACK);
  const String clock = currentStatus.clock[0] ? String(currentStatus.clock) : String("--:--");
  gfx->setTextWrap(false);
  gfx->setTextSize(4);
  gfx->setTextColor(currentStatus.clock[0] ? C_CARD : C_MUTED);
  gfx->setCursor((172 - static_cast<int16_t>(clock.length()) * 24) / 2, 58);
  gfx->print(clock);

  drawText(22, 122, C_BLUE, 1, clipped(currentStatus.station[0] ? String(currentStatus.station) : String("Radio"), 21));
  drawText(22, 142, linkOnline() ? C_GREEN : C_AMBER, 1, linkOnline() ? "LINK ONLINE" : (paired ? "RADIO OFFLINE" : "WAITING PAIR"));

  drawText(22, 178, C_MUTED, 1, "Radio");
  drawBattery(72, 174, currentStatus.radioBatteryPercent, currentStatus.radioBatteryValid);
  drawText(22, 208, C_MUTED, 1, "Pilot");
  drawBattery(72, 204, pilotBatteryPercent, pilotBatteryValid);
  drawText(22, 242, C_MUTED, 1, "Touch to wake");
  lastSaverRefreshMs = millis();
}

void drawMainStatic() {
  gfx->fillScreen(C_BG);
  gfx->fillRect(0, 0, 172, 34, C_CARD);
  gfx->drawFastHLine(0, 34, 172, C_LINE);
  drawText(8, 6, C_TEXT, 2, "Radio Pilot");

  gfx->fillRoundRect(8, 46, 156, 118, 10, C_CARD);
  gfx->drawRoundRect(8, 46, 156, 118, 10, C_LINE);

  if (!paired) {
    drawButton(8, 174, 156, 36, "SCAN PAIR", C_BLUE, C_CARD);
  } else {
    drawButton(8, 174, 74, 36, "Prev", C_CARD, C_TEXT);
    drawButton(90, 174, 74, 36, "Next", C_CARD, C_TEXT);
  }
  drawButton(8, 220, 74, 38, "Vol-", C_CARD, C_TEXT);
  drawButton(90, 220, 74, 38, "Vol+", C_CARD, C_TEXT);
  drawButton(8, 270, 74, 38, "Play", C_BLUE, C_CARD);
  drawButton(90, 270, 74, 38, "Stop", C_RED, C_CARD);

  uiLayoutDrawn = true;
  uiLayoutPaired = paired;
}

void drawMainDynamic() {
  gfx->fillRect(8, 22, 76, 10, C_CARD);
  gfx->fillRect(86, 6, 24, 18, C_CARD);
  gfx->fillRect(112, 6, 32, 16, C_CARD);
  gfx->fillRect(144, 8, 28, 12, C_CARD);
  drawText(8, 24, C_MUTED, 1, paired ? clipped(macText(radioMac), 17) : "not paired");
  drawWifi(86, 8, currentStatus.wifiBars, currentStatus.flags & RADIO_REMOTE_FLAG_WIFI_CONNECTED);
  drawBattery(114, 8, currentStatus.radioBatteryPercent, currentStatus.radioBatteryValid);
  drawText(146, 10, pilotBatteryValid ? C_GREEN : C_MUTED, 1, pilotBatteryValid ? String(pilotBatteryPercent) + "%" : "--");

  const bool online = linkOnline();
  gfx->fillRect(14, 54, 148, 104, C_CARD);
  drawText(18, 58, online ? C_GREEN : C_AMBER, 1, online ? "LINK ONLINE" : (paired ? "RADIO OFFLINE" : "WAITING PAIR"));
  drawText(18, 78, C_BLUE, 2, clipped(currentStatus.station[0] ? String(currentStatus.station) : String("Radio"), 11));
  drawText(18, 104, C_TEXT, 1, clipped(currentStatus.title[0] ? String(currentStatus.title) : String("Enable Pair Pilot"), 24));
  drawText(18, 126, C_MUTED, 1, String("Vol ") + String(currentStatus.volume) + "  Ch " + String(radioChannel));
  drawText(18, 142, C_MUTED, 1, String("Radio bat ") + (currentStatus.radioBatteryValid ? String(currentStatus.radioBatteryPercent) + "%" : String("--")));
  drawSmallButton(122, 132, 36, 22, "OTA", C_CARD, C_BLUE);
}

void drawUi() {
  if (!uiDirty) {
    return;
  }
  if (screenMode == ScreenMode::Off) {
    uiDirty = false;
    return;
  }
  uiDirty = false;
  if (otaActive) {
    drawOtaScreen(otaProgressPercent, otaStatus, false);
    return;
  }
  if (screenMode == ScreenMode::Saver) {
    drawSaverScreen();
    uiFullRedraw = false;
    return;
  }
  if (uiFullRedraw || !uiLayoutDrawn || uiLayoutPaired != paired) {
    drawMainStatic();
    uiFullRedraw = false;
  }
  drawMainDynamic();
}

bool inRect(uint16_t x, uint16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void handleOtaRoot() {
  String html;
  html.reserve(1500);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>ESP32 WiFi Radio Pilot OTA</title><style>body{font-family:system-ui,Segoe UI,sans-serif;margin:20px;background:#f1f5f9;color:#111827}");
  html += F("input,button{box-sizing:border-box;width:100%;font:inherit;margin:8px 0;padding:10px;border-radius:8px;border:1px solid #cbd5e1;background:#fff;color:#111827}");
  html += F("button{background:#0284c7;color:#fff;border:0;font-weight:700}.card{max-width:560px;margin:auto}.muted{color:#64748b}</style></head><body><main class='card'>");
  html += F("<h1>ESP32 WiFi Radio Pilot OTA</h1><p class='muted'>Upload firmware .bin. Watch the pilot screen for progress.</p>");
  html += F("<form method='post' action='/update' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><button>Upload</button></form>");
  html += F("</main></body></html>");
  otaServer.send(200, "text/html", html);
}

void handleOtaDone() {
  const bool ok = !Update.hasError();
  otaServer.send(ok ? 200 : 500, "text/html", ok ? "<p>OTA complete. Rebooting...</p>" : "<p>OTA failed.</p>");
  if (ok) {
    drawOtaScreen(100, "Rebooting", false);
    delay(500);
    ESP.restart();
  } else {
    drawOtaScreen(otaProgressPercent, "OTA failed", true);
  }
}

void handleOtaUpload() {
  HTTPUpload &upload = otaServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaProgressPercent = 0;
    snprintf(otaStatus, sizeof(otaStatus), "Receiving %s", upload.filename.c_str());
    drawOtaScreen(0, otaStatus, false);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      drawOtaScreen(0, "Cannot start", true);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      drawOtaScreen(otaProgressPercent, "Write failed", true);
      return;
    }
    if (Update.size() > 0) {
      uint8_t next = static_cast<uint8_t>((Update.progress() * 100ULL) / Update.size());
      if (next > 99) {
        next = 99;
      }
      if (next >= otaProgressPercent + 4) {
        otaProgressPercent = next;
        snprintf(otaStatus, sizeof(otaStatus), "%u KB", static_cast<unsigned>(Update.progress() / 1024U));
        drawOtaScreen(otaProgressPercent, otaStatus, false);
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      otaProgressPercent = 100;
      drawOtaScreen(100, "Complete", false);
      Serial.printf("OTA complete: %u bytes\n", static_cast<unsigned>(upload.totalSize));
    } else {
      Update.printError(Serial);
      drawOtaScreen(otaProgressPercent, "End failed", true);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    drawOtaScreen(otaProgressPercent, "Aborted", true);
  }
}

void startOtaPortal() {
  if (otaActive) {
    return;
  }
  otaActive = true;
  screenMode = ScreenMode::Active;
  setBacklight(true);
  otaProgressPercent = 0;
  snprintf(otaStatus, sizeof(otaStatus), "Waiting upload");
  uiLayoutDrawn = false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(PILOT_OTA_SSID, PILOT_OTA_PASS, radioChannel);
  if (!otaRoutesReady) {
    otaServer.on("/", HTTP_GET, handleOtaRoot);
    otaServer.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);
    otaRoutesReady = true;
  }
  otaServer.begin();
  invalidateUi(true);
}

void handleTouch() {
  if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) {
    return;
  }
  TouchPoint point;
  if (!readTouch(point)) {
    return;
  }
  lastTouchMs = millis();
  if (wakeDisplayOnly()) {
    return;
  }

  if (inRect(point.x, point.y, 122, 132, 36, 22)) {
    startOtaPortal();
    return;
  }

  if (!paired) {
    if (inRect(point.x, point.y, 8, 174, 156, 36)) {
      prefs.clear();
      memset(radioMac, 0, sizeof(radioMac));
      paired = false;
      lastSeenMs = 0;
      invalidateUi(true);
    }
    return;
  }

  if (inRect(point.x, point.y, 8, 174, 74, 36)) {
    sendPacket(RADIO_REMOTE_COMMAND, RADIO_REMOTE_CMD_PREV);
  } else if (inRect(point.x, point.y, 90, 174, 74, 36)) {
    sendPacket(RADIO_REMOTE_COMMAND, RADIO_REMOTE_CMD_NEXT);
  } else if (inRect(point.x, point.y, 8, 220, 74, 38)) {
    sendPacket(RADIO_REMOTE_COMMAND, RADIO_REMOTE_CMD_VOL_DOWN);
  } else if (inRect(point.x, point.y, 90, 220, 74, 38)) {
    sendPacket(RADIO_REMOTE_COMMAND, RADIO_REMOTE_CMD_VOL_UP);
  } else if (inRect(point.x, point.y, 8, 270, 74, 38)) {
    sendPacket(RADIO_REMOTE_COMMAND, RADIO_REMOTE_CMD_PLAY);
  } else if (inRect(point.x, point.y, 90, 270, 74, 38)) {
    sendPacket(RADIO_REMOTE_COMMAND, RADIO_REMOTE_CMD_STOP);
  }
}

void setup() {
  Serial.begin(115200);
  prefs.begin("radio-pilot", false);
  loadPairing();
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC, ADC_11db);

  if (!gfx->begin()) {
    Serial.println("gfx begin failed");
  }
  lcdRegInit();
  gfx->setRotation(0);
  pinMode(LCD_BL, OUTPUT);
  backlightEnabled = true;
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(C_BG);
  initTouch();
  updateBattery(true);
  initEspNow();
  radioRemoteClearPacket(currentStatus);
  snprintf(currentStatus.clock, sizeof(currentStatus.clock), "--:--");
  snprintf(currentStatus.station, sizeof(currentStatus.station), "Radio");
  snprintf(currentStatus.title, sizeof(currentStatus.title), "Use Pair Pilot");
  lastUiActivityMs = millis();
  invalidateUi(true);
}

void loop() {
  if (otaActive) {
    otaServer.handleClient();
    drawUi();
    delay(5);
    return;
  }
  serviceRadioLink();
  updateBattery(false);
  handleTouch();
  serviceScreenPower();
  drawUi();
  delay(5);
}
