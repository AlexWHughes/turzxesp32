#include "config_store.h"

#include <cstring>

#include <Preferences.h>

#include "turzx_log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

void ConfigStore::begin() { load(cfg_); }

AppConfig &ConfigStore::get() { return cfg_; }

static void applySecrets(AppConfig &cfg) {
#if defined(WIFI_SSID)
  if (WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "YourNetworkName") != 0) {
    cfg.wifiSsid = WIFI_SSID;
    cfg.wifiPassword = WIFI_PASSWORD;
  }
#endif
#if defined(MQTT_HOST)
  if (MQTT_HOST[0] != '\0' && strcmp(MQTT_HOST, "homeassistant.local") != 0) {
    cfg.mqttHost = MQTT_HOST;
    cfg.mqttPort = MQTT_PORT;
  }
  if (MQTT_USER[0] != '\0') {
    cfg.mqttUser = MQTT_USER;
  }
  if (MQTT_PASSWORD[0] != '\0') {
    cfg.mqttPassword = MQTT_PASSWORD;
  }
#endif
}

static void applyWidgetDefaults(AppConfig &cfg) {
  // Migrate legacy label/topic-only slots to mqtt_text.
  bool anyTyped = false;
  for (int i = 0; i < kMaxWidgets; ++i) {
    if (!cfg.widgetTypes[i].isEmpty()) {
      anyTyped = true;
      break;
    }
  }
  if (!anyTyped) {
    bool hadLegacy = false;
    for (int i = 0; i < kMaxWidgets; ++i) {
      if (!cfg.widgetLabels[i].isEmpty() || !cfg.widgetTopics[i].isEmpty()) {
        cfg.widgetTypes[i] = "mqtt_text";
        hadLegacy = true;
      }
    }
    if (hadLegacy) {
      return;
    }
    cfg.widgetTypes[0] = "mqtt_text";
    cfg.widgetLabels[0] = "Living Room";
    cfg.widgetTopics[0] = "homeassistant/sensor/living_temp/state";
    cfg.widgetTypes[1] = "mqtt_text";
    cfg.widgetLabels[1] = "Outdoor";
    cfg.widgetTopics[1] = "homeassistant/sensor/outdoor_temp/state";
    cfg.widgetTypes[2] = "world_clock";
    cfg.widgetLabels[2] = "Melbourne";
    cfg.widgetTz[2] = "UTC+10";
    cfg.widgetTypes[3] = "weather";
    cfg.widgetLabels[3] = "Weather";
    cfg.widgetTypes[4] = "spotify";
    cfg.widgetLabels[4] = "Now Playing";
  }
}

