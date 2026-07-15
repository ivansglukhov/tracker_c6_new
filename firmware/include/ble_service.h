#pragma once

#include <Arduino.h>

#include "binary_protocol.h"
#include "tracker_types.h"

class BLECharacteristic;

struct BleCommandFrame {
  uint8_t bytes[64] = {};
  uint8_t length = 0;
};

class BleService {
 public:
  bool begin();
  bool pop(BleCommandFrame &frame);
  bool connected() const { return connected_; }
  void notifyStatus(const DeviceStatus &status);
  void notifyPoint(uint32_t trackId, uint32_t sampleId, const GpsPoint &point);
  void notifyNmeaGga(const char *sentence, size_t length);
  bool notifyBulk(const protocol::BulkChunkHeader &header, const uint8_t *data);
  void respond(uint16_t requestId, protocol::Result result, const void *payload = nullptr, size_t payloadSize = 0);
  void stop();

  void setConnected(bool connected) { connected_ = connected; }
  void enqueue(const uint8_t *data, size_t size);

 private:
  volatile bool connected_ = false;
  bool started_ = false;
  void *queue_ = nullptr;
  BLECharacteristic *eventCharacteristic_ = nullptr;
  BLECharacteristic *bulkCharacteristic_ = nullptr;
};
