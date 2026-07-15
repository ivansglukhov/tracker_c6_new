#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <soc/usb_serial_jtag_struct.h>

#include "../include/binary_protocol.h"
#include "../include/ble_service.h"
#include "../include/board_pins.h"
#include "../include/crc32.h"
#include "../include/settings_store.h"
#include "../include/simple_display.h"
#include "../include/track_store.h"
#include "../include/ublox_gps.h"

namespace {
constexpr uint32_t INTERACTIVE_WINDOW_MS = 30000;
constexpr uint32_t POINT_STALL_TIMEOUT_MS = 30000;
constexpr uint32_t USB_ACTIVITY_HOLD_MS = 3000;
constexpr uint32_t STATUS_PERIOD_MS = 1000;
constexpr uint32_t TRANSFER_RETRY_MS = 1200;
constexpr uint8_t TRANSFER_MAX_RETRIES = 5;
constexpr size_t TRANSFER_CHUNK_SIZE = 180;

RTC_DATA_ATTR uint32_t wakeCycleId = 0;

SettingsStore settingsStore;
TrackStore trackStore;
UbloxGps gps;
SimpleDisplay display;
BleService ble;
DeviceStatus status;

TrackerSettings settings;
uint32_t bootMs = 0;
uint32_t lastStatusMs = 0;
bool interactiveWake = false;
bool displayEnabled = false;
bool bleEnabled = false;
bool displayForcedOnly = false;
bool buttonPressedPreviously = false;
uint32_t interactiveDeadlineMs = 0;
uint16_t cyclePointCount = 0;
uint32_t lastStoredPointMs = 0;
bool nmeaDebugEnabled = false;
uint16_t lastUsbFrame = 0;
uint32_t lastUsbActivityMs = 0;

struct TransferState {
  bool active = false;
  bool waitingAck = false;
  uint8_t retries = 0;
  uint16_t sessionId = 0;
  uint32_t trackId = 0;
  uint32_t fileSize = 0;
  uint32_t nextOffset = 0;
  uint32_t sentEnd = 0;
  uint32_t lastSendMs = 0;
};

TransferState transfer;
uint16_t nextTransferSession = 1;
uint32_t lastGpsEpoch = 0;
float filteredBatteryMillivolts = 0.0F;

struct BatteryReading {
  uint16_t millivolts = 0;
  uint8_t percent = 0;
};

size_t packBits(const uint8_t *input, size_t length, uint8_t *output, size_t capacity) {
  size_t source = 0;
  size_t target = 0;
  while (source < length) {
    size_t runLength = 1;
    while (source + runLength < length && input[source + runLength] == input[source] && runLength < 130) {
      ++runLength;
    }
    if (runLength >= 3) {
      if (target + 2 > capacity) return SIZE_MAX;
      output[target++] = static_cast<uint8_t>(0x80U | (runLength - 3));
      output[target++] = input[source];
      source += runLength;
      continue;
    }

    const size_t literalStart = source;
    source += runLength;
    while (source < length && source - literalStart < 128) {
      runLength = 1;
      while (source + runLength < length && input[source + runLength] == input[source] && runLength < 130) {
        ++runLength;
      }
      if (runLength >= 3) break;
      if (source - literalStart + runLength > 128) break;
      source += runLength;
    }
    const size_t literalLength = source - literalStart;
    if (target + 1 + literalLength > capacity) return SIZE_MAX;
    output[target++] = static_cast<uint8_t>(literalLength - 1);
    memcpy(output + target, input + literalStart, literalLength);
    target += literalLength;
  }
  return target;
}

bool interactiveWindowActive() {
  return interactiveDeadlineMs != 0 && static_cast<int32_t>(interactiveDeadlineMs - millis()) > 0;
}

void startInteractiveWindow() {
  interactiveDeadlineMs = millis() + INTERACTIVE_WINDOW_MS;
  if (!displayEnabled) {
    displayEnabled = display.begin();
    displayForcedOnly = displayEnabled;
  }
  if (!bleEnabled) {
    bleEnabled = ble.begin();
  }
}

void handleButtonAndInteractiveWindow() {
  const bool pressed = digitalRead(board::WAKE_BUTTON) == LOW;
  if (pressed && !buttonPressedPreviously) startInteractiveWindow();
  buttonPressedPreviously = pressed;

  if (interactiveWindowActive()) return;
  if (displayForcedOnly && displayEnabled) {
    display.off();
    displayEnabled = false;
    displayForcedOnly = false;
  }
}

bool usbHostConnected() {
  const uint16_t frame = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  if (USB_SERIAL_JTAG.bus_reset_st.usb_bus_reset_st && frame != lastUsbFrame) {
    lastUsbFrame = frame;
    lastUsbActivityMs = millis();
  }
  return lastUsbActivityMs != 0 && millis() - lastUsbActivityMs < USB_ACTIVITY_HOLD_MS;
}

BatteryReading readBattery() {
  uint32_t pinMillivolts = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    pinMillivolts += analogReadMilliVolts(board::BATTERY_ADC);
  }
  const float measuredMillivolts = (pinMillivolts / 8.0F) * board::BATTERY_SCALE;
  filteredBatteryMillivolts = filteredBatteryMillivolts < 1.0F
      ? measuredMillivolts
      : filteredBatteryMillivolts * 0.85F + measuredMillivolts * 0.15F;
  const float voltage = filteredBatteryMillivolts / 1000.0F;
  const float normalized = (voltage - board::BATTERY_EMPTY_V) /
                           (board::BATTERY_FULL_V - board::BATTERY_EMPTY_V);
  BatteryReading reading{};
  reading.millivolts = static_cast<uint16_t>(constrain(lroundf(filteredBatteryMillivolts), 0L, 65535L));
  reading.percent = static_cast<uint8_t>(constrain(lroundf(normalized * 100.0F), 0L, 100L));
  return reading;
}