bool ConfigStore::load(AppConfig &cfg) {
  Preferences prefs;
  const bool haveNvs = prefs.begin(kNs, true);
  bool migratedOrient = false;
  if (haveNvs) {
    cfg.wifiSsid = prefs.getString("wifiSsid", cfg.wifiSsid);
    cfg.wifiPassword = prefs.getString("wifiPass", cfg.wifiPassword);
    cfg.mqttHost = prefs.getString("mqttHost", cfg.mqttHost);
    cfg.mqttPort = prefs.getUShort("mqttPort", cfg.mqttPort);
    cfg.mqttUser = prefs.getString("mqttUser", cfg.mqttUser);
    cfg.mqttPassword = prefs.getString("mqttPass", cfg.mqttPassword);
    cfg.mqttClientId = prefs.getString("mqttCid", cfg.mqttClientId);
    cfg.haStatusTopic = prefs.getString("haStatus", cfg.haStatusTopic);
    cfg.brightness = prefs.getUChar("brightness", cfg.brightness);
    cfg.orientation = prefs.getUChar("orient", cfg.orientation);
    // Migrate pre-1.3.1 mapping (0=P, 1=L, 2=RP, 3=RL) → upstream IntEnum
    // (0=P, 1=RP, 2=L, 3=RL). Without this, saved "Landscape" (1) becomes
    // ReversePortrait and the modes look swapped.
    const uint8_t orientMap = prefs.getUChar("orMap", 0);
    if (orientMap < 1) {
      if (cfg.orientation == 1) {
        cfg.orientation = 2;
      } else if (cfg.orientation == 2) {
        cfg.orientation = 1;
      }
      migratedOrient = true;  // stamp orMap on save so we don't re-migrate
    }
    cfg.clockIntervalMs = prefs.getUShort("clockMs", cfg.clockIntervalMs);
    // Migrate old 1 Hz default — it blocked the USB link with huge partials.
    if (cfg.clockIntervalMs < 5000) {
      cfg.clockIntervalMs = 15000;
    }
    cfg.haDiscovery = prefs.getBool("haDisc", cfg.haDiscovery);
    {
      const uint8_t layoutRaw =
          prefs.getUChar("layout", static_cast<uint8_t>(LayoutMode::Grid));
      cfg.layoutMode = (layoutRaw == static_cast<uint8_t>(LayoutMode::Carousel))
                           ? LayoutMode::Carousel
                           : LayoutMode::Grid;
    }
    cfg.carouselIntervalMs = prefs.getUShort("carMs", cfg.carouselIntervalMs);
    cfg.testPattern = prefs.getBool("testPat", cfg.testPattern);
    cfg.weatherLat = prefs.getFloat("wLat", cfg.weatherLat);
    cfg.weatherLon = prefs.getFloat("wLon", cfg.weatherLon);
    cfg.weatherTz = prefs.getString("wTz", cfg.weatherTz);
    cfg.weatherFahrenheit = prefs.getBool("wF", cfg.weatherFahrenheit);

    for (int i = 0; i < kMaxWidgets; ++i) {
      cfg.widgetTopics[i] = prefs.getString(("wTopic" + String(i)).c_str(), cfg.widgetTopics[i]);
      cfg.widgetLabels[i] = prefs.getString(("wLabel" + String(i)).c_str(), cfg.widgetLabels[i]);
      cfg.widgetTypes[i] = prefs.getString(("wType" + String(i)).c_str(), cfg.widgetTypes[i]);
      cfg.widgetPaths[i] = prefs.getString(("wPath" + String(i)).c_str(), cfg.widgetPaths[i]);
      cfg.widgetTz[i] = prefs.getString(("wWtz" + String(i)).c_str(), cfg.widgetTz[i]);
    }
    prefs.end();
  } else {
    TURZX_LOGI("NVS empty — using secrets.h / defaults");
  }

  applyWidgetDefaults(cfg);
  applySecrets(cfg);
  cfg_ = cfg;

  if ((!haveNvs && !cfg.wifiSsid.isEmpty()) || (haveNvs && migratedOrient)) {
    save(cfg);
  }
  return true;
}

bool ConfigStore::save(const AppConfig &cfg) {
  Preferences prefs;
  if (!prefs.begin(kNs, false)) {
    return false;
  }
  prefs.putString("wifiSsid", cfg.wifiSsid);
  prefs.putString("wifiPass", cfg.wifiPassword);
  prefs.putString("mqttHost", cfg.mqttHost);
  prefs.putUShort("mqttPort", cfg.mqttPort);
  prefs.putString("mqttUser", cfg.mqttUser);
  prefs.putString("mqttPass", cfg.mqttPassword);
  prefs.putString("mqttCid", cfg.mqttClientId);
  prefs.putString("haStatus", cfg.haStatusTopic);
  prefs.putUChar("brightness", cfg.brightness);
  prefs.putUChar("orient", cfg.orientation);
  prefs.putUChar("orMap", 1);
  prefs.putUShort("clockMs", cfg.clockIntervalMs);
  prefs.putBool("haDisc", cfg.haDiscovery);
  prefs.putUChar("layout", static_cast<uint8_t>(cfg.layoutMode));
  prefs.putUShort("carMs", cfg.carouselIntervalMs);
  prefs.putBool("testPat", cfg.testPattern);
  prefs.putFloat("wLat", cfg.weatherLat);
  prefs.putFloat("wLon", cfg.weatherLon);
  prefs.putString("wTz", cfg.weatherTz);
  prefs.putBool("wF", cfg.weatherFahrenheit);
  for (int i = 0; i < kMaxWidgets; ++i) {
    prefs.putString(("wTopic" + String(i)).c_str(), cfg.widgetTopics[i]);
    prefs.putString(("wLabel" + String(i)).c_str(), cfg.widgetLabels[i]);
    prefs.putString(("wType" + String(i)).c_str(), cfg.widgetTypes[i]);
    prefs.putString(("wPath" + String(i)).c_str(), cfg.widgetPaths[i]);
    prefs.putString(("wWtz" + String(i)).c_str(), cfg.widgetTz[i]);
  }
  prefs.end();
  cfg_ = cfg;
  return true;
}

