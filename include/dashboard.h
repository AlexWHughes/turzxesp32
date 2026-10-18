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
  uint32_t dirtySeq = 0;
};

class Dashboard {
 public:
  bool begin(TurzxRevC &display, const AppConfig &cfg);
  void setDisplay(TurzxRevC *display) { display_ = display; }
  void applyConfig(const AppConfig &cfg);
  void setSlot(int index, const String &label, const String &value, const String &secondary);
  void setAlbumArt(const uint16_t *pixels, int16_t width, int16_t height);
  void clearAlbumArt();
  void setClock(const String &clockText);
  void tick();
  void forceRedraw();

 private:
  bool ensureCanvas();
  void layoutWidgets();
  void renderWidget(const DashboardWidget &w, bool large);
  void renderTestPattern();
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void setPixel(int16_t x, int16_t y, uint16_t color);
  void drawText(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t scale,
                int16_t maxX);
  void blitArt(int16_t x, int16_t y, int16_t w, int16_t h);
  bool blitRegion(int16_t x, int16_t y, int16_t w, int16_t h);
  void paintFullCanvas();
  void markSpotifyDirty();
  void loadTheme();
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
  // Full frames go out as horizontal strips so the first strip shows up
  // without waiting for a multi-megabyte USB transfer.
  int16_t fullBandY_ = -1;
  int carouselIndex_ = 0;
  uint32_t lastCarouselMs_ = 0;
  uint32_t nextDirtySeq_ = 1;
  uint16_t themeBg_ = 0x0884;
  uint16_t themePanel_ = 0x1989;
  uint16_t themeAccent_ = 0x7A9F;
  uint16_t themeText_ = 0xF79F;
  uint16_t themeMuted_ = 0x9D14;
  uint16_t themeValue_ = 0x5F5A;
  uint16_t *art_ = nullptr;
  int16_t artW_ = 0;
  int16_t artH_ = 0;

  void markDirty(DashboardWidget &widget) {
    widget.dirty = true;
    widget.dirtySeq = nextDirtySeq_++;
  }
};