void refreshStatus() {
  status.trackId = trackStore.trackId();
  status.pointCount = trackStore.pointCount();
  status.distanceM = trackStore.distanceM();
  const uint32_t waitSeconds = (millis() - bootMs) / 1000;
  status.awakeElapsedSec = static_cast<uint16_t>(waitSeconds > 65535 ? 65535 : waitSeconds);
  status.awakeTimeSec = settings.awakeTimeSec;
  status.cyclePointCount = cyclePointCount;
  status.pointsBeforeSleep = settings.pointsBeforeSleep;
  const BatteryReading battery = readBattery();
  status.batteryMillivolts = battery.millivolts;
  status.batteryPercent = battery.percent;
  status.sdReady = trackStore.ready();
  status.sdError = trackStore.writeError();
  status.bleConnected = ble.connected();
  status.usbConnected = usbHostConnected();
  if (interactiveWindowActive()) {
    status.interactiveRemainingSec = static_cast<uint16_t>((interactiveDeadlineMs - millis() + 999) / 1000);
  } else {
    status.interactiveRemainingSec = 0;
  }
  if (displayEnabled) display.update(status);
  if (bleEnabled) ble.notifyStatus(status);
}

void handleCommand(const BleCommandFrame &frame) {
  if (frame.length < sizeof(protocol::RequestHeader)) return;
  protocol::RequestHeader header{};
  memcpy(&header, frame.bytes, sizeof(header));
  if (header.version != protocol::VERSION) {
    ble.respond(header.requestId, protocol::Result::InvalidFrame);
    return;
  }
  const auto opcode = static_cast<protocol::Opcode>(header.opcode);
  if (opcode == protocol::Opcode::GetStatus) {
    refreshStatus();
    ble.respond(header.requestId, protocol::Result::Ok);
  } else if (opcode == protocol::Opcode::GetSettings) {
    protocol::SettingsPayload payload{};
    payload.awakeTimeSec = settings.awakeTimeSec;
    payload.sleepTimeSec = settings.sleepTimeSec;
    payload.pointsBeforeSleep = settings.pointsBeforeSleep;
    if (settings.screenOnTimerWake) payload.flags |= 0x01;
    if (settings.followSleepScheduleWhileBle) payload.flags |= 0x04;
    ble.respond(header.requestId, protocol::Result::Ok, &payload, sizeof(payload));
  } else if (opcode == protocol::Opcode::SetSettings) {
    if (frame.length != sizeof(header) + sizeof(protocol::SettingsPayload)) {
      ble.respond(header.requestId, protocol::Result::InvalidFrame);
      return;
    }
    protocol::SettingsPayload payload{};
    memcpy(&payload, frame.bytes + sizeof(header), sizeof(payload));
    if (payload.awakeTimeSec < 1 || payload.awakeTimeSec > 3600 ||
        payload.sleepTimeSec < 5 || payload.sleepTimeSec > 86400 ||
        payload.pointsBeforeSleep < 1 || payload.pointsBeforeSleep > 1000) {
      ble.respond(header.requestId, protocol::Result::InvalidValue);
      return;
    }
    settings.awakeTimeSec = payload.awakeTimeSec;
    settings.sleepTimeSec = payload.sleepTimeSec;
    settings.pointsBeforeSleep = payload.pointsBeforeSleep;
    settings.screenOnTimerWake = payload.flags & 0x01;
    settings.followSleepScheduleWhileBle = payload.flags & 0x04;
    trackStore.setSchedule(settings.awakeTimeSec, settings.sleepTimeSec);
    ble.respond(header.requestId,
                settingsStore.save(settings) ? protocol::Result::Ok : protocol::Result::StorageError);
  } else if (opcode == protocol::Opcode::SetNmeaDebug) {
    if (frame.length != sizeof(header) + 1) {
      ble.respond(header.requestId, protocol::Result::InvalidFrame);
      return;
    }
    nmeaDebugEnabled = frame.bytes[sizeof(header)] != 0;
    ble.respond(header.requestId, protocol::Result::Ok);
  } else if (opcode == protocol::Opcode::CreateTrack) {
    ble.respond(header.requestId,
                trackStore.createNext(lastGpsEpoch) ? protocol::Result::Ok : protocol::Result::StorageError);
    refreshStatus();
  } else if (opcode == protocol::Opcode::ListTracks) {
    if (frame.length != sizeof(header) + sizeof(protocol::ListTracksRequest)) {
      ble.respond(header.requestId, protocol::Result::InvalidFrame);
      return;
    }
    protocol::ListTracksRequest request{};
    memcpy(&request, frame.bytes + sizeof(header), sizeof(request));
    TrackFileInfo info{};
    if (!trackStore.nextTrack(request.afterTrackId, info)) {
      ble.respond(header.requestId, protocol::Result::StorageError);
      return;
    }
    protocol::TrackInfoPayload payload{};
    payload.trackId = info.trackId;
    payload.fileSize = info.fileSize;
    payload.createdEpoch = info.createdEpoch;
    if (info.current) payload.flags |= 0x01;
    ble.respond(header.requestId, protocol::Result::Ok, &payload, sizeof(payload));
  } else if (opcode == protocol::Opcode::OpenTransfer) {
    if (frame.length != sizeof(header) + sizeof(protocol::OpenTransferRequest)) {
      ble.respond(header.requestId, protocol::Result::InvalidFrame);
      return;
    }
    protocol::OpenTransferRequest request{};
    memcpy(&request, frame.bytes + sizeof(header), sizeof(request));
    uint8_t probe = 0;
    size_t probeSize = 0;
    uint32_t fileSize = 0;
    if (!trackStore.readChunk(request.trackId, request.offset, &probe, 1, probeSize, fileSize) ||
        request.offset > fileSize) {
      ble.respond(header.requestId, protocol::Result::InvalidValue);
      return;
    }
    transfer = {};
    transfer.active = true;
    transfer.sessionId = nextTransferSession++;
    if (transfer.sessionId == 0) transfer.sessionId = nextTransferSession++;
    transfer.trackId = request.trackId;
    transfer.fileSize = fileSize;
    transfer.nextOffset = request.offset;
    protocol::OpenTransferPayload payload{};
    payload.sessionId = transfer.sessionId;
    payload.trackId = transfer.trackId;
    payload.fileSize = transfer.fileSize;
    payload.nextOffset = transfer.nextOffset;
    ble.respond(header.requestId, protocol::Result::Ok, &payload, sizeof(payload));
  } else if (opcode == protocol::Opcode::AckTransfer) {
    if (frame.length != sizeof(header) + sizeof(protocol::AckTransferPayload)) {
      ble.respond(header.requestId, protocol::Result::InvalidFrame);
      return;
    }
    protocol::AckTransferPayload payload{};
    memcpy(&payload, frame.bytes + sizeof(header), sizeof(payload));
    if (!transfer.active || payload.sessionId != transfer.sessionId || !transfer.waitingAck ||
        payload.nextOffset != transfer.sentEnd) {
      ble.respond(header.requestId, protocol::Result::InvalidValue);
      return;
    }
    transfer.nextOffset = payload.nextOffset;
    transfer.waitingAck = false;
    transfer.retries = 0;
    if (transfer.nextOffset >= transfer.fileSize) transfer.active = false;
    ble.respond(header.requestId, protocol::Result::Ok);
  } else if (opcode == protocol::Opcode::CancelTransfer) {
    if (frame.length != sizeof(header) + sizeof(protocol::CancelTransferPayload)) {
      ble.respond(header.requestId, protocol::Result::InvalidFrame);
      return;
    }
    protocol::CancelTransferPayload payload{};
    memcpy(&payload, frame.bytes + sizeof(header), sizeof(payload));
    if (transfer.active && payload.sessionId == transfer.sessionId) transfer = {};
    ble.respond(header.requestId, protocol::Result::Ok);
  } else {
    ble.respond(header.requestId, protocol::Result::Unsupported);
  }
}

