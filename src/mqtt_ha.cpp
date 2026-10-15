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
  cfg_ = cfg;
  mqtt.disconnect();
  mqtt.setServer(cfg_.mqttHost.c_str(), cfg_.mqttPort);
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
    mqtt.publish("turzx/status", "online", true);
    subscribeAll();
    if (cfg_.haDiscovery) {
      publishHaDiscovery();
    }
  } else {
    Serial.printf("[mqtt] failed rc=%d\n", mqtt.state());
  }
}

void MqttHa::loop() {
  if (!started_) {
    return;
  }
  static uint32_t lastTry = 0;
  if (!mqtt.connected()) {
    if (millis() - lastTry > cfg_.mqttReconnectMs) {
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
