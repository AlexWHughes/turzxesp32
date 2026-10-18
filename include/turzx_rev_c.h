#pragma once

#include <Arduino.h>
#include <cstdint>

#include "turzx_transport.h"

// Integer values match mathoudebine Orientation IntEnum (lcd_comm.py).
enum class TurzxOrientation : uint8_t {
  Portrait = 0,
  ReversePortrait = 1,
  Landscape = 2,
  ReverseLandscape = 3,
};

class TurzxRevC {
 public:
  static constexpr uint16_t kPortraitW = 480;
  static constexpr uint16_t kPortraitH = 1920;

  explicit TurzxRevC(TurzxTransport &transport);

  bool begin();
  void end();
  bool isReady() const { return ready_ && transport_.isReady(); }

  bool setBrightness(uint8_t percent);
  bool setOrientation(TurzxOrientation orientation);
  // Stop stored image/video playback and power the panel down. Drops the USB link.
  bool screenOff();
  bool clear(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0);
  bool displayFullRgb565(const uint16_t *pixels, uint16_t width, uint16_t height);
  bool displayRegionRgb565(const uint16_t *pixels, uint16_t x, uint16_t y, uint16_t width,
                           uint16_t height);

  uint16_t width() const;
  uint16_t height() const;
  TurzxOrientation orientation() const { return orientation_; }
  const String &displayId() const { return displayId_; }

 private:
  bool hello();
  bool sendCommand(const uint8_t *cmd, size_t cmdLen, const uint8_t *payload = nullptr,
                   size_t payloadLen = 0, uint8_t padByte = 0x00, size_t readSize = 0);
  bool sendRaw(const uint8_t *data, size_t len, size_t readSize = 0);
  bool readExact(uint8_t *buf, size_t len, uint32_t timeoutMs);
  void padTo250(uint8_t *buf, size_t &len, size_t capacity, uint8_t padByte);
  bool generateAndSendFull(const uint16_t *pixels, uint16_t w, uint16_t h);
  bool generateAndSendPartial(const uint16_t *pixels, uint16_t x, uint16_t y, uint16_t w,
                              uint16_t h);

  TurzxTransport &transport_;
  bool ready_ = false;
  TurzxOrientation orientation_ = TurzxOrientation::Landscape;
  String displayId_;
  int romVersion_ = 90;
  uint32_t updateCount_ = 0;
};
