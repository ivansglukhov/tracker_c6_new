#pragma once

#include "tracker_types.h"

class SimpleDisplay {
 public:
  bool begin();
  void update(const DeviceStatus &status);
  void off();
  bool ready() const { return ready_; }

 private:
  void drawStatic();
  bool ready_ = false;
  DeviceStatus previous_{};
  bool firstUpdate_ = true;
};

