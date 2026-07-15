#pragma once

#include <Arduino.h>

struct TrackerSettings {
  uint16_t awakeTimeSec = 120;
  uint32_t sleepTimeSec = 60;
  bool screenOnTimerWake = false;
  bool bleOnTimerWake = true;
  bool followSleepScheduleWhileBle = false;
};

struct GpsPoint {
  uint32_t epoch = 0;
  int32_t latitudeE7 = 0;
  int32_t longitudeE7 = 0;
  int32_t altitudeCm = 0;
  uint16_t speedCms = 0;
  uint16_t courseCdeg = 0;
  uint16_t hdopCenti = 0;
  uint8_t satellites = 0;
  uint8_t flags = 0;
};

struct DeviceStatus {
  uint32_t trackId = 0;
  uint32_t pointCount = 0;
  uint32_t distanceM = 0;
  int32_t altitudeM = 0;
  uint16_t awakeElapsedSec = 0;
  uint16_t awakeTimeSec = 0;
  uint16_t interactiveRemainingSec = 0;
  uint16_t batteryMillivolts = 0;
  uint8_t batteryPercent = 0;
  uint8_t satellites = 0;
  uint8_t wakeReason = 0;
  bool gpsCoordinate = false;
  bool sdReady = false;
  bool sdError = false;
  bool bleConnected = false;
};
