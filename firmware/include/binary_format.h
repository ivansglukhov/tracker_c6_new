#pragma once

#include <Arduino.h>

namespace track_format {

constexpr uint32_t FILE_MAGIC = 0x32543643U;  // "C6T2", little-endian.
constexpr uint16_t RECORD_MAGIC = 0xC62AU;
constexpr uint16_t VERSION = 1;

enum class RecordType : uint8_t {
  Point = 1,
  Footer = 2,
};

#pragma pack(push, 1)
struct FileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint32_t trackId;
  uint64_t deviceId;
  uint32_t createdEpoch;
  uint32_t awakeTimeSec;
  uint32_t sleepTimeSec;
  uint32_t flags;
  uint32_t crc32;
};

struct Record {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t sequence;
  uint32_t gpsEpoch;
  int32_t latitudeE7;
  int32_t longitudeE7;
  int32_t altitudeCm;
  uint16_t speedCms;
  uint16_t courseCdeg;
  uint16_t hdopCenti;
  uint8_t satellites;
  uint8_t flags;
  uint32_t wakeCycleId;
  uint32_t cumulativeDistanceM;
  uint32_t reserved;
  uint32_t crc32;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 40, "Unexpected C6T2 file header size");
static_assert(sizeof(Record) == 48, "Unexpected C6T2 record size");

}  // namespace track_format
