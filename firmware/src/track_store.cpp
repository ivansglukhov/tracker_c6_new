#include "../include/track_store.h"

#include <SD.h>
#include <SPI.h>
#include <math.h>

#include "../include/board_pins.h"
#include "../include/crc32.h"

namespace {
constexpr double DEG_TO_RAD_D = 0.017453292519943295769;
constexpr double EARTH_RADIUS_M = 6371000.0;
}

uint32_t TrackStore::headerCrc(const track_format::FileHeader &header) {
  return trackerCrc32(&header, offsetof(track_format::FileHeader, crc32));
}

uint32_t TrackStore::recordCrc(const track_format::Record &record) {
  return trackerCrc32(&record, offsetof(track_format::Record, crc32));
}

void TrackStore::updatePath() {
  pathFor(trackId_, path_, sizeof(path_));
}

void TrackStore::pathFor(uint32_t trackId, char *path, size_t pathSize) {
  snprintf(path, pathSize, "/track_%06lu.c6t", static_cast<unsigned long>(trackId));
}

bool TrackStore::begin(uint16_t awakeTimeSec, uint32_t sleepTimeSec) {
  awakeTimeSec_ = awakeTimeSec;
  sleepTimeSec_ = sleepTimeSec;
  pinMode(board::SD_CS, OUTPUT);
  digitalWrite(board::SD_CS, HIGH);
  SPI.begin(board::SD_SCK, board::SD_MISO, board::SD_MOSI);
  if (!SD.begin(board::SD_CS, SPI)) return false;
  if (!preferences_.begin("c6v2track", false)) return false;

  trackId_ = preferences_.getUInt("track_id", 1);
  if (trackId_ == 0) trackId_ = 1;
  updatePath();
  if (!SD.exists(path_)) {
    if (!createFile(trackId_, 0, awakeTimeSec_, sleepTimeSec_)) return false;
  }
  ready_ = recoverTail();
  return ready_;
}

bool TrackStore::createFile(uint32_t trackId, uint32_t createdEpoch, uint16_t awakeTimeSec, uint32_t sleepTimeSec) {
  trackId_ = trackId;
  updatePath();
  if (SD.exists(path_)) return false;

  track_format::FileHeader header{};
  header.magic = track_format::FILE_MAGIC;
  header.version = track_format::VERSION;
  header.headerSize = sizeof(header);
  header.trackId = trackId_;
  header.deviceId = ESP.getEfuseMac();
  header.createdEpoch = createdEpoch;
  header.awakeTimeSec = awakeTimeSec;
  header.sleepTimeSec = sleepTimeSec;
  header.crc32 = headerCrc(header);

  File file = SD.open(path_, FILE_WRITE);
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  file.flush();
  file.close();
  if (!ok) {
    SD.remove(path_);
    return false;
  }
  preferences_.putUInt("track_id", trackId_);
  pointCount_ = 0;
  cumulativeDistanceM_ = 0;
  hasLastPoint_ = false;
  ready_ = true;
  return true;
}

bool TrackStore::recoverTail() {
  File file = SD.open(path_, FILE_READ);
  if (!file) return false;
  track_format::FileHeader header{};
  if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header) ||
      header.magic != track_format::FILE_MAGIC || header.version != track_format::VERSION ||
      header.trackId != trackId_ || header.crc32 != headerCrc(header)) {
    file.close();
    return false;
  }

  const size_t fileSize = file.size();
  size_t validSize = sizeof(header);
  track_format::Record candidate{};
  while (validSize + sizeof(candidate) <= fileSize) {
    if (file.read(reinterpret_cast<uint8_t *>(&candidate), sizeof(candidate)) != sizeof(candidate) ||
        candidate.magic != track_format::RECORD_MAGIC || candidate.version != track_format::VERSION ||
        candidate.crc32 != recordCrc(candidate) ||
        (candidate.type != static_cast<uint8_t>(track_format::RecordType::Point) &&
         candidate.type != static_cast<uint8_t>(track_format::RecordType::Footer) &&
         candidate.type != static_cast<uint8_t>(track_format::RecordType::Battery))) break;
    validSize += sizeof(candidate);
  }
  file.close();

  if (validSize != fileSize && !repairToSize(validSize)) return false;

  track_format::Record record{};
  if (readLastPoint(record)) {
    pointCount_ = record.sequence;
    cumulativeDistanceM_ = record.cumulativeDistanceM;
    lastLatitudeE7_ = record.latitudeE7;
    lastLongitudeE7_ = record.longitudeE7;
    hasLastPoint_ = true;
  }
  return true;
}

