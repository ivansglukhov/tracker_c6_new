#include "../include/ble_service.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
constexpr char DEVICE_NAME[] = "C6 Tracker v2";
constexpr char SERVICE_UUID[] = "6f4c2000-b9a8-4e31-9d7a-c6c6c6c62000";
constexpr char CONTROL_UUID[] = "6f4c2001-b9a8-4e31-9d7a-c6c6c6c62000";
constexpr char EVENT_UUID[] = "6f4c2002-b9a8-4e31-9d7a-c6c6c6c62000";
constexpr char BULK_UUID[] = "6f4c2003-b9a8-4e31-9d7a-c6c6c6c62000";
BleService *activeService = nullptr;

class ServerCallbacks final : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    if (activeService) activeService->setConnected(true);
  }
  void onDisconnect(BLEServer *) override {
    if (activeService) activeService->setConnected(false);
    BLEDevice::startAdvertising();
  }
};

class ControlCallbacks final : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    if (!activeService) return;
    const String value = characteristic->getValue();
    activeService->enqueue(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
  }
};

ServerCallbacks serverCallbacks;
ControlCallbacks controlCallbacks;
}

bool BleService::begin() {
  if (started_) return true;
  queue_ = xQueueCreate(8, sizeof(BleCommandFrame));
  if (!queue_) return false;
  activeService = this;
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(247);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *control = service->createCharacteristic(
      CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  control->setCallbacks(&controlCallbacks);
  eventCharacteristic_ = service->createCharacteristic(
      EVENT_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  eventCharacteristic_->addDescriptor(new BLE2902());
  bulkCharacteristic_ = service->createCharacteristic(BULK_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  bulkCharacteristic_->addDescriptor(new BLE2902());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  started_ = true;
  return true;
}

void BleService::enqueue(const uint8_t *data, size_t size) {
  if (!queue_ || !data || size == 0 || size > 64) return;
  BleCommandFrame frame{};
  frame.length = static_cast<uint8_t>(size);
  memcpy(frame.bytes, data, size);
  xQueueSend(static_cast<QueueHandle_t>(queue_), &frame, 0);
}

bool BleService::pop(BleCommandFrame &frame) {
  return queue_ && xQueueReceive(static_cast<QueueHandle_t>(queue_), &frame, 0) == pdTRUE;
}

void BleService::notifyStatus(const DeviceStatus &status) {
  if (!eventCharacteristic_) return;
  protocol::StatusPayload payload{};
  payload.version = protocol::VERSION;
  payload.opcode = static_cast<uint8_t>(protocol::Opcode::StatusEvent);
  if (status.gpsCoordinate) payload.flags |= 0x0001;
  if (status.sdReady) payload.flags |= 0x0002;
  if (status.sdError) payload.flags |= 0x0004;
  if (status.bleConnected) payload.flags |= 0x0008;
  if (status.usbConnected) payload.flags |= 0x0010;
  payload.trackId = status.trackId;
  payload.pointCount = status.pointCount;
  payload.distanceM = status.distanceM;
  payload.altitudeM = status.altitudeM;
  payload.awakeElapsedSec = status.awakeElapsedSec;
  payload.awakeTimeSec = status.awakeTimeSec;
  payload.interactiveRemainingSec = status.interactiveRemainingSec;
  payload.batteryPercent = status.batteryPercent;
  payload.batteryMillivolts = status.batteryMillivolts;
  payload.satellites = status.satellites;
  payload.wakeReason = status.wakeReason;
  payload.cyclePointCount = status.cyclePointCount;
  payload.pointsBeforeSleep = status.pointsBeforeSleep;
  eventCharacteristic_->setValue(reinterpret_cast<uint8_t *>(&payload), sizeof(payload));
  if (connected_) eventCharacteristic_->notify();
}

void BleService::notifyNmeaGga(const char *sentence, size_t length) {
  if (!eventCharacteristic_ || !connected_ || !sentence || length == 0 || length > 180) return;
  uint8_t frame[182] = {};
  frame[0] = protocol::VERSION;
  frame[1] = static_cast<uint8_t>(protocol::Opcode::NmeaGgaEvent);
  memcpy(frame + 2, sentence, length);
  eventCharacteristic_->setValue(frame, length + 2);
  eventCharacteristic_->notify();
}

void BleService::notifyPoint(uint32_t trackId, uint32_t sampleId, const GpsPoint &point) {
  if (!eventCharacteristic_ || !connected_) return;
  protocol::LivePointPayload payload{};
  payload.version = protocol::VERSION;
  payload.opcode = static_cast<uint8_t>(protocol::Opcode::LivePointEvent);
  payload.trackId = trackId;
  payload.sampleId = sampleId;
  payload.gpsEpoch = point.epoch;
  payload.latitudeE7 = point.latitudeE7;
  payload.longitudeE7 = point.longitudeE7;
  payload.altitudeCm = point.altitudeCm;
  payload.satellites = point.satellites;
  payload.flags = point.flags;
  eventCharacteristic_->setValue(reinterpret_cast<uint8_t *>(&payload), sizeof(payload));
  eventCharacteristic_->notify();
}

bool BleService::notifyBulk(const protocol::BulkChunkHeader &header, const uint8_t *data) {
  if (!bulkCharacteristic_ || !connected_ || header.payloadLength > 180 ||
      (header.payloadLength != 0 && !data)) return false;
  uint8_t frame[sizeof(protocol::BulkChunkHeader) + 180] = {};
  memcpy(frame, &header, sizeof(header));
  if (header.payloadLength) memcpy(frame + sizeof(header), data, header.payloadLength);
  bulkCharacteristic_->setValue(frame, sizeof(header) + header.payloadLength);
  bulkCharacteristic_->notify();
  return true;
}

void BleService::respond(uint16_t requestId, protocol::Result result, const void *payload, size_t payloadSize) {
  if (!eventCharacteristic_ || payloadSize > 56) return;
  uint8_t frame[64] = {};
  frame[0] = protocol::VERSION;
  frame[1] = static_cast<uint8_t>(protocol::Opcode::Response);
  frame[2] = static_cast<uint8_t>(requestId);
  frame[3] = static_cast<uint8_t>(requestId >> 8U);
  frame[4] = static_cast<uint8_t>(result);
  frame[5] = static_cast<uint8_t>(payloadSize);
  if (payload && payloadSize) memcpy(frame + 6, payload, payloadSize);
  eventCharacteristic_->setValue(frame, payloadSize + 6);
  if (connected_) eventCharacteristic_->notify();
}

void BleService::stop() {
  if (!started_) return;
  BLEDevice::deinit(true);
  started_ = false;
  connected_ = false;
  activeService = nullptr;
  eventCharacteristic_ = nullptr;
  bulkCharacteristic_ = nullptr;
  if (queue_) {
    vQueueDelete(static_cast<QueueHandle_t>(queue_));
    queue_ = nullptr;
  }
}
