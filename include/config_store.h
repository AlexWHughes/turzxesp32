#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

static constexpr int kMaxWidgets = 6;

enum class LayoutMode : uint8_t {
  Grid = 0,
  Carousel = 1,
};

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
  // Matches upstream Orientation: 0=Portrait 1=ReversePortrait 2=Landscape 3=ReverseLandscape
  uint8_t orientation = 2;
  // Clocks show HH:MM; sub-second / 1s updates are too heavy for USB Full-Speed
  // partial blits on a 1920×480 panel and starve WebUI / MQTT refreshes.
  uint16_t clockIntervalMs = 15000;
  uint16_t mqttReconnectMs = 5000;
  bool haDiscovery = true;
  // Off skips broker reconnects, Open-Meteo fetches, and Spotify TLS polls.
  bool mqttEnabled = true;
  bool weatherEnabled = true;
  bool spotifyEnabled = true;

  LayoutMode layoutMode = LayoutMode::Grid;
  uint16_t carouselIntervalMs = 8000;
  bool testPattern = false;
  // aurora | ember | forest | neon | paper | midnight
  String theme = "aurora";

  float weatherLat = -37.8136f;
  float weatherLon = 144.9631f;
  String weatherTz = "Australia/Melbourne";
  bool weatherFahrenheit = false;

  String spotifyClientId;
  String spotifyClientSecret;
  String spotifyRefreshToken;

  // Per-slot: type = mqtt_text|mqtt_json|world_clock|weather|spotify|off
  String widgetTypes[kMaxWidgets];
  String widgetLabels[kMaxWidgets];
  String widgetTopics[kMaxWidgets];
  String widgetPaths[kMaxWidgets];
  String widgetTz[kMaxWidgets];
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

inline const char *layoutModeToString(LayoutMode mode) {
  switch (mode) {
    case LayoutMode::Grid:
      return "grid";
    case LayoutMode::Carousel:
      return "carousel";
    default: {
      const LayoutMode unreachable = mode;
      (void)unreachable;
      return "grid";
    }
  }
}

inline LayoutMode layoutModeFromString(const String &s) {
  if (s.equalsIgnoreCase("carousel")) {
    return LayoutMode::Carousel;
  }
  return LayoutMode::Grid;
}
