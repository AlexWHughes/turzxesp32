#include "web_ui.h"

#include <LittleFS.h>
#include <WiFi.h>

#include "http_server.h"
#include "web_ui_page.h"

namespace {
WebUi *gUi = nullptr;
ConfigChangedHandler gOnConfig = nullptr;
BrightnessHandler gOnBrightness = nullptr;
}  // namespace

void WebUi::begin(ConfigStore &store, ConfigChangedHandler onConfig,
                  BrightnessHandler onBrightness) {
  store_ = &store;
  onConfig_ = onConfig;
  onBrightness_ = onBrightness;
  gUi = this;
  gOnConfig = onConfig;
  gOnBrightness = onBrightness;

  // LittleFS is optional now — UI is embedded in firmware.
  if (!LittleFS.begin(true)) {
    Serial.println("[web] LittleFS mount failed (using embedded UI)");
  }

  WebServer &server = httpServer();

  server.on("/", HTTP_GET, []() {
    // Always serve firmware-embedded UI (avoids empty/stale LittleFS pages).
    httpServer().send_P(200, "text/html", kWebUiPage);
  });

  server.on("/api/status", HTTP_GET, []() {
    WebServer &s = httpServer();
    IPAddress ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
    String body = "{\"status\":\"";
    body += gUi ? gUi->status() : "unknown";
    body += "\",\"ip\":\"";
    body += ip.toString();
    body += "\",\"firmware\":\"" FIRMWARE_VERSION "\",\"diag\":\"";
    if (gUi) {
      String d = gUi->diag();
      d.replace("\\", "\\\\");
      d.replace("\"", "\\\"");
      body += d;
    }
    body += "\"}";
    s.send(200, "application/json", body);
  });

  server.on("/api/config", HTTP_GET, []() {
    WebServer &s = httpServer();
    if (!gUi || !gUi->store()) {
      s.send(500, "application/json", "{\"error\":\"no store\"}");
      return;
    }
    s.send(200, "application/json", gUi->store()->toJson(false));
  });

  server.on("/api/config", HTTP_POST, []() {
    WebServer &s = httpServer();
    if (!gUi || !gUi->store()) {
      s.send(500, "application/json", "{\"error\":\"no store\"}");
      return;
    }
    String body = s.arg("plain");
    if (body.isEmpty()) {
      body = s.arg(0);
    }
    if (!gUi->store()->applyJson(body, true)) {
      s.send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }
    if (gOnConfig) {
      gOnConfig(gUi->store()->get());
    }
    if (gOnBrightness) {
      gOnBrightness(gUi->store()->get().brightness);
    }
    s.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/brightness", HTTP_POST, []() {
    WebServer &s = httpServer();
    if (!s.hasArg("value") || !gUi || !gUi->store()) {
      s.send(400, "application/json", "{\"error\":\"value required\"}");
      return;
    }
    uint8_t v = static_cast<uint8_t>(s.arg("value").toInt());
    AppConfig cfg = gUi->store()->get();
    cfg.brightness = v;
    gUi->store()->save(cfg);
    if (gOnBrightness) {
      gOnBrightness(v);
    }
    s.send(200, "application/json", "{\"ok\":true}");
  });

  server.serveStatic("/assets/", LittleFS, "/");
  server.begin();
  Serial.println("[web] HTTP server on :80 (embedded UI)");
}

void WebUi::loop() { httpServer().handleClient(); }

void WebUi::setStatus(const String &status) { status_ = status; }

void WebUi::setDiag(const String &diag) { diag_ = diag; }
