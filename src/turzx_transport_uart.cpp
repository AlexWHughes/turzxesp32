#include "turzx_factory.h"
#include "turzx_transport.h"

#if TURZX_USE_UART

#include <HardwareSerial.h>

class TurzxUartTransport : public TurzxTransport {
 public:
  bool begin() override {
    Serial1.begin(TURZX_UART_BAUD, SERIAL_8N1, TURZX_UART_RX, TURZX_UART_TX);
    Serial1.setTimeout(200);
    ready_ = true;
    return true;
  }

  void end() override {
    Serial1.end();
    ready_ = false;
  }

  bool isReady() const override { return ready_; }

  size_t write(const uint8_t *data, size_t len) override { return Serial1.write(data, len); }

  size_t read(uint8_t *data, size_t len, uint32_t timeoutMs) override {
    Serial1.setTimeout(timeoutMs);
    return Serial1.readBytes(data, len);
  }

  void flushInput() override {
    while (Serial1.available()) {
      Serial1.read();
    }
  }

 private:
  bool ready_ = false;
};

TurzxTransport *createTurzxTransport() {
  static TurzxUartTransport transport;
  return &transport;
}

#endif
