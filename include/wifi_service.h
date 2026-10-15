#pragma once

#include <Arduino.h>

#include "config_store.h"

class WifiService {
 public:
  bool begin(const AppConfig &cfg);
  void loop();
  bool isStaConnected() const;
  bool isApMode() const { return apMode_; }
  IPAddress localIp() const;

 private:
  bool startAp();
  bool apMode_ = false;
};
