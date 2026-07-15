#include "../include/simple_display.h"

#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <driver/gpio.h>

#include "../include/board_pins.h"

namespace {
Arduino_HWSPI lcdBus(board::LCD_DC, board::LCD_CS, board::LCD_SCK, board::LCD_MOSI,
                     board::SD_MISO, &SPI, true);
Arduino_ST7789 lcd(&lcdBus, board::LCD_RST, 0, false, 172, 320, 34, 0, 34, 0);

constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t RED = 0xF800;
constexpr uint16_t YELLOW = 0xFFE0;
constexpr uint16_t GREY = 0x8410;

void initializeController() {
  static const uint8_t operations[] = {
      BEGIN_WRITE, WRITE_COMMAND_8, 0x11, END_WRITE, DELAY, 120,
      BEGIN_WRITE,
      WRITE_C8_D16, 0xDF, 0x98, 0x53,
      WRITE_C8_D8, 0xB2, 0x23,
      WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
      WRITE_COMMAND_8, 0xBB, WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
      WRITE_C8_D16, 0xC0, 0x44, 0xA4,
      WRITE_C8_D8, 0xC1, 0x16,
      WRITE_COMMAND_8, 0xC3, WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
      WRITE_COMMAND_8, 0xC4, WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A,
      0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
      WRITE_COMMAND_8, 0xC8, WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      WRITE_COMMAND_8, 0xD0, WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
      WRITE_C8_D16, 0xD7, 0x00, 0x30,
      WRITE_C8_D8, 0xE6, 0x14,
      WRITE_C8_D8, 0xDE, 0x01,
      WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
      WRITE_COMMAND_8, 0xC1, WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
      WRITE_C8_D16, 0xC2, 0x06, 0x3A,
      WRITE_C8_D16, 0xC4, 0x72, 0x12,
      WRITE_C8_D8, 0xBE, 0x00,
      WRITE_C8_D8, 0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x01, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x35, 0x00,
      WRITE_C8_D8, 0x3A, 0x05,
      WRITE_COMMAND_8, 0x2A, WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
      WRITE_COMMAND_8, 0x2B, WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
      WRITE_C8_D8, 0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5, WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x36, 0x00,
      WRITE_COMMAND_8, 0x21,
      END_WRITE, DELAY, 10,
      BEGIN_WRITE, WRITE_COMMAND_8, 0x29, END_WRITE,
  };
  lcdBus.batchOperation(operations, sizeof(operations));
}

void field(int x, int y, int width, int height, uint16_t color, uint8_t size, const char *text) {
  lcd.fillRect(x, y, width, height, BLACK);
  lcd.setCursor(x, y);
  lcd.setTextColor(color);
  lcd.setTextSize(size);
  lcd.print(text);
}
}

bool SimpleDisplay::begin() {
  gpio_hold_dis(static_cast<gpio_num_t>(board::LCD_BL));
  pinMode(board::LCD_BL, OUTPUT);
  digitalWrite(board::LCD_BL, LOW);
  pinMode(board::SD_CS, OUTPUT);
  digitalWrite(board::SD_CS, HIGH);
  if (!lcd.begin()) return false;
  initializeController();
  lcd.setRotation(1);
  lcd.fillScreen(BLACK);
  ledcAttach(board::LCD_BL, 5000, 10);
  ledcWrite(board::LCD_BL, 680);
  ready_ = true;
  firstUpdate_ = true;
  drawStatic();
  return true;
}

void SimpleDisplay::drawStatic() {
  lcd.setTextWrap(false);
  lcd.setTextColor(GREY);
  lcd.setTextSize(1);
  lcd.setCursor(8, 9);
  lcd.print("GPS");
  lcd.setCursor(66, 9);
  lcd.print("SD");
  lcd.setCursor(120, 9);
  lcd.print("BAT");
  lcd.setCursor(208, 9);
  lcd.print("TRACK");
  lcd.drawFastHLine(6, 28, 308, GREY);
  lcd.setCursor(8, 42);
  lcd.print("DISTANCE");
  lcd.setTextSize(2);
  lcd.setCursor(8, 133);
  lcd.print("ALT");
}

void SimpleDisplay::update(const DeviceStatus &status) {
  if (!ready_) return;
  char text[32] = {};
  if (firstUpdate_ || previous_.gpsCoordinate != status.gpsCoordinate) {
    field(31, 5, 31, 15, status.gpsCoordinate ? GREEN : YELLOW, 1,
          status.gpsCoordinate ? "OK" : "WAIT");
  }
  if (firstUpdate_ || previous_.sdReady != status.sdReady || previous_.sdError != status.sdError) {
    field(84, 5, 34, 15, status.sdError ? RED : (status.sdReady ? GREEN : YELLOW), 1,
          status.sdError ? "ERR" : (status.sdReady ? "OK" : "WAIT"));
  }
  if (firstUpdate_ || previous_.batteryPercent != status.batteryPercent) {
    snprintf(text, sizeof(text), "%u%%", status.batteryPercent);
    field(146, 5, 55, 18, WHITE, 2, text);
  }
  if (firstUpdate_ || previous_.trackId != status.trackId) {
    snprintf(text, sizeof(text), "%06lu", static_cast<unsigned long>(status.trackId));
    field(247, 5, 68, 18, WHITE, 2, text);
  }
  if (firstUpdate_ || previous_.distanceM != status.distanceM) {
    const unsigned long km = status.distanceM / 1000UL;
    const unsigned long hundredths = (status.distanceM % 1000UL) / 10UL;
    snprintf(text, sizeof(text), "%lu.%02lu km", km, hundredths);
    field(18, 60, 294, 58, WHITE, 5, text);
  }
  if (firstUpdate_ || previous_.altitudeM != status.altitudeM) {
    snprintf(text, sizeof(text), "%ld m", static_cast<long>(status.altitudeM));
    field(58, 128, 140, 38, GREEN, 3, text);
  }
  if (firstUpdate_ || previous_.interactiveRemainingSec != status.interactiveRemainingSec ||
      previous_.bleConnected != status.bleConnected) {
    snprintf(text, sizeof(text), status.bleConnected ? "BT LINK" : "BT ON");
    field(202, 132, 112, 30, status.bleConnected ? GREEN : YELLOW, 2, text);
  }
  previous_ = status;
  firstUpdate_ = false;
}

void SimpleDisplay::off() {
  if (!ready_) return;
  static const uint8_t sleepOperations[] = {
      BEGIN_WRITE, WRITE_COMMAND_8, 0x28, END_WRITE, DELAY, 20,
      BEGIN_WRITE, WRITE_COMMAND_8, 0x10, END_WRITE,
  };
  lcdBus.batchOperation(sleepOperations, sizeof(sleepOperations));
  ledcWrite(board::LCD_BL, 0);
  ledcDetach(board::LCD_BL);
  pinMode(board::LCD_BL, OUTPUT);
  digitalWrite(board::LCD_BL, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(board::LCD_BL));
  ready_ = false;
}
