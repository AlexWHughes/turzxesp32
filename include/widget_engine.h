#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "config_store.h"
#include "dashboard.h"

// Exchanges a Spotify authorization-code callback URL for a refresh token.
// redirectUri must match the URI registered on the Spotify app and used in the authorize link.
bool spotifyExchangeAuthCode(const String &clientId, const String &clientSecret, const String &pasted,
                             const String &redirectUri, String &refreshTokenOut, String &errorOut);

inline const char *spotifyRedirectUri() { return "http://127.0.0.1:8888/callback"; }

class WidgetEngine {
 public:
  void begin(Dashboard &dashboard);
  void applyConfig(const AppConfig &cfg);
  void onMqttMessage(const String &topic, const String &payload);
  void loop();

 private:
  void refreshWorldClocks();
  void refreshLocalWidgets();
  bool spotifyReady() const;
  bool pollWeather();
  bool pollSpotify();
  bool refreshSpotifyToken();
  void updateAlbumArt(const String &imageUrl);
  void clearAlbumArt();
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
  uint32_t spotifyPollPeriodMs_ = 0;
  uint32_t lastClockMs_ = 0;
  bool weatherOk_ = false;
  String weatherPrimary_;
  String weatherSecondary_;
  String sunRise_;
  String sunSet_;
  String spotifyPrimary_;
  String spotifySecondary_;
  String artUrl_;
  String artFailUrl_;
};
