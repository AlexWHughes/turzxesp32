#include "widget_engine.h"

#include <cstring>
#include <HTTPClient.h>
#include <JPEGDEC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "turzx_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {
constexpr uint32_t kWeatherIntervalMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kSpotifyIntervalMs = 15UL * 1000UL;
// Match displayed HH:MM — refreshing faster only queues extra USB blits.
constexpr uint32_t kClockIntervalMs = 60000UL;

struct ArtDecodeTarget {
  uint16_t *pixels = nullptr;
  int width = 0;
  int height = 0;
};

ArtDecodeTarget artDecodeTarget;

int drawAlbumJpeg(JPEGDRAW *pDraw) {
  if (!artDecodeTarget.pixels || pDraw == nullptr || pDraw->pPixels == nullptr) {
    return 0;
  }
  for (int row = 0; row < pDraw->iHeight; ++row) {
    const int dy = pDraw->y + row;
    if (dy < 0 || dy >= artDecodeTarget.height) {
      continue;
    }
    int sx = 0;
    int dx = pDraw->x;
    int copy = pDraw->iWidth;
    if (dx < 0) {
      sx = -dx;
      copy -= sx;
      dx = 0;
    }
    if (dx + copy > artDecodeTarget.width) {
      copy = artDecodeTarget.width - dx;
    }
    if (copy <= 0) {
      continue;
    }
    memcpy(artDecodeTarget.pixels + static_cast<size_t>(dy) * artDecodeTarget.width + dx,
           pDraw->pPixels + static_cast<size_t>(row) * pDraw->iWidth + sx,
           static_cast<size_t>(copy) * sizeof(uint16_t));
  }
  return 1;
}

String spotifyImageUrl(JsonDocument &doc) {
  JsonObject item = doc["item"].as<JsonObject>();
  if (item.isNull()) {
    return "";
  }
  JsonArray images = item["album"]["images"].as<JsonArray>();
  if (images.isNull() || images.size() == 0) {
    images = item["images"].as<JsonArray>();
  }
  String best;
  int bestScore = 100000;
  for (JsonObject image : images) {
    const char *url = image["url"] | "";
    if (url == nullptr || url[0] == '\0') {
      continue;
    }
    const int height = image["height"] | 0;
    const int score = height <= 0 ? 500 : abs(height - 300);
    if (score < bestScore) {
      bestScore = score;
      best = url;
    }
  }
  return best;
}

bool downloadAlbumJpeg(WiFiClientSecure &client, const String &url, uint8_t *&out, size_t &outLen) {
  out = nullptr;
  outLen = 0;
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    return false;
  }
  http.setConnectTimeout(4000);
  http.setTimeout(12000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    TURZX_LOGW("Album art HTTP %d", code);
    http.end();
    return false;
  }
  const int declared = http.getSize();
  if (declared > 96 * 1024) {
    TURZX_LOGW("Album art too large (%d)", declared);
    http.end();
    return false;
  }
  const size_t cap = declared > 0 ? static_cast<size_t>(declared) : static_cast<size_t>(96 * 1024);
  uint8_t *buf =
      static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(cap));
  }
  if (!buf) {
    http.end();
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  size_t got = 0;
  const uint32_t start = millis();
  while (got < cap && millis() - start < 12000) {
    if (declared > 0 && got >= static_cast<size_t>(declared)) {
      break;
    }
    const int avail = stream->available();
    if (avail <= 0) {
      if (!http.connected()) {
        break;
      }
      delay(2);
      continue;
    }
    const size_t room = cap - got;
    const size_t ask = static_cast<size_t>(avail) > room ? room : static_cast<size_t>(avail);
    const int n = stream->read(buf + got, ask);
    if (n <= 0) {
      break;
    }
    got += static_cast<size_t>(n);
  }
  http.end();
  if (got < 64 || (declared > 0 && got < static_cast<size_t>(declared))) {
    free(buf);
    return false;
  }
  out = buf;
  outLen = got;
  return true;
}

