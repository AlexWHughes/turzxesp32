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
  // secrets.h wins when filled in (skip the example placeholder).
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
  if (cfg.widgetLabels[0].isEmpty()) {
    cfg.widgetLabels[0] = "Living Room";
    cfg.widgetTopics[0] = "homeassistant/sensor/living_temp/state";
    cfg.widgetLabels[1] = "Outdoor";
    cfg.widgetTopics[1] = "homeassistant/sensor/outdoor_temp/state";
    cfg.widgetLabels[2] = "Power";
    cfg.widgetTopics[2] = "homeassistant/sensor/house_power/state";
  }
}

bool ConfigStore::load(AppConfig &cfg) {
  Preferences prefs;
  // Read-only open fails with NOT_FOUND on first boot — still apply secrets.
  const bool haveNvs = prefs.begin(kNs, true);
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
    cfg.clockIntervalMs = prefs.getUShort("clockMs", cfg.clockIntervalMs);
    cfg.haDiscovery = prefs.getBool("haDisc", cfg.haDiscovery);

    for (int i = 0; i < 6; ++i) {
      cfg.widgetTopics[i] = prefs.getString(("wTopic" + String(i)).c_str(), cfg.widgetTopics[i]);
      cfg.widgetLabels[i] = prefs.getString(("wLabel" + String(i)).c_str(), cfg.widgetLabels[i]);
    }
    prefs.end();
  } else {
    TURZX_LOGI("NVS empty — using secrets.h / defaults");
  }

  applyWidgetDefaults(cfg);
  applySecrets(cfg);
  cfg_ = cfg;

  // Persist secrets on first boot so later NVS opens succeed.
  if (!haveNvs && !cfg.wifiSsid.isEmpty()) {
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
  prefs.putUShort("clockMs", cfg.clockIntervalMs);
  prefs.putBool("haDisc", cfg.haDiscovery);
  for (int i = 0; i < 6; ++i) {
    prefs.putString(("wTopic" + String(i)).c_str(), cfg.widgetTopics[i]);
    prefs.putString(("wLabel" + String(i)).c_str(), cfg.widgetLabels[i]);
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
  JsonArray topics = doc["widgetTopics"].to<JsonArray>();
  JsonArray labels = doc["widgetLabels"].to<JsonArray>();
  for (int i = 0; i < 6; ++i) {
    topics.add(cfg_.widgetTopics[i]);
    labels.add(cfg_.widgetLabels[i]);
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
  if (doc["mqttPort"].is<uint16_t>()) {
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
  if (doc["brightness"].is<uint8_t>()) {
    next.brightness = doc["brightness"].as<uint8_t>();
  }
  if (doc["orientation"].is<uint8_t>()) {
    next.orientation = doc["orientation"].as<uint8_t>();
  }
  if (doc["clockIntervalMs"].is<uint16_t>()) {
    next.clockIntervalMs = doc["clockIntervalMs"].as<uint16_t>();
  }
  if (doc["haDiscovery"].is<bool>()) {
    next.haDiscovery = doc["haDiscovery"].as<bool>();
  }
  if (doc["widgetTopics"].is<JsonArray>()) {
    JsonArray arr = doc["widgetTopics"].as<JsonArray>();
    for (size_t i = 0; i < arr.size() && i < 6; ++i) {
      next.widgetTopics[i] = arr[i].as<String>();
    }
  }
  if (doc["widgetLabels"].is<JsonArray>()) {
    JsonArray arr = doc["widgetLabels"].as<JsonArray>();
    for (size_t i = 0; i < arr.size() && i < 6; ++i) {
      next.widgetLabels[i] = arr[i].as<String>();
    }
  }
  cfg_ = next;
  if (saveToFlash) {
    return save(cfg_);
  }
  return true;
}
