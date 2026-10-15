#include "wifi_service.h"

#include <WiFi.h>

#include "turzx_log.h"

bool WifiService::begin(const AppConfig &cfg) {
  WiFi.mode(WIFI_STA);
  if (cfg.wifiSsid.length() > 0) {
    TURZX_LOGI("Connecting to %s...", cfg.wifiSsid.c_str());
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) {
      apMode_ = false;
      TURZX_LOGI("STA IP %s", WiFi.localIP().toString().c_str());
      return true;
    }
    TURZX_LOGW("STA connect failed (status=%d)", (int)WiFi.status());
  } else {
    TURZX_LOGW("No WiFi SSID configured");
  }
  return startAp();
}

bool WifiService::startAp() {
  apMode_ = true;
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP("Turzx-Setup", "turzxsetup");
  TURZX_LOGI("AP mode SSID=Turzx-Setup IP=%s", WiFi.softAPIP().toString().c_str());
  return ok;
}

void WifiService::loop() {
  if (!apMode_ && WiFi.status() != WL_CONNECTED) {
    // Keep trying STA; WebUI still reachable only if we fall back to AP.
  }
}

bool WifiService::isStaConnected() const { return !apMode_ && WiFi.status() == WL_CONNECTED; }

IPAddress WifiService::localIp() const {
  return apMode_ ? WiFi.softAPIP() : WiFi.localIP();
}
