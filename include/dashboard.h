#pragma once

#include <Arduino.h>
#include <vector>

#include "config_store.h"
#include "turzx_rev_c.h"

struct DashboardWidget {
  String id;
  String label;
  String value;
  String secondary;
  String topic;
  String type;
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 280;
  int16_t h = 90;
  bool dirty = true;
  bool active = false;
};

class Dashboard {
 public:
  bool begin(TurzxRevC &display, const AppConfig &cfg);
  void setDisplay(TurzxRevC *display) { display_ = display; }
  void applyConfig(const AppConfig &cfg);
  void setSlot(int index, const String &label, const String &value, const String &secondary);
  void setClock(const String &clockText);
  void tick();
  void forceRedraw();

 private:
  bool ensureCanvas();
  void layoutWidgets();
  void renderWidget(const DashboardWidget &w, bool large);
  void renderTestPattern();
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void drawText(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t scale);
  void blitRegion(int16_t x, int16_t y, int16_t w, int16_t h);
  int activeWidgetCount() const;

  TurzxRevC *display_ = nullptr;
  AppConfig cfg_;
  std::vector<DashboardWidget> widgets_;
  DashboardWidget clockWidget_;
  uint16_t *canvas_ = nullptr;
  // Reused PSRAM scratch for partial blits — avoids per-tick DRAM vector allocs
  // that fragment the heap and look like a leak.
  uint16_t *blitScratch_ = nullptr;
  size_t blitScratchPixels_ = 0;
  uint16_t canvasW_ = 0;
  uint16_t canvasH_ = 0;
  bool fullDirty_ = true;
  int carouselIndex_ = 0;
  uint32_t lastCarouselMs_ = 0;
};
