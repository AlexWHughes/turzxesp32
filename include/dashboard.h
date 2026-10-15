#pragma once

#include <Arduino.h>
#include <vector>

#include "config_store.h"
#include "turzx_rev_c.h"

struct DashboardWidget {
  String id;
  String label;
  String value;
  String topic;
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 280;
  int16_t h = 90;
  bool dirty = true;
};

class Dashboard {
 public:
  bool begin(TurzxRevC &display, const AppConfig &cfg);
  void setDisplay(TurzxRevC *display) { display_ = display; }
  void applyConfig(const AppConfig &cfg);
  void setWidgetValue(const String &topic, const String &value);
  void setClock(const String &clockText);
  void tick();
  void forceRedraw();

 private:
  void layoutDefaults();
  void renderWidget(const DashboardWidget &w);
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawText(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t scale);
  void blitRegion(int16_t x, int16_t y, int16_t w, int16_t h);

  TurzxRevC *display_ = nullptr;
  std::vector<DashboardWidget> widgets_;
  DashboardWidget clockWidget_;
  uint16_t *canvas_ = nullptr;
  uint16_t canvasW_ = 0;
  uint16_t canvasH_ = 0;
  bool fullDirty_ = true;
  uint32_t lastClockMs_ = 0;
};
