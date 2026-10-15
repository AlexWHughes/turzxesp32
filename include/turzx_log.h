#pragma once

#include <Arduino.h>
#include <esp_log.h>

// USB monitor shows ESP_LOG (same path as ArduinoOTA). HWCDC Serial.println
// often never appears on /dev/cu.usbmodem — prefer these macros for boot diag.
#define TURZX_LOGI(fmt, ...) ESP_LOGI("turzx", fmt, ##__VA_ARGS__)
#define TURZX_LOGW(fmt, ...) ESP_LOGW("turzx", fmt, ##__VA_ARGS__)
#define TURZX_LOGE(fmt, ...) ESP_LOGE("turzx", fmt, ##__VA_ARGS__)