bool TrackStore::repairToSize(size_t validSize) {
  constexpr char TEMP_PATH[] = "/c6t_repair.tmp";
  SD.remove(TEMP_PATH);
  File source = SD.open(path_, FILE_READ);
  File target = SD.open(TEMP_PATH, FILE_WRITE);
  if (!source || !target) {
    if (source) source.close();
    if (target) target.close();
    SD.remove(TEMP_PATH);
    return false;
  }
  uint8_t buffer[256] = {};
  size_t remaining = validSize;
  bool ok = true;
  while (remaining > 0) {
    const size_t requested = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
    const size_t read = source.read(buffer, requested);
    if (read != requested || target.write(buffer, read) != read) {
      ok = false;
      break;
    }
    remaining -= read;
  }
  target.flush();
  source.close();
  target.close();
  if (!ok || !SD.remove(path_) || !SD.rename(TEMP_PATH, path_)) {
    SD.remove(TEMP_PATH);
    return false;
  }
  repairedTail_ = true;
  return true;
}

bool TrackStore::readLastPoint(track_format::Record &record) {
  File file = SD.open(path_, FILE_READ);
  if (!file) return false;
  size_t offset = file.size();
  while (offset >= sizeof(track_format::FileHeader) + sizeof(record)) {
    offset -= sizeof(record);
    if (!file.seek(offset) || file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record)) break;
    if (record.magic == track_format::RECORD_MAGIC && record.version == track_format::VERSION &&
        record.crc32 == recordCrc(record) &&
        record.type == static_cast<uint8_t>(track_format::RecordType::Point)) {
      file.close();
      return true;
    }
  }
  file.close();
  return false;
}

uint32_t TrackStore::distanceBetweenM(int32_t lat1E7, int32_t lon1E7, int32_t lat2E7, int32_t lon2E7) {
  const double lat1 = lat1E7 / 10000000.0 * DEG_TO_RAD_D;
  const double lat2 = lat2E7 / 10000000.0 * DEG_TO_RAD_D;
  const double deltaLat = lat2 - lat1;
  const double deltaLon = (lon2E7 - lon1E7) / 10000000.0 * DEG_TO_RAD_D;
  const double a = sin(deltaLat / 2.0) * sin(deltaLat / 2.0) +
                   cos(lat1) * cos(lat2) * sin(deltaLon / 2.0) * sin(deltaLon / 2.0);
  return static_cast<uint32_t>(lround(EARTH_RADIUS_M * 2.0 * atan2(sqrt(a), sqrt(1.0 - a))));
}

bool TrackStore::appendRecord(track_format::Record &record) {
  record.crc32 = recordCrc(record);
  File file = SD.open(path_, FILE_APPEND);
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  file.close();
  return ok;
}

bool TrackStore::append(const GpsPoint &point, uint32_t wakeCycleId,
                        uint16_t batteryMillivolts, uint8_t batteryPercent) {
  if (!ready_) return false;
  uint32_t nextDistance = cumulativeDistanceM_;
  if (hasLastPoint_) {
    nextDistance += distanceBetweenM(lastLatitudeE7_, lastLongitudeE7_, point.latitudeE7, point.longitudeE7);
  }

  track_format::Record record{};
  record.magic = track_format::RECORD_MAGIC;
  record.version = track_format::VERSION;
  record.type = static_cast<uint8_t>(track_format::RecordType::Point);
  record.sequence = pointCount_ + 1;
  record.gpsEpoch = point.epoch;
  record.latitudeE7 = point.latitudeE7;
  record.longitudeE7 = point.longitudeE7;
  record.altitudeCm = point.altitudeCm;
  record.speedCms = point.speedCms;
  record.courseCdeg = point.courseCdeg;
  record.hdopCenti = point.hdopCenti;
  record.satellites = point.satellites;
  record.flags = point.flags;
  record.wakeCycleId = wakeCycleId;
  record.cumulativeDistanceM = nextDistance;
  record.reserved = track_format::packBattery(batteryMillivolts, batteryPercent);

  if (!appendRecord(record)) {
    writeError_ = true;
    return false;
  }
  pointCount_ = record.sequence;
  cumulativeDistanceM_ = nextDistance;
  lastLatitudeE7_ = point.latitudeE7;
  lastLongitudeE7_ = point.longitudeE7;
  hasLastPoint_ = true;
  return true;
}

