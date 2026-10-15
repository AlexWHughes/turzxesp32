#pragma once

#include <Arduino.h>
#include <functional>

#include "config_store.h"

using ConfigChangedHandler = std::function<void(const AppConfig &)>;
using BrightnessHandler = std::function<void(uint8_t)>;

class WebUi {
 public:
  void begin(ConfigStore &store, ConfigChangedHandler onConfig,
             BrightnessHandler onBrightness);
  void loop();
  void setStatus(const String &status);
  void setDiag(const String &diag);
  const String &status() const { return status_; }
  const String &diag() const { return diag_; }
  ConfigStore *store() { return store_; }

 private:
  ConfigStore *store_ = nullptr;
  ConfigChangedHandler onConfig_ = nullptr;
  BrightnessHandler onBrightness_ = nullptr;
  String status_ = "booting";
  String diag_;
};
