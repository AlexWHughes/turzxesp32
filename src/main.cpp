#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config_store.h"
#include "dashboard.h"
#include "mqtt_ha.h"
#include "ota_service.h"
#include "turzx_factory.h"
#include "turzx_log.h"
#include "turzx_rev_c.h"
#include "web_ui.h"
#include "widget_engine.h"
#include "wifi_service.h"

namespace {
ConfigStore configStore;
WifiService wifiService;
WebUi webUi;
OtaService otaService;
MqttHa mqttHa;
WidgetEngine widgetEngine;
TurzxTransport *transport = nullptr;
TurzxRevC *display = nullptr;
Dashboard dashboard;
uint32_t lastClockMs = 0;
uint32_t lastPanelRetryMs = 0;
bool panelReady = false;
bool panelAttempted = false;
}  // namespace

static TurzxOrientation orientationFromConfig(uint8_t value) {
  switch (value) {
    case 0:
      return TurzxOrientation::Portrait;
    case 1:
      return TurzxOrientation::ReversePortrait;
    case 2:
      return TurzxOrientation::Landscape;
    case 3:
      return TurzxOrientation::ReverseLandscape;
    default: {
      const uint8_t unreachable = value;
      (void)unreachable;
      return TurzxOrientation::Landscape;
    }
  }
}

static void applyRuntimeConfig(const AppConfig &cfg) {
  const bool orientationChanged =
      display && display->isReady() &&
      display->orientation() != orientationFromConfig(cfg.orientation);
  if (display && display->isReady()) {
    display->setBrightness(cfg.brightness);
    display->setOrientation(orientationFromConfig(cfg.orientation));
  }
  dashboard.applyConfig(cfg);
  widgetEngine.applyConfig(cfg);
  // Orientation resize needs a full frame; other saves dirty widgets themselves.
  if (orientationChanged) {
    dashboard.forceRedraw();
  }
  mqttHa.applyConfig(cfg);
}

static String currentClockString() {
  struct tm timeinfo {};
  if (!getLocalTime(&timeinfo, 50)) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (millis() / 3600000UL) % 24,
             (millis() / 60000UL) % 60);
    return String(buf);
  }
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  return String(buf);
}

static void tryInitPanel() {
  if (panelReady || !display) {
    return;
  }
  webUi.setStatus("panel-probing");
  const bool ok = display->begin();
  panelAttempted = true;
  if (transport) {
    webUi.setDiag(transport->diag());
  }
  if (!ok) {
    TURZX_LOGW("Panel not ready yet — WebUI stays up, will retry");
    webUi.setStatus("running-no-panel");
    return;
  }

  panelReady = true;
  const AppConfig &cfg = configStore.get();
  display->setOrientation(orientationFromConfig(cfg.orientation));
  TURZX_LOGI("Panel ready: %s", display->displayId().c_str());
  webUi.setStatus("panel-ready");
  if (!dashboard.begin(*display, cfg)) {
    TURZX_LOGE("Dashboard canvas alloc failed");
    display->screenOff();
    panelReady = false;
    webUi.setStatus("running-no-panel");
    return;
  }
  widgetEngine.applyConfig(cfg);
  dashboard.forceRedraw();
  dashboard.tick();
  display->setBrightness(cfg.brightness);
  webUi.setStatus("running");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  TURZX_LOGI("TURZX ESP32-S3 dashboard %s", FIRMWARE_VERSION);

  configStore.begin();
  AppConfig cfg = configStore.get();
  TURZX_LOGI("WiFi target SSID='%s' (len=%u)", cfg.wifiSsid.c_str(),
             (unsigned)cfg.wifiSsid.length());

  wifiService.begin(cfg);
  if (wifiService.isStaConnected()) {
    TURZX_LOGI("STA up — open http://%s/", WiFi.localIP().toString().c_str());
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  } else {
    TURZX_LOGW("STA failed — AP Turzx-Setup / turzxsetup @ http://192.168.4.1/");
  }

  widgetEngine.begin(dashboard);
  widgetEngine.applyConfig(cfg);

  webUi.begin(
      configStore,
      [](const AppConfig &updated) {
        applyRuntimeConfig(updated);
        webUi.setStatus("config-updated");
      },
      [](uint8_t brightness) {
        if (display && display->isReady()) {
          display->setBrightness(brightness);
        }
      });
  otaService.begin();
  webUi.setStatus("wifi-up");

  transport = createTurzxTransport();
  display = new TurzxRevC(*transport);

  mqttHa.begin(cfg, [](const String &topic, const String &payload) {
    if (topic == configStore.get().haStatusTopic && payload == "online") {
      mqttHa.publishHaDiscovery();
    }
    widgetEngine.onMqttMessage(topic, payload);
  });

#if TURZX_LOG_UART
  TURZX_LOGI("UART logging — starting panel USB host now");
#else
  TURZX_LOGI("Panel USB host starts at t=20s (OTG serial will drop; use WebUI)");
#endif
  webUi.setStatus("running-no-panel");
}

void loop() {
  wifiService.loop();
  webUi.loop();
  otaService.loop();
  mqttHa.loop();
  widgetEngine.loop();

  if (panelReady && transport && !transport->isReady()) {
    TURZX_LOGW("Panel link lost — it will sleep instead of holding the stored image");
    panelReady = false;
    panelAttempted = true;
    lastPanelRetryMs = millis();
    if (display) {
      display->end();
    }
    webUi.setStatus("running-no-panel");
  }

  if (!panelReady) {
    const uint32_t now = millis();
#if TURZX_LOG_UART
    const bool pastBootWindow = true;
#else
    const bool pastBootWindow = now >= 20000;
#endif
    if (pastBootWindow && (!panelAttempted || (now - lastPanelRetryMs) >= 5000)) {
      lastPanelRetryMs = now;
      tryInitPanel();
      if (transport) {
        webUi.setDiag(transport->diag());
      }
    }
  }

  const AppConfig &cfg = configStore.get();
  if (panelReady && millis() - lastClockMs >= cfg.clockIntervalMs) {
    lastClockMs = millis();
    dashboard.setClock(currentClockString());
  }
  if (panelReady) {
    dashboard.tick();
  }
  delay(5);
}
