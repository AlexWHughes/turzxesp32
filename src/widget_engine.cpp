#include "widget_engine.h"

#include <cstring>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <ArduinoJson.h>

#include "turzx_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {
constexpr uint32_t kWeatherIntervalMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kSpotifyIntervalMs = 15UL * 1000UL;
// Match dashboard clock cadence — HH:MM does not need 1 Hz USB blits.
constexpr uint32_t kClockIntervalMs = 15000UL;

bool hasSpotifySecrets() {
#if defined(SPOTIFY_CLIENT_ID) && defined(SPOTIFY_CLIENT_SECRET) && defined(SPOTIFY_REFRESH_TOKEN)
  return SPOTIFY_CLIENT_ID[0] != '\0' && strcmp(SPOTIFY_CLIENT_ID, "your_client_id") != 0 &&
         SPOTIFY_REFRESH_TOKEN[0] != '\0' &&
         strcmp(SPOTIFY_REFRESH_TOKEN, "your_refresh_token") != 0;
#else
  return false;
#endif
}

String urlEncode(const String &value) {
  String out;
  out.reserve(value.length() * 2);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
      out += buf;
    }
  }
  return out;
}
}  // namespace

void WidgetEngine::begin(Dashboard &dashboard) {
  dashboard_ = &dashboard;
}

void WidgetEngine::applyConfig(const AppConfig &cfg) {
  if (cfg.weatherLat != cfg_.weatherLat || cfg.weatherLon != cfg_.weatherLon) {
    weatherOk_ = false;
    weatherPrimary_ = "";
    weatherSecondary_ = "";
  }
  cfg_ = cfg;
  lastWeatherMs_ = 0;
  lastSpotifyMs_ = 0;
  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.widgetTypes[i] == "off" || cfg_.widgetTypes[i].isEmpty()) {
      slotPrimary_[i] = "";
      slotSecondary_[i] = "";
      continue;
    }
    if (cfg_.widgetTypes[i] == "mqtt_text" || cfg_.widgetTypes[i] == "mqtt_json") {
      if (slotPrimary_[i].isEmpty()) {
        slotPrimary_[i] = "--";
      }
    } else if (cfg_.widgetTypes[i] == "world_clock") {
      slotPrimary_[i] = "--:--";
      slotSecondary_[i] = cfg_.widgetTz[i];
    } else if (cfg_.widgetTypes[i] == "weather") {
      slotPrimary_[i] = weatherOk_ ? weatherPrimary_ : "--";
      slotSecondary_[i] = weatherOk_ ? weatherSecondary_ : "Open-Meteo";
    } else if (cfg_.widgetTypes[i] == "spotify") {
      slotPrimary_[i] = spotifyPrimary_.isEmpty() ? (hasSpotifySecrets() ? "Idle" : "No secrets")
                                                 : spotifyPrimary_;
      slotSecondary_[i] = spotifySecondary_;
    }
    if (dashboard_) {
      dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
    }
  }
}

void WidgetEngine::onMqttMessage(const String &topic, const String &payload) {
  for (int i = 0; i < kMaxWidgets; ++i) {
    const String &type = cfg_.widgetTypes[i];
    if (cfg_.widgetTopics[i] != topic) {
      continue;
    }
    if (type == "mqtt_text") {
      slotPrimary_[i] = payload;
      slotSecondary_[i] = "";
    } else if (type == "mqtt_json") {
      slotPrimary_[i] = extractJsonPath(payload, cfg_.widgetPaths[i]);
      if (slotPrimary_[i].isEmpty()) {
        slotPrimary_[i] = "--";
      }
      slotSecondary_[i] = cfg_.widgetPaths[i];
    } else {
      continue;
    }
    if (dashboard_) {
      dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
    }
  }
}

void WidgetEngine::loop() {
  const uint32_t now = millis();
  if (now - lastClockMs_ >= kClockIntervalMs) {
    lastClockMs_ = now;
    refreshWorldClocks();
  }
  if (!dashboard_ || WiFi.status() != WL_CONNECTED) {
    return;
  }
  bool needWeather = false;
  bool needSpotify = false;
  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.widgetTypes[i] == "weather") {
      needWeather = true;
    }
    if (cfg_.widgetTypes[i] == "spotify") {
      needSpotify = true;
    }
  }
  static uint32_t weatherPollPeriodMs = kWeatherIntervalMs;
  if (needWeather && (lastWeatherMs_ == 0 || now - lastWeatherMs_ >= weatherPollPeriodMs)) {
    lastWeatherMs_ = now;
    weatherPollPeriodMs = pollWeather() ? kWeatherIntervalMs : 30000UL;
  }
  if (needSpotify && (lastSpotifyMs_ == 0 || now - lastSpotifyMs_ >= kSpotifyIntervalMs)) {
    lastSpotifyMs_ = now;
    pollSpotify();
  }
}

