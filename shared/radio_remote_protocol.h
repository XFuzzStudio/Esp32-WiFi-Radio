#ifndef RADIO_REMOTE_PROTOCOL_H
#define RADIO_REMOTE_PROTOCOL_H

#include <stdint.h>
#include <string.h>

static constexpr uint32_t RADIO_REMOTE_MAGIC = 0x52444D54UL;  // RDMT
static constexpr uint8_t RADIO_REMOTE_VERSION = 2;
static constexpr uint8_t RADIO_REMOTE_CLOCK_TEXT = 5;
static constexpr uint8_t RADIO_REMOTE_STATION_TEXT = 40;
static constexpr uint8_t RADIO_REMOTE_TITLE_TEXT = 72;

enum RadioRemoteType : uint8_t {
  RADIO_REMOTE_PAIR_ADVERT = 1,
  RADIO_REMOTE_PAIR_REQUEST = 2,
  RADIO_REMOTE_PAIR_ACK = 3,
  RADIO_REMOTE_STATUS = 4,
  RADIO_REMOTE_COMMAND = 5,
  RADIO_REMOTE_PING = 6,
};

enum RadioRemoteCommand : uint8_t {
  RADIO_REMOTE_CMD_NONE = 0,
  RADIO_REMOTE_CMD_PREV = 1,
  RADIO_REMOTE_CMD_NEXT = 2,
  RADIO_REMOTE_CMD_VOL_DOWN = 3,
  RADIO_REMOTE_CMD_VOL_UP = 4,
  RADIO_REMOTE_CMD_TOGGLE = 5,
  RADIO_REMOTE_CMD_PLAY = 6,
  RADIO_REMOTE_CMD_STOP = 7,
};

enum RadioRemoteFlags : uint8_t {
  RADIO_REMOTE_FLAG_WIFI_CONNECTED = 1 << 0,
  RADIO_REMOTE_FLAG_PLAYING = 1 << 1,
  RADIO_REMOTE_FLAG_SD_READY = 1 << 2,
  RADIO_REMOTE_FLAG_AP_ACTIVE = 1 << 3,
  RADIO_REMOTE_FLAG_PAIRING = 1 << 4,
  RADIO_REMOTE_FLAG_REMOTE_LINK = 1 << 5,
};

#pragma pack(push, 1)
struct RadioRemotePacket {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t seq;
  uint8_t stationIndex;
  uint8_t stationCount;
  uint8_t volume;
  uint8_t radioBatteryPercent;
  uint8_t radioBatteryValid;
  uint8_t pilotBatteryPercent;
  uint8_t pilotBatteryValid;
  int8_t wifiBars;
  int8_t wifiRssi;
  uint8_t flags;
  uint8_t command;
  uint8_t channel;
  uint8_t radioMac[6];
  uint8_t pilotMac[6];
  char clock[RADIO_REMOTE_CLOCK_TEXT + 1];
  char station[RADIO_REMOTE_STATION_TEXT + 1];
  char title[RADIO_REMOTE_TITLE_TEXT + 1];
};
#pragma pack(pop)

static inline void radioRemoteClearPacket(RadioRemotePacket &packet) {
  memset(&packet, 0, sizeof(packet));
  packet.magic = RADIO_REMOTE_MAGIC;
  packet.version = RADIO_REMOTE_VERSION;
}

static inline bool radioRemotePacketValid(const RadioRemotePacket &packet) {
  return packet.magic == RADIO_REMOTE_MAGIC && packet.version == RADIO_REMOTE_VERSION;
}

#endif  // RADIO_REMOTE_PROTOCOL_H
