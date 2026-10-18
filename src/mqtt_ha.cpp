#include "mqtt_ha.h"

#include <PubSubClient.h>
#include <WiFi.h>

namespace {
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
MqttMessageHandler gHandler;

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t(topic);
  String body;
  body.reserve(length);
  for (unsigned int i = 0; i < length; ++i) {
    body += static_cast<char>(payload[i]);
  }
  if (gHandler) {
    gHandler(t, body);
  }
}
}  // namespace

void MqttHa::begin(const AppConfig &cfg, MqttMessageHandler handler) {
  cfg_ = cfg;
  handler_ = handler;
  gHandler = handler;
  mqtt.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(2048);
  started_ = true;
}

void MqttHa::applyConfig(const AppConfig &cfg) {
  if (cfg_.mqttEnabled && !cfg.mqttEnabled && mqtt.connected()) {
    // Clean disconnect does not send the LWT; clear retained online before drop.
    mqtt.publish("turzx/status", "offline", true);
  }
  cfg_ = cfg;
  mqtt.disconnect();
  mqtt.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
  failStreak_ = 0;
  reconnectDelayMs_ = 0;
}

void MqttHa::subscribeAll() {
  for (int i = 0; i < kMaxWidgets; ++i) {
    const String &type = cfg_.widgetTypes[i];
    if (type != "mqtt_text" && type != "mqtt_json") {
      continue;
    }
    if (cfg_.widgetTopics[i].length()) {
      mqtt.subscribe(cfg_.widgetTopics[i].c_str());
      Serial.printf("[mqtt] subscribe %s\n", cfg_.widgetTopics[i].c_str());
    }
  }
  if (cfg_.haStatusTopic.length()) {
    mqtt.subscribe(cfg_.haStatusTopic.c_str());
  }
}

void MqttHa::ensureConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (mqtt.connected()) {
    return;
  }
  Serial.printf("[mqtt] Connecting to %s:%u\n", cfg_.mqttHost.c_str(), cfg_.mqttPort);
  bool ok;
  if (cfg_.mqttUser.length()) {
    ok = mqtt.connect(cfg_.mqttClientId.c_str(), cfg_.mqttUser.c_str(), cfg_.mqttPassword.c_str(),
                      "turzx/status", 0, true, "offline");
  } else {
    ok = mqtt.connect(cfg_.mqttClientId.c_str(), "turzx/status", 0, true, "offline");
  }
  if (ok) {
    Serial.println("[mqtt] connected");
    failStreak_ = 0;
    reconnectDelayMs_ = 0;
    mqtt.publish("turzx/status", "online", true);
    subscribeAll();
    if (cfg_.haDiscovery) {
      publishHaDiscovery();
    }
  } else {
    Serial.printf("[mqtt] failed rc=%d\n", mqtt.state());
    if (failStreak_ < 12) {
      ++failStreak_;
    }
    // Auth failures (rc=4/5) will not self-heal — back off hard to avoid DNS/TLS churn.
    const uint32_t base = cfg_.mqttReconnectMs ? cfg_.mqttReconnectMs : 5000;
    uint32_t delayMs = base << (failStreak_ > 3 ? failStreak_ - 3 : 0);
    if (delayMs > 300000UL) {
      delayMs = 300000UL;
    }
    reconnectDelayMs_ = delayMs;
    Serial.printf("[mqtt] next try in %lu ms\n", static_cast<unsigned long>(reconnectDelayMs_));
  }
}

void MqttHa::loop() {
  if (!started_) {
    return;
  }
  if (!cfg_.mqttEnabled) {
    if (mqtt.connected()) {
      // Clean disconnect does not send the LWT, so publish retained offline first.
      mqtt.publish("turzx/status", "offline", true);
      mqtt.disconnect();
    }
    return;
  }
  static uint32_t lastTry = 0;
  if (!mqtt.connected()) {
    const uint32_t waitMs = reconnectDelayMs_ ? reconnectDelayMs_ : cfg_.mqttReconnectMs;
    if (millis() - lastTry > waitMs) {
      lastTry = millis();
      ensureConnected();
    }
    return;
  }
  mqtt.loop();
}

bool MqttHa::isConnected() const { return mqtt.connected(); }

void MqttHa::publishStatus(const String &payload) {
  if (mqtt.connected()) {
    mqtt.publish("turzx/status", payload.c_str(), true);
  }
}

void MqttHa::publishHaDiscovery() {
  String topic = "homeassistant/binary_sensor/" + cfg_.mqttClientId + "/connectivity/config";
  String payload =
      "{\"name\":\"TURZX Display\",\"unique_id\":\"" + cfg_.mqttClientId +
      "_online\",\"state_topic\":\"turzx/status\",\"payload_on\":\"online\",\"payload_off\":"
      "\"offline\",\"device_class\":\"connectivity\",\"device\":{\"identifiers\":[\"" +
      cfg_.mqttClientId +
      "\"],\"name\":\"TURZX ESP32\",\"model\":\"8.8 Rev C\",\"manufacturer\":\"DIY\"}}";
  mqtt.publish(topic.c_str(), payload.c_str(), true);
}