String WidgetEngine::extractJsonPath(const String &json, const String &path) const {
  if (path.isEmpty()) {
    return json;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return String();
  }
  JsonVariant cur = doc.as<JsonVariant>();
  int start = 0;
  while (start <= static_cast<int>(path.length())) {
    int dot = path.indexOf('.', start);
    String key = (dot < 0) ? path.substring(start) : path.substring(start, dot);
    if (key.isEmpty()) {
      break;
    }
    bool allDigits = true;
    for (size_t i = 0; i < key.length(); ++i) {
      if (!isDigit(key[i])) {
        allDigits = false;
        break;
      }
    }
    if (allDigits) {
      cur = cur[key.toInt()];
    } else {
      cur = cur[key];
    }
    if (cur.isNull()) {
      return String();
    }
    if (dot < 0) {
      break;
    }
    start = dot + 1;
  }
  if (cur.is<const char *>()) {
    return String(cur.as<const char *>());
  }
  if (cur.is<float>() || cur.is<double>()) {
    return String(cur.as<float>(), 1);
  }
  if (cur.is<int>() || cur.is<long>()) {
    return String(cur.as<long>());
  }
  if (cur.is<bool>()) {
    return cur.as<bool>() ? "true" : "false";
  }
  String out;
  serializeJson(cur, out);
  return out;
}

int WidgetEngine::parseUtcOffsetMinutes(const String &tz) const {
  String s = tz;
  s.trim();
  s.toUpperCase();
  if (s.startsWith("UTC")) {
    s = s.substring(3);
  } else if (s.startsWith("GMT")) {
    s = s.substring(3);
  }
  s.trim();
  if (s.isEmpty()) {
    return 0;
  }
  int sign = 1;
  if (s[0] == '+') {
    s = s.substring(1);
  } else if (s[0] == '-') {
    sign = -1;
    s = s.substring(1);
  }
  int colon = s.indexOf(':');
  int hours = 0;
  int mins = 0;
  if (colon >= 0) {
    hours = s.substring(0, colon).toInt();
    mins = s.substring(colon + 1).toInt();
  } else {
    hours = s.toInt();
  }
  return sign * (hours * 60 + mins);
}

String WidgetEngine::formatClockAtOffset(int offsetMinutes) const {
  time_t now = time(nullptr);
  if (now < 100000) {
    return String("--:--");
  }
  now += static_cast<time_t>(offsetMinutes) * 60;
  struct tm t {};
  gmtime_r(&now, &t);
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

void WidgetEngine::refreshWorldClocks() {
  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.widgetTypes[i] != "world_clock") {
      continue;
    }
    const int offset = parseUtcOffsetMinutes(cfg_.widgetTz[i].isEmpty() ? "UTC+0" : cfg_.widgetTz[i]);
    slotPrimary_[i] = formatClockAtOffset(offset);
    slotSecondary_[i] = cfg_.widgetTz[i];
    if (dashboard_) {
      dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
    }
  }
}

const char *WidgetEngine::weatherCodeLabel(int code) const {
  if (code == 0) {
    return "Clear";
  }
  if (code <= 3) {
    return "Cloudy";
  }
  if (code <= 48) {
    return "Fog";
  }
  if (code <= 57) {
    return "Drizzle";
  }
  if (code <= 67) {
    return "Rain";
  }
  if (code <= 77) {
    return "Snow";
  }
  if (code <= 82) {
    return "Showers";
  }
  if (code <= 86) {
    return "Snow";
  }
  if (code <= 99) {
    return "Storm";
  }
  return "Weather";
}

