#include "ota_service.h"

#include <ArduinoOTA.h>
#include <Update.h>
#include <WiFi.h>

#include "http_server.h"

namespace {
bool otaPageRegistered = false;

void registerHttpOta() {
  if (otaPageRegistered) {
    return;
  }
  otaPageRegistered = true;
  WebServer &server = httpServer();

  server.on("/update", HTTP_GET, []() {
    httpServer().send(200, "text/html",
                      "<!DOCTYPE html><html><body style='font-family:sans-serif;background:#0b1220;color:#e8eef8;"
                      "padding:24px'>"
                      "<h1>TURZX OTA</h1>"
                      "<form method='POST' action='/update' enctype='multipart/form-data'>"
                      "<input type='file' name='firmware' accept='.bin'>"
                      "<button type='submit'>Upload firmware</button></form>"
                      "<p>ArduinoOTA is also enabled on the network hostname.</p>"
                      "</body></html>");
  });

  server.on(
      "/update", HTTP_POST,
      []() {
        httpServer().sendHeader("Connection", "close");
        httpServer().send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
        delay(500);
        ESP.restart();
      },
      []() {
        WebServer &s = httpServer();
        HTTPUpload &upload = s.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("[ota] Update: %s\n", upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
            Serial.printf("[ota] Success: %u bytes\n", upload.totalSize);
          } else {
            Update.printError(Serial);
          }
        }
      });
}
}  // namespace

void OtaService::begin() {
  ArduinoOTA.setHostname("turzx-esp32");
  ArduinoOTA
      .onStart([]() { Serial.println("[ota] ArduinoOTA start"); })
      .onEnd([]() { Serial.println("\n[ota] ArduinoOTA end"); })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[ota] Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) { Serial.printf("[ota] Error[%u]\n", error); });
  ArduinoOTA.begin();
  registerHttpOta();
  Serial.println("[ota] ArduinoOTA + /update HTTP endpoint ready");
}

void OtaService::loop() { ArduinoOTA.handle(); }
