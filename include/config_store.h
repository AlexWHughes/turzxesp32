#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String mqttHost = "homeassistant.local";
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;
  String mqttClientId = "turzx-esp32";
  String haStatusTopic = "homeassistant/status";
  uint8_t brightness = 40;
  uint8_t orientation = 1;  // 0=portrait, 1=landscape
  uint16_t clockIntervalMs = 1000;
  uint16_t mqttReconnectMs = 5000;
  bool haDiscovery = true;
  String widgetTopics[6];
  String widgetLabels[6];
};

class ConfigStore {
 public:
  void begin();
  bool load(AppConfig &cfg);
  bool save(const AppConfig &cfg);
  AppConfig &get();
  String toJson(bool includeSecrets = false) const;
  bool applyJson(const String &json, bool saveToFlash = true);

 private:
  AppConfig cfg_;
  static constexpr const char *kNs = "turzx";
};