bool decodeAlbumJpeg(uint8_t *jpegBytes, size_t jpegLen, uint16_t *&pixels, int &width, int &height) {
  pixels = nullptr;
  width = 0;
  height = 0;
  static JPEGDEC jpeg;
  if (!jpeg.openRAM(jpegBytes, static_cast<int>(jpegLen), drawAlbumJpeg)) {
    return false;
  }
  jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  int scale = 0;
  int w = jpeg.getWidth();
  int h = jpeg.getHeight();
  while ((w > 240 || h > 240) && scale < 3) {
    ++scale;
    w /= 2;
    h /= 2;
  }
  int option = 0;
  if (scale == 1) {
    option = JPEG_SCALE_HALF;
  } else if (scale == 2) {
    option = JPEG_SCALE_QUARTER;
  } else if (scale == 3) {
    option = JPEG_SCALE_EIGHTH;
  }
  if (w < 8 || h < 8 || w > 320 || h > 320) {
    jpeg.close();
    return false;
  }
  const size_t pixelsN = static_cast<size_t>(w) * static_cast<size_t>(h);
  pixels = static_cast<uint16_t *>(
      heap_caps_malloc(pixelsN * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels) {
    pixels = static_cast<uint16_t *>(malloc(pixelsN * sizeof(uint16_t)));
  }
  if (!pixels) {
    jpeg.close();
    return false;
  }
  memset(pixels, 0, pixelsN * sizeof(uint16_t));
  artDecodeTarget.pixels = pixels;
  artDecodeTarget.width = w;
  artDecodeTarget.height = h;
  const int decoded = jpeg.decode(0, 0, option);
  artDecodeTarget.pixels = nullptr;
  jpeg.close();
  if (decoded != 1) {
    free(pixels);
    pixels = nullptr;
    return false;
  }
  width = w;
  height = h;
  return true;
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

String urlDecode(const String &value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] == '%' && i + 2 < value.length()) {
      char hex[3] = {value[i + 1], value[i + 2], '\0'};
      out += static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    } else if (value[i] == '+') {
      out += ' ';
    } else {
      out += value[i];
    }
  }
  return out;
}

