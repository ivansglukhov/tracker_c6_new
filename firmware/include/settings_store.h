#pragma once

#include <Preferences.h>

#include "tracker_types.h"

class SettingsStore {
 public:
  bool begin();
  const TrackerSettings &get() const { return settings_; }
  bool save(const TrackerSettings &settings);

 private:
  Preferences preferences_;
  TrackerSettings settings_;
};