bool WidgetEngine::pollWeather() {
  HTTPClient http;
  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(cfg_.weatherLat, 4);
  url += "&longitude=";
  url += String(cfg_.weatherLon, 4);
  url += "&current=temperature_2m,weather_code,wind_speed_10m";
  url += "&temperature_unit=";
  url += cfg_.weatherFahrenheit ? "fahrenheit" : "celsius";
  url += "&wind_speed_unit=kmh";
  if (cfg_.weatherTz.length()) {
    url += "&timezone=";
    url += urlEncode(cfg_.weatherTz);
  }

  WiFiClientSecure client;
  client.setInsecure();
  if (!http.begin(client, url)) {
    TURZX_LOGW("Weather HTTP begin failed");
    return false;
  }
  http.setConnectTimeout(3000);
  http.setTimeout(8000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    TURZX_LOGW("Weather HTTP %d", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    TURZX_LOGW("Weather JSON parse failed");
    return false;
  }
  JsonVariant current = doc["current"];
  if (current.isNull()) {
    return false;
  }
  const float temp = current["temperature_2m"] | 0.0f;
  const int wmo = current["weather_code"] | 0;
  const float wind = current["wind_speed_10m"] | 0.0f;
  weatherPrimary_ = String(temp, 1) + (cfg_.weatherFahrenheit ? " F" : " C");
  weatherSecondary_ = String(weatherCodeLabel(wmo)) + "  " + String(wind, 0) + " km/h";
  weatherOk_ = true;

  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.widgetTypes[i] != "weather") {
      continue;
    }
    slotPrimary_[i] = weatherPrimary_;
    slotSecondary_[i] = weatherSecondary_;
    if (dashboard_) {
      dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
    }
  }
  return true;
}

void WidgetEngine::configureSpotifyTls(WiFiClientSecure &client) const {
  // Platform root bundle covers DigiCert (api.spotify.com) and Certainly
  // (accounts.spotify.com); avoids setInsecure().
  client.useBuiltinCACertBundle();
}

bool WidgetEngine::refreshSpotifyToken() {
#if defined(SPOTIFY_CLIENT_ID) && defined(SPOTIFY_CLIENT_SECRET) && defined(SPOTIFY_REFRESH_TOKEN)
  if (!hasSpotifySecrets()) {
    return false;
  }
  if (spotifyAccessToken_.length() && millis() < spotifyTokenExpiryMs_) {
    return true;
  }

  WiFiClientSecure client;
  configureSpotifyTls(client);
  HTTPClient http;
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) {
    return false;
  }
  http.setConnectTimeout(3000);
  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setAuthorization(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);

  String body = "grant_type=refresh_token&refresh_token=";
  body += urlEncode(SPOTIFY_REFRESH_TOKEN);
  const int code = http.POST(body);
  if (code != HTTP_CODE_OK) {
    TURZX_LOGW("Spotify token HTTP %d", code);
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    return false;
  }
  spotifyAccessToken_ = doc["access_token"] | "";
  const int expires = doc["expires_in"] | 3600;
  spotifyTokenExpiryMs_ = millis() + static_cast<uint32_t>((expires > 60 ? expires - 60 : expires) * 1000UL);
  return spotifyAccessToken_.length() > 0;
#else
  return false;
#endif
}

void WidgetEngine::pollSpotify() {
  if (!hasSpotifySecrets()) {
    spotifyPrimary_ = "No secrets";
    spotifySecondary_ = "Edit secrets.h";
    for (int i = 0; i < kMaxWidgets; ++i) {
      if (cfg_.widgetTypes[i] != "spotify") {
        continue;
      }
      slotPrimary_[i] = spotifyPrimary_;
      slotSecondary_[i] = spotifySecondary_;
      if (dashboard_) {
        dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
      }
    }
    return;
  }
  if (!refreshSpotifyToken()) {
    spotifyPrimary_ = "Auth error";
    spotifySecondary_ = "Check refresh token";
  } else {
    WiFiClientSecure client;
    configureSpotifyTls(client);
    HTTPClient http;
    if (!http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing")) {
      return;
    }
    http.setConnectTimeout(3000);
    http.setTimeout(8000);
    http.addHeader("Authorization", "Bearer " + spotifyAccessToken_);
    const int code = http.GET();
    if (code == 204) {
      spotifyPrimary_ = "Idle";
      spotifySecondary_ = "Nothing playing";
      http.end();
    } else if (code == HTTP_CODE_OK) {
      String resp = http.getString();
      http.end();
      JsonDocument doc;
      if (!deserializeJson(doc, resp)) {
        const bool playing = doc["is_playing"] | false;
        const char *name = doc["item"]["name"] | "Unknown";
        String artists;
        JsonArray arr = doc["item"]["artists"].as<JsonArray>();
        for (JsonVariant v : arr) {
          if (artists.length()) {
            artists += ", ";
          }
          artists += v["name"] | "";
        }
        spotifyPrimary_ = String(name);
        spotifySecondary_ = artists + (playing ? "" : "  (paused)");
      }
    } else {
      TURZX_LOGW("Spotify player HTTP %d", code);
      http.end();
      spotifyPrimary_ = "Error";
      spotifySecondary_ = "HTTP " + String(code);
    }
  }

  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.widgetTypes[i] != "spotify") {
      continue;
    }
    slotPrimary_[i] = spotifyPrimary_;
    slotSecondary_[i] = spotifySecondary_;
    if (dashboard_) {
      dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
    }
  }
}
