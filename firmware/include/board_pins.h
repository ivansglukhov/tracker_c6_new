#pragma once

#include <Arduino.h>

namespace board {

constexpr int LCD_SCK = 1;
constexpr int LCD_MOSI = 2;
constexpr int LCD_CS = 14;
constexpr int LCD_DC = 15;
constexpr int LCD_RST = 22;
constexpr int LCD_BL = 23;
constexpr int LCD_WIDTH = 320;
constexpr int LCD_HEIGHT = 172;

constexpr int SD_SCK = 1;
constexpr int SD_MISO = 3;
constexpr int SD_MOSI = 2;
constexpr int SD_CS = 4;

constexpr int BATTERY_ADC = 0;
constexpr float BATTERY_SCALE = 3.0F;
constexpr float BATTERY_EMPTY_V = 3.30F;
constexpr float BATTERY_FULL_V = 4.20F;

constexpr int GPS_RX = 17;
constexpr int GPS_TX = 16;
constexpr uint32_t GPS_BAUD = 38400;

// Active low. Hardware requires an external 10 kOhm pull-up to 3.3 V.
constexpr int WAKE_BUTTON = 5;

}  // namespace board