String ConfigStore::toJson(bool includeSecrets) const {
  JsonDocument doc;
  doc["wifiSsid"] = cfg_.wifiSsid;
  if (includeSecrets) {
    doc["wifiPassword"] = cfg_.wifiPassword;
    doc["mqttPassword"] = cfg_.mqttPassword;
  }
  doc["mqttHost"] = cfg_.mqttHost;
  doc["mqttPort"] = cfg_.mqttPort;
  doc["mqttUser"] = cfg_.mqttUser;
  doc["mqttClientId"] = cfg_.mqttClientId;
  doc["haStatusTopic"] = cfg_.haStatusTopic;
  doc["brightness"] = cfg_.brightness;
  doc["orientation"] = cfg_.orientation;
  doc["clockIntervalMs"] = cfg_.clockIntervalMs;
  doc["haDiscovery"] = cfg_.haDiscovery;
  doc["layoutMode"] = layoutModeToString(cfg_.layoutMode);
  doc["carouselIntervalMs"] = cfg_.carouselIntervalMs;
  doc["testPattern"] = cfg_.testPattern;
  doc["weatherLat"] = cfg_.weatherLat;
  doc["weatherLon"] = cfg_.weatherLon;
  doc["weatherTz"] = cfg_.weatherTz;
  doc["weatherFahrenheit"] = cfg_.weatherFahrenheit;

  JsonArray types = doc["widgetTypes"].to<JsonArray>();
  JsonArray topics = doc["widgetTopics"].to<JsonArray>();
  JsonArray labels = doc["widgetLabels"].to<JsonArray>();
  JsonArray paths = doc["widgetPaths"].to<JsonArray>();
  JsonArray tzs = doc["widgetTz"].to<JsonArray>();
  for (int i = 0; i < kMaxWidgets; ++i) {
    types.add(cfg_.widgetTypes[i]);
    topics.add(cfg_.widgetTopics[i]);
    labels.add(cfg_.widgetLabels[i]);
    paths.add(cfg_.widgetPaths[i]);
    tzs.add(cfg_.widgetTz[i]);
  }
  doc["firmware"] = FIRMWARE_VERSION;
  String out;
  serializeJson(doc, out);
  return out;
}