void serviceTransfer() {
  if (!transfer.active || !ble.connected() || transfer.nextOffset >= transfer.fileSize) return;
  if (transfer.waitingAck && millis() - transfer.lastSendMs < TRANSFER_RETRY_MS) return;
  if (transfer.waitingAck && transfer.retries >= TRANSFER_MAX_RETRIES) {
    transfer = {};
    return;
  }

  uint8_t data[TRANSFER_CHUNK_SIZE] = {};
  uint8_t compressed[TRANSFER_CHUNK_SIZE] = {};
  size_t bytesRead = 0;
  uint32_t currentFileSize = 0;
  const uint32_t remaining = transfer.fileSize - transfer.nextOffset;
  const size_t capacity = remaining < sizeof(data) ? remaining : sizeof(data);
  if (!trackStore.readChunk(transfer.trackId, transfer.nextOffset, data, capacity, bytesRead, currentFileSize) ||
      bytesRead == 0 || currentFileSize < transfer.fileSize) {
    transfer = {};
    return;
  }

  protocol::BulkChunkHeader header{};
  header.version = protocol::VERSION;
  header.opcode = static_cast<uint8_t>(protocol::Opcode::BulkChunk);
  header.sessionId = transfer.sessionId;
  header.trackId = transfer.trackId;
  header.offset = transfer.nextOffset;
  const size_t compressedSize = packBits(data, bytesRead, compressed, sizeof(compressed));
  const bool useCompressed = compressedSize != SIZE_MAX && compressedSize < bytesRead;
  header.encoding = useCompressed ? 1 : 0;
  header.payloadLength = static_cast<uint16_t>(useCompressed ? compressedSize : bytesRead);
  header.rawLength = static_cast<uint16_t>(bytesRead);
  header.dataCrc32 = trackerCrc32(data, bytesRead);
  if (!ble.notifyBulk(header, useCompressed ? compressed : data)) return;
  transfer.waitingAck = true;
  transfer.sentEnd = transfer.nextOffset + bytesRead;
  transfer.lastSendMs = millis();
  ++transfer.retries;
}

