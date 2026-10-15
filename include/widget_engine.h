#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "config_store.h"
#include "dashboard.h"

class WidgetEngine {
 public:
  void begin(Dashboard &dashboard);
  void applyConfig(const AppConfig &cfg);
  void onMqttMessage(const String &topic, const String &payload);
  void loop();

 private:
  void refreshWorldClocks();
  bool pollWeather();
  void pollSpotify();
  bool refreshSpotifyToken();
  void configureSpotifyTls(WiFiClientSecure &client) const;
  String extractJsonPath(const String &json, const String &path) const;
  int parseUtcOffsetMinutes(const String &tz) const;
  String formatClockAtOffset(int offsetMinutes) const;
  const char *weatherCodeLabel(int code) const;

  Dashboard *dashboard_ = nullptr;
  AppConfig cfg_;
  String slotPrimary_[kMaxWidgets];
  String slotSecondary_[kMaxWidgets];
  String spotifyAccessToken_;
  uint32_t spotifyTokenExpiryMs_ = 0;
  uint32_t lastWeatherMs_ = 0;
  uint32_t lastSpotifyMs_ = 0;
  uint32_t lastClockMs_ = 0;
  bool weatherOk_ = false;
  String weatherPrimary_;
  String weatherSecondary_;
  String spotifyPrimary_;
  String spotifySecondary_;
};
