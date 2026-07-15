#pragma once

#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

#include "tracker_types.h"

class UbloxGps {
 public:
  bool begin();
  bool poll(GpsPoint &point);
  void prepareForDeepSleep();

 private:
  void sendUbx(uint8_t messageClass, uint8_t messageId, const uint8_t *payload, uint16_t length);
  void updateCandidate();
  uint32_t timeKey();
  uint32_t epoch();
  static int64_t daysFromCivil(int year, unsigned month, unsigned day);

  HardwareSerial serial_{1};
  TinyGPSPlus parser_;
  GpsPoint candidate_{};
  uint32_t candidateKey_ = 0;
  uint32_t candidateUpdatedMs_ = 0;
  uint32_t emittedKey_ = 0;
  bool candidateReady_ = false;
};