void enterDeepSleep() {
  if (digitalRead(board::WAKE_BUTTON) == LOW) return;
  if (usbHostConnected()) return;
  refreshStatus();
  trackStore.appendBattery(lastGpsEpoch, wakeCycleId,
                           status.batteryMillivolts, status.batteryPercent);
  if (displayEnabled) display.off();
  if (bleEnabled) ble.stop();
  gps.prepareForDeepSleep();
  SD.end();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(settings.sleepTimeSec) * 1000000ULL);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << board::WAKE_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
  Serial.flush();
  esp_deep_sleep_start();
}

bool sleepAllowed() {
  if (usbHostConnected()) return false;
  if (interactiveWindowActive()) return false;
  if (ble.connected() && !settings.followSleepScheduleWhileBle) return false;
  if (cyclePointCount == 0) return millis() - bootMs >= settings.awakeTimeSec * 1000UL;
  if (cyclePointCount >= settings.pointsBeforeSleep) return true;
  return millis() - lastStoredPointMs >= POINT_STALL_TIMEOUT_MS;
}
}

void setup() {
  Serial.begin(115200);
  delay(50);
  bootMs = millis();
  ++wakeCycleId;
  if (wakeCycleId == 0) ++wakeCycleId;

  pinMode(board::WAKE_BUTTON, INPUT);
  lastUsbFrame = USB_SERIAL_JTAG.fram_num.sof_frame_index;
  analogReadResolution(12);
  analogSetPinAttenuation(board::BATTERY_ADC, ADC_11db);
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  interactiveWake = wakeCause != ESP_SLEEP_WAKEUP_TIMER;
  status.wakeReason = wakeCause == ESP_SLEEP_WAKEUP_TIMER ? 1 :
                      wakeCause == ESP_SLEEP_WAKEUP_GPIO ? 2 : 3;

  settingsStore.begin();
  settings = settingsStore.get();
  displayEnabled = interactiveWake || settings.screenOnTimerWake;
  displayForcedOnly = interactiveWake;
  if (interactiveWake) interactiveDeadlineMs = millis() + INTERACTIVE_WINDOW_MS;

  if (displayEnabled) display.begin();
  trackStore.begin(settings.awakeTimeSec, settings.sleepTimeSec);
  gps.begin();
  bleEnabled = ble.begin();
  refreshStatus();
  Serial.printf("C6 Tracker v2: wake=%u track=%lu SD=%u\n", status.wakeReason,
                static_cast<unsigned long>(trackStore.trackId()), trackStore.ready());
}

