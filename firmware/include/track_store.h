#pragma once

#include <FS.h>
#include <Preferences.h>

#include "binary_format.h"
#include "tracker_types.h"

struct TrackFileInfo {
  uint32_t trackId = 0;
  uint32_t fileSize = 0;
  uint32_t createdEpoch = 0;
  bool current = false;
};

class TrackStore {
 public:
  bool begin(uint16_t awakeTimeSec, uint32_t sleepTimeSec);
  bool append(const GpsPoint &point, uint32_t wakeCycleId,
              uint16_t batteryMillivolts, uint8_t batteryPercent);
  bool appendBattery(uint32_t gpsEpoch, uint32_t wakeCycleId,
                     uint16_t batteryMillivolts, uint8_t batteryPercent);
  bool createNext(uint32_t createdEpoch);
  void setSchedule(uint16_t awakeTimeSec, uint32_t sleepTimeSec) {
    awakeTimeSec_ = awakeTimeSec;
    sleepTimeSec_ = sleepTimeSec;
  }
  bool nextTrack(uint32_t afterTrackId, TrackFileInfo &info);
  bool readChunk(uint32_t trackId, uint32_t offset, uint8_t *buffer, size_t capacity,
                 size_t &bytesRead, uint32_t &fileSize);

  bool ready() const { return ready_; }
  bool writeError() const { return writeError_; }
  bool repairedTail() const { return repairedTail_; }
  uint32_t trackId() const { return trackId_; }
  uint32_t pointCount() const { return pointCount_; }
  uint32_t distanceM() const { return cumulativeDistanceM_; }
  const char *path() const { return path_; }

 private:
  bool createFile(uint32_t trackId, uint32_t createdEpoch, uint16_t awakeTimeSec, uint32_t sleepTimeSec);
  bool recoverTail();
  bool repairToSize(size_t validSize);
  bool readLastPoint(track_format::Record &record);
  bool appendRecord(track_format::Record &record);
  void updatePath();
  static void pathFor(uint32_t trackId, char *path, size_t pathSize);
  static uint32_t recordCrc(const track_format::Record &record);
  static uint32_t headerCrc(const track_format::FileHeader &header);
  static uint32_t distanceBetweenM(int32_t lat1E7, int32_t lon1E7, int32_t lat2E7, int32_t lon2E7);

  Preferences preferences_;
  bool ready_ = false;
  bool writeError_ = false;
  bool repairedTail_ = false;
  uint16_t awakeTimeSec_ = 120;
  uint32_t sleepTimeSec_ = 60;
  uint32_t trackId_ = 1;
  uint32_t pointCount_ = 0;
  uint32_t cumulativeDistanceM_ = 0;
  bool hasLastPoint_ = false;
  int32_t lastLatitudeE7_ = 0;
  int32_t lastLongitudeE7_ = 0;
  char path_[32] = {};
};
