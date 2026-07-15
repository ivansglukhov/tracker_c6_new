#pragma once

#include <Arduino.h>

namespace protocol {

constexpr uint8_t VERSION = 1;

enum class Opcode : uint8_t {
  GetStatus = 0x01,
  GetSettings = 0x02,
  SetSettings = 0x03,
  SetNmeaDebug = 0x04,
  CreateTrack = 0x10,
  ListTracks = 0x11,
  OpenTransfer = 0x20,
  AckTransfer = 0x21,
  CancelTransfer = 0x22,
  StatusEvent = 0x40,
  LivePointEvent = 0x41,
  BulkChunk = 0x42,
  NmeaGgaEvent = 0x43,
  Response = 0x7F,
};

enum class Result : uint8_t {
  Ok = 0,
  InvalidFrame = 1,
  Unsupported = 2,
  InvalidValue = 3,
  StorageError = 4,
  Busy = 5,
};

#pragma pack(push, 1)
struct RequestHeader {
  uint8_t version;
  uint8_t opcode;
  uint16_t requestId;
};

struct SettingsPayload {
  uint16_t awakeTimeSec;
  uint32_t sleepTimeSec;
  uint16_t pointsBeforeSleep;
  uint8_t flags;  // bit0 screen timer, bit2 follow sleep while BLE.
};

struct ListTracksRequest {
  uint32_t afterTrackId;
};

struct TrackInfoPayload {
  uint32_t trackId;       // Zero means end of list.
  uint32_t fileSize;
  uint32_t createdEpoch;
  uint32_t flags;         // bit0 current track.
};

struct OpenTransferRequest {
  uint32_t trackId;
  uint32_t offset;
};

struct OpenTransferPayload {
  uint16_t sessionId;
  uint16_t reserved;
  uint32_t trackId;
  uint32_t fileSize;
  uint32_t nextOffset;
};

struct AckTransferPayload {
  uint16_t sessionId;
  uint16_t reserved;
  uint32_t nextOffset;
};

struct CancelTransferPayload {
  uint16_t sessionId;
};

struct BulkChunkHeader {
  uint8_t version;
  uint8_t opcode;
  uint16_t sessionId;
  uint32_t trackId;
  uint32_t offset;
  uint8_t encoding;       // 0 raw, 1 PackBits.
  uint8_t reserved;
  uint16_t payloadLength;
  uint16_t rawLength;
  uint32_t dataCrc32;
};

struct StatusPayload {
  uint8_t version;
  uint8_t opcode;
  uint16_t flags;
  uint32_t trackId;
  uint32_t pointCount;
  uint32_t distanceM;
  int32_t altitudeM;
  uint16_t awakeElapsedSec;
  uint16_t awakeTimeSec;
  uint16_t interactiveRemainingSec;
  uint8_t batteryPercent;
  uint8_t satellites;
  uint8_t wakeReason;
  uint8_t reserved;
  uint16_t batteryMillivolts;
  uint16_t cyclePointCount;
  uint16_t pointsBeforeSleep;
};

struct LivePointPayload {
  uint8_t version;
  uint8_t opcode;
  uint32_t trackId;
  uint32_t sampleId;
  uint32_t gpsEpoch;
  int32_t latitudeE7;
  int32_t longitudeE7;
  int32_t altitudeCm;
  uint8_t satellites;
  uint8_t flags;
};
#pragma pack(pop)

static_assert(sizeof(SettingsPayload) == 9, "Unexpected settings payload size");
static_assert(sizeof(StatusPayload) == 36, "Unexpected status payload size");
static_assert(sizeof(LivePointPayload) == 28, "Unexpected live point payload size");
static_assert(sizeof(TrackInfoPayload) == 16, "Unexpected track info payload size");
static_assert(sizeof(OpenTransferPayload) == 16, "Unexpected open transfer payload size");
static_assert(sizeof(BulkChunkHeader) == 22, "Unexpected bulk header size");

}  // namespace protocol