bool ConfigStore::applyJson(const String &json, bool saveToFlash) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  AppConfig next = cfg_;
  if (doc["wifiSsid"].is<const char *>()) {
    next.wifiSsid = doc["wifiSsid"].as<String>();
  }
  if (doc["wifiPassword"].is<const char *>()) {
    next.wifiPassword = doc["wifiPassword"].as<String>();
  }
  if (doc["mqttHost"].is<const char *>()) {
    next.mqttHost = doc["mqttHost"].as<String>();
  }
  if (doc["mqttPort"].is<uint16_t>() || doc["mqttPort"].is<int>()) {
    next.mqttPort = doc["mqttPort"].as<uint16_t>();
  }
  if (doc["mqttUser"].is<const char *>()) {
    next.mqttUser = doc["mqttUser"].as<String>();
  }
  if (doc["mqttPassword"].is<const char *>()) {
    next.mqttPassword = doc["mqttPassword"].as<String>();
  }
  if (doc["mqttClientId"].is<const char *>()) {
    next.mqttClientId = doc["mqttClientId"].as<String>();
  }
  if (doc["haStatusTopic"].is<const char *>()) {
    next.haStatusTopic = doc["haStatusTopic"].as<String>();
  }
  if (doc["brightness"].is<uint8_t>() || doc["brightness"].is<int>()) {
    next.brightness = doc["brightness"].as<uint8_t>();
  }
  if (doc["orientation"].is<uint8_t>() || doc["orientation"].is<int>()) {
    next.orientation = doc["orientation"].as<uint8_t>();
    if (next.orientation > 3) {
      next.orientation = 1;
    }
  }
  if (doc["clockIntervalMs"].is<uint16_t>() || doc["clockIntervalMs"].is<int>()) {
    next.clockIntervalMs = doc["clockIntervalMs"].as<uint16_t>();
    if (next.clockIntervalMs < 5000) {
      next.clockIntervalMs = 5000;
    }
  }
  if (doc["haDiscovery"].is<bool>()) {
    next.haDiscovery = doc["haDiscovery"].as<bool>();
  }
  if (doc["layoutMode"].is<const char *>()) {
    next.layoutMode = layoutModeFromString(doc["layoutMode"].as<String>());
  }
  if (doc["carouselIntervalMs"].is<uint16_t>() || doc["carouselIntervalMs"].is<int>()) {
    long carMs = doc["carouselIntervalMs"].as<long>();
    if (carMs < 2000) {
      carMs = 2000;
    } else if (carMs > 65535) {
      carMs = 65535;
    }
    next.carouselIntervalMs = static_cast<uint16_t>(carMs);
  }
  if (doc["testPattern"].is<bool>()) {
    next.testPattern = doc["testPattern"].as<bool>();
  } else if (doc["testPattern"].is<int>()) {
    next.testPattern = doc["testPattern"].as<int>() != 0;
  } else if (doc["testPattern"].is<const char *>()) {
    const String v = doc["testPattern"].as<String>();
    next.testPattern = v.equalsIgnoreCase("true") || v == "1";
  }
  if (doc["weatherLat"].is<float>() || doc["weatherLat"].is<double>() || doc["weatherLat"].is<int>()) {
    next.weatherLat = doc["weatherLat"].as<float>();
  }
  if (doc["weatherLon"].is<float>() || doc["weatherLon"].is<double>() || doc["weatherLon"].is<int>()) {
    next.weatherLon = doc["weatherLon"].as<float>();
  }
  if (doc["weatherTz"].is<const char *>()) {
    next.weatherTz = doc["weatherTz"].as<String>();
  }
  if (doc["weatherFahrenheit"].is<bool>()) {
    next.weatherFahrenheit = doc["weatherFahrenheit"].as<bool>();
  }

  auto fillStringArray = [](JsonDocument &d, const char *key, String *out) {
    if (!d[key].is<JsonArray>()) {
      return;
    }
    JsonArray arr = d[key].as<JsonArray>();
    for (size_t i = 0; i < arr.size() && i < kMaxWidgets; ++i) {
      out[i] = arr[i].as<String>();
    }
  };
  fillStringArray(doc, "widgetTopics", next.widgetTopics);
  fillStringArray(doc, "widgetLabels", next.widgetLabels);
  fillStringArray(doc, "widgetTypes", next.widgetTypes);
  fillStringArray(doc, "widgetPaths", next.widgetPaths);
  fillStringArray(doc, "widgetTz", next.widgetTz);

  cfg_ = next;
  if (saveToFlash) {
    return save(cfg_);
  }
  return true;
}