bool TrackStore::appendBattery(uint32_t gpsEpoch, uint32_t wakeCycleId,
                               uint16_t batteryMillivolts, uint8_t batteryPercent) {
  if (!ready_) return false;
  track_format::Record record{};
  record.magic = track_format::RECORD_MAGIC;
  record.version = track_format::VERSION;
  record.type = static_cast<uint8_t>(track_format::RecordType::Battery);
  record.sequence = pointCount_;
  record.gpsEpoch = gpsEpoch;
  record.wakeCycleId = wakeCycleId;
  record.cumulativeDistanceM = cumulativeDistanceM_;
  record.reserved = track_format::packBattery(batteryMillivolts, batteryPercent);
  if (appendRecord(record)) return true;
  writeError_ = true;
  return false;
}

bool TrackStore::createNext(uint32_t createdEpoch) {
  if (!ready_) return false;
  track_format::Record footer{};
  footer.magic = track_format::RECORD_MAGIC;
  footer.version = track_format::VERSION;
  footer.type = static_cast<uint8_t>(track_format::RecordType::Footer);
  footer.sequence = pointCount_;
  footer.gpsEpoch = createdEpoch;
  footer.cumulativeDistanceM = cumulativeDistanceM_;
  if (!appendRecord(footer)) return false;

  uint32_t candidate = trackId_ + 1;
  char candidatePath[32] = {};
  while (candidate != 0) {
    pathFor(candidate, candidatePath, sizeof(candidatePath));
    if (!SD.exists(candidatePath)) break;
    ++candidate;
  }
  if (candidate == 0) return false;
  return createFile(candidate, createdEpoch, awakeTimeSec_, sleepTimeSec_);
}

bool TrackStore::nextTrack(uint32_t afterTrackId, TrackFileInfo &info) {
  info = {};
  if (!ready_) return false;
  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }

  uint32_t bestId = UINT32_MAX;
  uint32_t bestSize = 0;
  uint32_t bestCreatedEpoch = 0;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      const char *name = file.name();
      const char *base = strrchr(name, '/');
      base = base ? base + 1 : name;
      if (strncmp(base, "track_", 6) == 0) {
        char *suffix = nullptr;
        const unsigned long parsed = strtoul(base + 6, &suffix, 10);
        if (parsed > afterTrackId && parsed < bestId && suffix && strcmp(suffix, ".c6t") == 0) {
          track_format::FileHeader header{};
          if (file.seek(0) && file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
              header.magic == track_format::FILE_MAGIC && header.version == track_format::VERSION &&
              header.trackId == parsed && header.crc32 == headerCrc(header)) {
            bestId = static_cast<uint32_t>(parsed);
            bestSize = static_cast<uint32_t>(file.size());
            bestCreatedEpoch = header.createdEpoch;
          }
        }
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();

  if (bestId == UINT32_MAX) return true;
  info.trackId = bestId;
  info.fileSize = bestSize;
  info.createdEpoch = bestCreatedEpoch;
  info.current = bestId == trackId_;
  return true;
}

bool TrackStore::readChunk(uint32_t trackId, uint32_t offset, uint8_t *buffer, size_t capacity,
                           size_t &bytesRead, uint32_t &fileSize) {
  bytesRead = 0;
  fileSize = 0;
  if (!ready_ || !buffer || capacity == 0 || trackId == 0) return false;
  char filePath[32] = {};
  pathFor(trackId, filePath, sizeof(filePath));
  File file = SD.open(filePath, FILE_READ);
  if (!file) return false;
  fileSize = static_cast<uint32_t>(file.size());
  if (offset > fileSize || !file.seek(offset)) {
    file.close();
    return false;
  }
  const size_t available = fileSize - offset;
  const size_t requested = available < capacity ? available : capacity;
  bytesRead = requested == 0 ? 0 : file.read(buffer, requested);
  file.close();
  return bytesRead == requested;
}
