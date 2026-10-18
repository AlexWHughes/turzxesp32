#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
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

#ifndef TURZX_SKIP_PANEL
#define TURZX_SKIP_PANEL 0
#endif

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
uint32_t lastHeapLogMs = 0;
bool panelReady = false;
bool panelAttempted = false;

void logHeap(const char *tag) {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t minHeap = ESP.getMinFreeHeap();
  const uint32_t maxAlloc = ESP.getMaxAllocHeap();
  const uint32_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  // Serial.printf — ESP_LOGI is filtered at CORE_DEBUG_LEVEL=1.
  Serial.printf("[heap] %s free=%u min=%u maxAlloc=%u int=%u psram=%u\n", tag, freeHeap, minHeap,
                maxAlloc, internal, spiram);
}
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
        webUi.setStatus(panelReady ? "running" : "running-no-panel");
      },
      [](uint8_t brightness) {
        if (display && display->isReady()) {
          display->setBrightness(brightness);
        }
      });
  otaService.begin();
  webUi.setStatus("wifi-up");

#if TURZX_SKIP_PANEL
  TURZX_LOGI("TURZX_SKIP_PANEL=1 — USB host off so OTG CDC stays up for heap debug");
  webUi.setStatus("heap-debug");
#else
  transport = createTurzxTransport();
  display = new TurzxRevC(*transport);

#if TURZX_LOG_UART
  TURZX_LOGI("UART logging — starting panel USB host now");
#else
  TURZX_LOGI("Panel USB host starts at t=20s (OTG serial will drop; use WebUI)");
#endif
  webUi.setStatus("running-no-panel");
#endif

  mqttHa.begin(cfg, [](const String &topic, const String &payload) {
    if (topic == configStore.get().haStatusTopic && payload == "online") {
      mqttHa.publishHaDiscovery();
    }
    widgetEngine.onMqttMessage(topic, payload);
  });

  logHeap("boot");
}

void loop() {
  wifiService.loop();
  webUi.loop();
  otaService.loop();
  mqttHa.loop();
  widgetEngine.loop();

#if !TURZX_SKIP_PANEL
  if (panelReady && transport && !transport->isReady()) {
    TURZX_LOGW("Panel link lost — will reopen on hotplug");
    panelReady = false;
    panelAttempted = true;
    lastPanelRetryMs = 0;  // retry as soon as the boot window / next poll allows
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
    const bool plugSeen = transport && transport->hotplugPending();
    // Hotplug: react within ~500 ms of a new CDC device. Blind retries stay at 5 s.
    const uint32_t retryMs = plugSeen ? 500 : 5000;
    if (pastBootWindow && (!panelAttempted || plugSeen || (now - lastPanelRetryMs) >= retryMs)) {
      lastPanelRetryMs = now;
      if (transport) {
        transport->poll();
      }
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
#endif

  if (millis() - lastHeapLogMs >= 5000) {
    lastHeapLogMs = millis();
    logHeap("loop");
  }
  delay(5);
}
