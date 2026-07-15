#include "../include/settings_store.h"

bool SettingsStore::begin() {
  if (!preferences_.begin("c6v2cfg", false)) return false;
  settings_.awakeTimeSec = static_cast<uint16_t>(
      constrain(preferences_.getUInt("awake", 120), 1U, 3600U));
  settings_.sleepTimeSec = constrain(preferences_.getUInt("sleep", 60), 5U, 86400U);
  settings_.pointsBeforeSleep = static_cast<uint16_t>(
      constrain(preferences_.getUInt("points", 3), 1U, 1000U));
  settings_.screenOnTimerWake = preferences_.getBool("screen", false);
  settings_.followSleepScheduleWhileBle = preferences_.getBool("bt_sleep", false);
  return true;
}

bool SettingsStore::save(const TrackerSettings &settings) {
  settings_ = settings;
  bool ok = true;
  ok &= preferences_.putUInt("awake", settings_.awakeTimeSec) == sizeof(uint32_t);
  ok &= preferences_.putUInt("sleep", settings_.sleepTimeSec) == sizeof(uint32_t);
  ok &= preferences_.putUInt("points", settings_.pointsBeforeSleep) == sizeof(uint32_t);
  ok &= preferences_.putBool("screen", settings_.screenOnTimerWake) == sizeof(uint8_t);
  ok &= preferences_.putBool("bt_sleep", settings_.followSleepScheduleWhileBle) == sizeof(uint8_t);
  return ok;
}
