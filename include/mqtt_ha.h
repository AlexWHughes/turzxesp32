#pragma once

#include <Arduino.h>
#include <functional>

#include "config_store.h"

using MqttMessageHandler = std::function<void(const String &topic, const String &payload)>;

class MqttHa {
 public:
  void begin(const AppConfig &cfg, MqttMessageHandler handler);
  void applyConfig(const AppConfig &cfg);
  void loop();
  bool isConnected() const;
  void publishStatus(const String &payload);
  void publishHaDiscovery();

 private:
  void ensureConnected();
  void subscribeAll();

  AppConfig cfg_;
  MqttMessageHandler handler_;
  bool started_ = false;
  uint32_t reconnectDelayMs_ = 0;
  uint8_t failStreak_ = 0;
};