String spotifyCodeFromPaste(const String &pasted) {
  String s = pasted;
  s.trim();
  const int key = s.indexOf("code=");
  if (key >= 0) {
    const int start = key + 5;
    int end = s.indexOf('&', start);
    if (end < 0) {
      end = s.indexOf('#', start);
    }
    if (end < 0) {
      end = s.length();
    }
    return urlDecode(s.substring(start, end));
  }
  if (s.indexOf(' ') < 0 && s.length() > 20) {
    return s;
  }
  return "";
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
  if (cfg.spotifyClientId != cfg_.spotifyClientId ||
      cfg.spotifyClientSecret != cfg_.spotifyClientSecret ||
      cfg.spotifyRefreshToken != cfg_.spotifyRefreshToken) {
    spotifyAccessToken_ = "";
    spotifyTokenExpiryMs_ = 0;
  }
  cfg_ = cfg;
  lastWeatherMs_ = 0;
  lastSpotifyMs_ = 0;
  spotifyPollPeriodMs_ = kSpotifyIntervalMs;
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
      slotPrimary_[i] = spotifyPrimary_.isEmpty() ? (spotifyReady() ? "Idle" : "Sign in")
                                                 : spotifyPrimary_;
      slotSecondary_[i] = spotifySecondary_;
    } else if (cfg_.widgetTypes[i] == "date") {
      slotPrimary_[i] = "--";
      slotSecondary_[i] = "";
    } else if (cfg_.widgetTypes[i] == "system") {
      slotPrimary_[i] = "--";
      slotSecondary_[i] = "";
    } else if (cfg_.widgetTypes[i] == "note") {
      slotPrimary_[i] = cfg_.widgetPaths[i].isEmpty() ? "--" : cfg_.widgetPaths[i];
      slotSecondary_[i] = "";
    } else if (cfg_.widgetTypes[i] == "sun") {
      slotPrimary_[i] = sunRise_.isEmpty() ? "--" : sunRise_;
      slotSecondary_[i] = sunSet_.isEmpty() ? "sunset" : sunSet_;
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
  static uint32_t clockPeriodMs = 0;
  if (lastClockMs_ == 0 || now - lastClockMs_ >= clockPeriodMs) {
    lastClockMs_ = now == 0 ? 1 : now;
    refreshWorldClocks();
    refreshLocalWidgets();
    struct tm timeinfo {};
    if (getLocalTime(&timeinfo, 20)) {
      clockPeriodMs = static_cast<uint32_t>(60 - timeinfo.tm_sec) * 1000UL;
      if (clockPeriodMs == 0) {
        clockPeriodMs = kClockIntervalMs;
      }
    } else {
      clockPeriodMs = kClockIntervalMs;
    }
  }
  if (!dashboard_ || WiFi.status() != WL_CONNECTED) {
    return;
  }
  bool needWeather = false;
  bool needSpotify = false;
  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.weatherEnabled &&
        (cfg_.widgetTypes[i] == "weather" || cfg_.widgetTypes[i] == "sun")) {
      needWeather = true;
    }
    if (cfg_.spotifyEnabled && cfg_.widgetTypes[i] == "spotify") {
      needSpotify = true;
    }
  }
  static uint32_t weatherPollPeriodMs = kWeatherIntervalMs;
  if (needWeather && (lastWeatherMs_ == 0 || now - lastWeatherMs_ >= weatherPollPeriodMs)) {
    lastWeatherMs_ = now;
    weatherPollPeriodMs = pollWeather() ? kWeatherIntervalMs : 30000UL;
  }
  if (spotifyPollPeriodMs_ == 0) {
    spotifyPollPeriodMs_ = kSpotifyIntervalMs;
  }
  if (needSpotify && (lastSpotifyMs_ == 0 || now - lastSpotifyMs_ >= spotifyPollPeriodMs_)) {
    lastSpotifyMs_ = now;
    // Failed TLS/token polls used to retry every 15s and drip PSRAM via mbedTLS.
    spotifyPollPeriodMs_ = pollSpotify() ? kSpotifyIntervalMs : 120000UL;
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

void WidgetEngine::refreshLocalWidgets() {
  struct tm timeinfo {};
  const bool haveTime = getLocalTime(&timeinfo, 20);
  char dateBuf[16];
  char dayBuf[16];
  if (haveTime) {
    strftime(dateBuf, sizeof(dateBuf), "%d %b", &timeinfo);
    strftime(dayBuf, sizeof(dayBuf), "%A", &timeinfo);
  }

  const uint32_t upSec = millis() / 1000UL;
  char upBuf[24];
  snprintf(upBuf, sizeof(upBuf), "%lud %02lu:%02lu", upSec / 86400UL, (upSec / 3600UL) % 24UL,
           (upSec / 60UL) % 60UL);
  String rssi = (WiFi.status() == WL_CONNECTED) ? (String(WiFi.RSSI()) + " dBm") : "offline";
  String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : upBuf;

  for (int i = 0; i < kMaxWidgets; ++i) {
    const String &type = cfg_.widgetTypes[i];
    if (type == "date") {
      slotPrimary_[i] = haveTime ? dateBuf : "--";
      slotSecondary_[i] = haveTime ? dayBuf : "no NTP";
    } else if (type == "system") {
      slotPrimary_[i] = rssi;
      slotSecondary_[i] = ip + "  " + upBuf;
    } else if (type == "note") {
      String text = cfg_.widgetPaths[i];
      if (text.length() > 28) {
        text = text.substring(0, 28);
      }
      slotPrimary_[i] = text.isEmpty() ? "--" : text;
      slotSecondary_[i] = "";
    } else {
      continue;
    }
    if (dashboard_) {
      dashboard_->setSlot(i, cfg_.widgetLabels[i], slotPrimary_[i], slotSecondary_[i]);
    }
  }
}

bool WidgetEngine::spotifyReady() const {
  return cfg_.spotifyClientId.length() > 0 && cfg_.spotifyClientSecret.length() > 0 &&
         cfg_.spotifyRefreshToken.length() > 0;
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
  url += "&daily=sunrise,sunset&forecast_days=1";
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
  const char *rise = doc["daily"]["sunrise"][0] | "";
  const char *set = doc["daily"]["sunset"][0] | "";
  auto hhmm = [](const char *iso) {
    const char *t = iso ? strchr(iso, 'T') : nullptr;
    if (!t || strlen(t) < 6) {
      return String("--:--");
    }
    return String(t + 1).substring(0, 5);
  };
  sunRise_ = hhmm(rise);
  sunSet_ = hhmm(set);

  for (int i = 0; i < kMaxWidgets; ++i) {
    if (cfg_.widgetTypes[i] == "weather") {
      slotPrimary_[i] = weatherPrimary_;
      slotSecondary_[i] = weatherSecondary_;
    } else if (cfg_.widgetTypes[i] == "sun") {
      slotPrimary_[i] = sunRise_;
      slotSecondary_[i] = String("set ") + sunSet_;
    } else {
      continue;
    }
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
  if (!spotifyReady()) {
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
  http.setAuthorization(cfg_.spotifyClientId.c_str(), cfg_.spotifyClientSecret.c_str());

  String body = "grant_type=refresh_token&refresh_token=";
  body += urlEncode(cfg_.spotifyRefreshToken);
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
}

void WidgetEngine::clearAlbumArt() {
  artUrl_ = "";
  artFailUrl_ = "";
  if (dashboard_) {
    dashboard_->clearAlbumArt();
  }
}

void WidgetEngine::updateAlbumArt(const String &imageUrl) {
  if (!dashboard_ || imageUrl.isEmpty() || imageUrl == artUrl_ || imageUrl == artFailUrl_) {
    return;
  }
  WiFiClientSecure client;
  configureSpotifyTls(client);
  uint8_t *jpegBytes = nullptr;
  size_t jpegLen = 0;
  uint16_t *pixels = nullptr;
  int width = 0;
  int height = 0;
  const bool got = downloadAlbumJpeg(client, imageUrl, jpegBytes, jpegLen) &&
                   decodeAlbumJpeg(jpegBytes, jpegLen, pixels, width, height);
  free(jpegBytes);
  if (!got || !pixels) {
    free(pixels);
    if (imageUrl != artFailUrl_) {
      TURZX_LOGW("Album art decode failed");
      dashboard_->clearAlbumArt();
      artFailUrl_ = imageUrl;
      artUrl_ = "";
    }
    return;
  }
  dashboard_->setAlbumArt(pixels, static_cast<int16_t>(width), static_cast<int16_t>(height));
  free(pixels);
  artUrl_ = imageUrl;
  artFailUrl_ = "";
  TURZX_LOGI("Album art %dx%d", width, height);
}

bool WidgetEngine::pollSpotify() {
  bool ok = false;
  bool refreshArt = false;
  bool dropArt = false;
  String imageUrl;
  if (!spotifyReady()) {
    spotifyPrimary_ = "Sign in";
    spotifySecondary_ = "WebUI Spotify";
    dropArt = true;
  } else if (!refreshSpotifyToken()) {
    spotifyPrimary_ = "Auth error";
    spotifySecondary_ = "Check refresh token";
    dropArt = true;
  } else {
    WiFiClientSecure client;
    configureSpotifyTls(client);
    HTTPClient http;
    if (!http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing")) {
      spotifyPrimary_ = "Error";
      spotifySecondary_ = "HTTP begin";
    } else {
      http.setConnectTimeout(3000);
      http.setTimeout(8000);
      http.addHeader("Authorization", "Bearer " + spotifyAccessToken_);
      const int code = http.GET();
      if (code == 204) {
        spotifyPrimary_ = "Idle";
        spotifySecondary_ = "Nothing playing";
        http.end();
        dropArt = true;
        ok = true;
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
          imageUrl = spotifyImageUrl(doc);
          refreshArt = true;
          ok = true;
        }
      } else {
        TURZX_LOGW("Spotify player HTTP %d", code);
        http.end();
        spotifyPrimary_ = "Error";
        spotifySecondary_ = "HTTP " + String(code);
      }
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
  if (dropArt) {
    clearAlbumArt();
  } else if (refreshArt) {
    if (imageUrl.isEmpty()) {
      clearAlbumArt();
    } else {
      updateAlbumArt(imageUrl);
    }
  }
  return ok;
}

bool spotifyExchangeAuthCode(const String &clientId, const String &clientSecret, const String &pasted,
                             const String &redirectUri, String &refreshTokenOut, String &errorOut) {
  refreshTokenOut = "";
  errorOut = "";
  if (clientId.isEmpty() || clientSecret.isEmpty()) {
    errorOut = "client id and secret required";
    return false;
  }
  if (redirectUri != spotifyRedirectUri()) {
    errorOut = "redirect URI mismatch";
    return false;
  }
  const String code = spotifyCodeFromPaste(pasted);
  if (code.isEmpty()) {
    errorOut = "paste the browser URL that contains code=";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    errorOut = "WiFi not connected";
    return false;
  }

  WiFiClientSecure client;
  client.useBuiltinCACertBundle();
  HTTPClient http;
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) {
    errorOut = "token request failed to start";
    return false;
  }
  http.setConnectTimeout(5000);
  http.setTimeout(12000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setAuthorization(clientId.c_str(), clientSecret.c_str());
  String body = "grant_type=authorization_code&code=";
  body += urlEncode(code);
  body += "&redirect_uri=";
  body += urlEncode(redirectUri);
  const int status = http.POST(body);
  if (status != HTTP_CODE_OK) {
    http.end();
    errorOut = "Spotify rejected the code (HTTP " + String(status) + ")";
    return false;
  }
  String resp = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, resp)) {
    errorOut = "token response was not JSON";
    return false;
  }
  refreshTokenOut = doc["refresh_token"] | "";
  if (refreshTokenOut.isEmpty()) {
    errorOut = "Spotify did not return a refresh token";
    return false;
  }
  return true;
}
