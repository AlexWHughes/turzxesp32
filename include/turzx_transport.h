#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

class TurzxTransport {
 public:
  virtual ~TurzxTransport() = default;
  virtual bool begin() = 0;
  virtual void end() = 0;
  virtual bool isReady() const = 0;
  virtual size_t write(const uint8_t *data, size_t len) = 0;
  virtual size_t read(uint8_t *data, size_t len, uint32_t timeoutMs) = 0;
  virtual void flushInput() = 0;
  virtual bool wakeDevice() { return true; }
  /** Latest USB/host diagnostic line for WebUI when OTG serial is unavailable. */
  virtual String diag() const { return String(); }
  /** Hotplug retry: reopen CDC if a device appeared after begin(). */
  virtual bool poll() { return isReady(); }
  /** True when a new USB device was seen and open has not been attempted yet. */
  virtual bool hotplugPending() const { return false; }
};
