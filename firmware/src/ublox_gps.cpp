#include "../include/ublox_gps.h"

#include "../include/board_pins.h"
#include <driver/gpio.h>

namespace {
}

bool UbloxGps::begin() {
  gpio_hold_dis(static_cast<gpio_num_t>(board::GPS_TX));
  pinMode(board::GPS_TX, OUTPUT);
  digitalWrite(board::GPS_TX, HIGH);
  serial_.begin(board::GPS_BAUD, SERIAL_8N1, board::GPS_RX, board::GPS_TX);
  serial_.write(0xFF);  // UART RX edge wakes an M10 receiver.
  serial_.flush();
  return true;
}

void UbloxGps::sendUbx(uint8_t messageClass, uint8_t messageId, const uint8_t *payload, uint16_t length) {
  uint8_t ckA = 0;
  uint8_t ckB = 0;
  const auto writeChecked = [&](uint8_t value) {
    serial_.write(value);
    ckA = static_cast<uint8_t>(ckA + value);
    ckB = static_cast<uint8_t>(ckB + ckA);
  };
  serial_.write(0xB5);
  serial_.write(0x62);
  writeChecked(messageClass);
  writeChecked(messageId);
  writeChecked(static_cast<uint8_t>(length));
  writeChecked(static_cast<uint8_t>(length >> 8U));
  for (uint16_t index = 0; index < length; ++index) writeChecked(payload[index]);
  serial_.write(ckA);
  serial_.write(ckB);
  serial_.flush();
}

void UbloxGps::prepareForDeepSleep() {
  uint8_t payload[16] = {};
  payload[8] = 0x06;   // backup + force minimum consumption
  payload[12] = 0x08;  // wake on UART RX edge
  sendUbx(0x02, 0x41, payload, sizeof(payload));
  delay(20);
  // UART TX already idles HIGH. Switch it to a held GPIO before ending the
  // peripheral so detaching UART cannot create an edge that wakes the M10.
  pinMode(board::GPS_TX, OUTPUT);
  digitalWrite(board::GPS_TX, HIGH);
  gpio_hold_en(static_cast<gpio_num_t>(board::GPS_TX));
  serial_.end();
}

void UbloxGps::captureNmea(char value) {
  if (value == '$') {
    sentenceLength_ = 0;
    capturingSentence_ = true;
  }
  if (!capturingSentence_) return;
  if (value == '\r') return;
  if (value == '\n') {
    sentence_[sentenceLength_] = '\0';
    if (sentenceLength_ >= 6 && strstr(sentence_, "GGA,") != nullptr) {
      latestGgaLength_ = sentenceLength_;
      memcpy(latestGga_, sentence_, latestGgaLength_ + 1);
      ggaReady_ = true;
    }
    sentenceLength_ = 0;
    capturingSentence_ = false;
    return;
  }
  if (sentenceLength_ + 1 >= sizeof(sentence_)) {
    sentenceLength_ = 0;
    capturingSentence_ = false;
    return;
  }
  sentence_[sentenceLength_++] = value;
}

bool UbloxGps::takeGga(char *output, size_t capacity, size_t &length) {
  length = 0;
  if (!output || capacity == 0 || !ggaReady_) return false;
  length = latestGgaLength_ < capacity - 1 ? latestGgaLength_ : capacity - 1;
  memcpy(output, latestGga_, length);
  output[length] = '\0';
  ggaReady_ = false;
  return true;
}

int64_t UbloxGps::daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097LL + static_cast<int64_t>(dayOfEra) - 719468LL;
}

uint32_t UbloxGps::epoch() {
  if (!parser_.date.isValid() || !parser_.time.isValid()) return 0;
  const int64_t seconds = daysFromCivil(parser_.date.year(), parser_.date.month(), parser_.date.day()) * 86400LL +
                          parser_.time.hour() * 3600LL + parser_.time.minute() * 60LL + parser_.time.second();
  if (seconds <= 0 || seconds > UINT32_MAX) return 0;
  return static_cast<uint32_t>(seconds);
}

uint32_t UbloxGps::timeKey() {
  const uint32_t gpsEpoch = epoch();
  if (gpsEpoch != 0) return gpsEpoch;
  if (!parser_.time.isValid()) return 0;
  return parser_.time.hour() * 3600UL + parser_.time.minute() * 60UL + parser_.time.second() + 1UL;
}

void UbloxGps::updateCandidate() {
  // TinyGPSPlus keeps the last valid coordinate after fix loss. Accept only a
  // location committed by the sentence that has just completed, otherwise a
  // changing NMEA time could turn the stale coordinate into new track points.
  if (!parser_.location.isValid() || parser_.location.age() > 250) return;
  const uint32_t key = timeKey();
  if (key == 0 || key == emittedKey_) return;
  candidateKey_ = key;
  candidate_.epoch = epoch();
  candidate_.latitudeE7 = static_cast<int32_t>(llround(parser_.location.lat() * 10000000.0));
  candidate_.longitudeE7 = static_cast<int32_t>(llround(parser_.location.lng() * 10000000.0));
  candidate_.altitudeCm = parser_.altitude.isValid() ? static_cast<int32_t>(lround(parser_.altitude.meters() * 100.0)) : 0;
  candidate_.speedCms = parser_.speed.isValid() ? static_cast<uint16_t>(constrain(lround(parser_.speed.mps() * 100.0), 0L, 65535L)) : 0;
  candidate_.courseCdeg = parser_.course.isValid() ? static_cast<uint16_t>(constrain(lround(parser_.course.deg() * 100.0), 0L, 35999L)) : 0;
  candidate_.hdopCenti = parser_.hdop.isValid()
                              ? static_cast<uint16_t>(parser_.hdop.value() > 65535 ? 65535 : parser_.hdop.value())
                              : 0;
  candidate_.satellites = parser_.satellites.isValid()
                              ? static_cast<uint8_t>(parser_.satellites.value() > 255 ? 255 : parser_.satellites.value())
                              : 0;
  candidate_.flags = 0x01;
  if (parser_.altitude.isValid()) candidate_.flags |= 0x02;
  if (parser_.time.isValid() && parser_.date.isValid()) candidate_.flags |= 0x04;
  candidateReady_ = true;
  candidateUpdatedMs_ = millis();
}

bool UbloxGps::poll(GpsPoint &point) {
  while (serial_.available()) {
    const char value = static_cast<char>(serial_.read());
    captureNmea(value);
    if (parser_.encode(value)) updateCandidate();
  }
  if (!candidateReady_ || millis() - candidateUpdatedMs_ < 250) return false;
  point = candidate_;
  emittedKey_ = candidateKey_;
  candidateReady_ = false;
  return true;
}