void loop() {
  handleButtonAndInteractiveWindow();
  GpsPoint point{};
  if (gps.poll(point)) {
    if (point.epoch) lastGpsEpoch = point.epoch;
    if (trackStore.append(point, wakeCycleId,
                          status.batteryMillivolts, status.batteryPercent)) {
      if (cyclePointCount < UINT16_MAX) ++cyclePointCount;
      lastStoredPointMs = millis();
      status.gpsCoordinate = true;
      status.altitudeM = point.altitudeCm / 100;
      status.satellites = point.satellites;
      if (bleEnabled) ble.notifyPoint(trackStore.trackId(), trackStore.pointCount(), point);
    }
    refreshStatus();
  }

  char gga[128] = {};
  size_t ggaLength = 0;
  if (gps.takeGga(gga, sizeof(gga), ggaLength) &&
      nmeaDebugEnabled && bleEnabled && ble.connected()) {
    ble.notifyNmeaGga(gga, ggaLength);
  }

  if (nmeaDebugEnabled && !ble.connected()) nmeaDebugEnabled = false;

  BleCommandFrame command{};
  while (bleEnabled && ble.pop(command)) handleCommand(command);
  if (bleEnabled) serviceTransfer();

  if (millis() - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = millis();
    refreshStatus();
  }

  if (sleepAllowed()) enterDeepSleep();
  delay(5);
}
