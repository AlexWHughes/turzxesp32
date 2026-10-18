#include "dashboard.h"

#include <cstring>
#include <vector>

#include <esp_heap_caps.h>

namespace {
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_BLUE = 0x001F;
constexpr uint16_t COLOR_YELLOW = 0xFFE0;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_WHITE = 0xFFFF;
constexpr uint16_t COLOR_BLACK = 0x0000;

const uint8_t FONT5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x08, 0x2A, 0x1C, 0x2A, 0x08}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x41, 0x22, 0x14, 0x08, 0x00}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x01, 0x01},
    {0x3E, 0x41, 0x41, 0x51, 0x32}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43},
};
}  // namespace

bool Dashboard::ensureCanvas() {
  if (!display_) {
    return false;
  }
  const uint16_t w = display_->width();
  const uint16_t h = display_->height();
  if (canvas_ && canvasW_ == w && canvasH_ == h) {
    return true;
  }
  if (canvas_) {
    free(canvas_);
    canvas_ = nullptr;
  }
  if (blitScratch_) {
    free(blitScratch_);
    blitScratch_ = nullptr;
    blitScratchPixels_ = 0;
  }
  canvasW_ = w;
  canvasH_ = h;
  const size_t pixels = static_cast<size_t>(canvasW_) * canvasH_;
  const size_t bytes = pixels * sizeof(uint16_t);
  canvas_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!canvas_) {
    canvas_ = static_cast<uint16_t *>(malloc(bytes));
  }
  if (!canvas_) {
    Serial.println("[dash] Failed to allocate canvas");
    canvasW_ = 0;
    canvasH_ = 0;
    return false;
  }
  blitScratch_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!blitScratch_) {
    blitScratch_ = static_cast<uint16_t *>(malloc(bytes));
  }
  blitScratchPixels_ = blitScratch_ ? pixels : 0;
  if (!blitScratch_) {
    Serial.println("[dash] blit scratch alloc failed — partials will use heap");
  }
  for (size_t i = 0; i < static_cast<size_t>(canvasW_) * canvasH_; ++i) {
    canvas_[i] = themeBg_;
  }
  fullDirty_ = true;
  return true;
}

bool Dashboard::begin(TurzxRevC &display, const AppConfig &cfg) {
  display_ = &display;
  if (!ensureCanvas()) {
    return false;
  }
  applyConfig(cfg);
  return true;
}

int Dashboard::activeWidgetCount() const {
  int n = 0;
  for (const auto &w : widgets_) {
    if (w.active) {
      ++n;
    }
  }
  return n;
}

void Dashboard::layoutWidgets() {
  const bool portrait = canvasH_ > canvasW_;
  const int16_t margin = 24;
  const int16_t gap = 16;
  const int16_t clockH = 96;
  auto place = [this](DashboardWidget &widget, int16_t x, int16_t y, int16_t w, int16_t h) {
    if (widget.x == x && widget.y == y && widget.w == w && widget.h == h) {
      return;
    }
    widget.x = x;
    widget.y = y;
    widget.w = w;
    widget.h = h;
    markDirty(widget);
  };
  place(clockWidget_, margin, margin, canvasW_ - margin * 2, clockH);

  const int16_t top = margin + clockH + gap;
  const int16_t areaH = canvasH_ - top - margin;
  int active = activeWidgetCount();
  if (active == 0) {
    return;
  }

  if (cfg_.layoutMode == LayoutMode::Carousel) {
    for (auto &w : widgets_) {
      if (!w.active) {
        continue;
      }
      place(w, margin, top, canvasW_ - margin * 2, areaH);
    }
    return;
  }

  // Grid: 3 columns in landscape, 2 in portrait.
  const int cols = portrait ? 2 : 3;
  const int rows = (active + cols - 1) / cols;
  const int16_t cellW = (canvasW_ - margin * 2 - gap * (cols - 1)) / cols;
  const int16_t cellH = (areaH - gap * (rows - 1)) / max(rows, 1);
  int idx = 0;
  for (auto &w : widgets_) {
    if (!w.active) {
      continue;
    }
    const int col = idx % cols;
    const int row = idx / cols;
    place(w, margin + col * (cellW + gap), top + row * (cellH + gap), cellW, cellH);
    ++idx;
  }
}

void Dashboard::applyConfig(const AppConfig &cfg) {
  const bool themeChanged = cfg.theme != cfg_.theme;
  const bool layoutChanged =
      cfg.layoutMode != cfg_.layoutMode || cfg.testPattern != cfg_.testPattern || themeChanged;
  bool slotsChanged = layoutChanged;
  if (!slotsChanged) {
    // Compare which slots are active / typed — geometry depends on that.
    int oldActive = 0;
    int newActive = 0;
    for (const auto &w : widgets_) {
      if (w.active) {
        ++oldActive;
      }
    }
    for (int i = 0; i < kMaxWidgets; ++i) {
      const String &type = cfg.widgetTypes[i];
      if (!type.isEmpty() && type != "off") {
        ++newActive;
      }
    }
    if (oldActive != newActive) {
      slotsChanged = true;
    } else {
      int wi = 0;
      for (int i = 0; i < kMaxWidgets; ++i) {
        const String &type = cfg.widgetTypes[i];
        if (type.isEmpty() || type == "off") {
          continue;
        }
        if (wi >= static_cast<int>(widgets_.size()) || widgets_[wi].type != type) {
          slotsChanged = true;
          break;
        }
        ++wi;
      }
    }
  }

  // Preserve on-screen values only when the slot's type and topic are unchanged
  // (label-only saves). Type/topic changes must not keep stale MQTT/weather text.
  String preservedValue[kMaxWidgets];
  String preservedSecondary[kMaxWidgets];
  String preservedLabel[kMaxWidgets];
  String preservedType[kMaxWidgets];
  String preservedTopic[kMaxWidgets];
  int16_t preservedX[kMaxWidgets] = {};
  int16_t preservedY[kMaxWidgets] = {};
  int16_t preservedW[kMaxWidgets] = {};
  int16_t preservedH[kMaxWidgets] = {};
  bool preservedHad[kMaxWidgets] = {};
  for (const auto &w : widgets_) {
    if (w.id.length() >= 2 && w.id[0] == 'w') {
      const int idx = w.id.substring(1).toInt();
      if (idx >= 0 && idx < kMaxWidgets) {
        preservedValue[idx] = w.value;
        preservedSecondary[idx] = w.secondary;
        preservedLabel[idx] = w.label;
        preservedType[idx] = w.type;
        preservedTopic[idx] = w.topic;
        preservedX[idx] = w.x;
        preservedY[idx] = w.y;
        preservedW[idx] = w.w;
        preservedH[idx] = w.h;
        preservedHad[idx] = true;
      }
    }
  }
  const String preservedClock = clockWidget_.value;

  cfg_ = cfg;
  loadTheme();
  ensureCanvas();
  widgets_.clear();
  for (int i = 0; i < kMaxWidgets; ++i) {
    const String &type = cfg.widgetTypes[i];
    if (type.isEmpty() || type == "off") {
      continue;
    }
    DashboardWidget w;
    w.id = "w" + String(i);
    w.label = cfg.widgetLabels[i].isEmpty() ? ("Widget " + String(i + 1)) : cfg.widgetLabels[i];
    w.topic = cfg.widgetTopics[i];
    w.type = type;
    const bool sameSource =
        preservedHad[i] && preservedType[i] == type && preservedTopic[i] == cfg.widgetTopics[i];
    if (sameSource) {
      w.value = preservedValue[i].isEmpty() ? "--" : preservedValue[i];
      w.secondary = preservedSecondary[i];
      w.x = preservedX[i];
      w.y = preservedY[i];
      w.w = preservedW[i];
      w.h = preservedH[i];
    } else {
      w.value = "--";
      w.secondary = "";
    }
    w.active = true;
    const bool textChanged = !sameSource || preservedLabel[i] != w.label;
    if (textChanged) {
      markDirty(w);
    }
    widgets_.push_back(w);
  }
  clockWidget_.id = "clock";
  clockWidget_.label = "Local";
  clockWidget_.value = preservedClock.isEmpty() ? "--:--" : preservedClock;
  if (slotsChanged || layoutChanged) {
    carouselIndex_ = 0;
    lastCarouselMs_ = millis();
  }
  layoutWidgets();
  // Orientation/canvas size changes already set fullDirty_ via ensureCanvas().
  // Slot/layout changes need a full frame; label-only saves can partial-blit.
  if (slotsChanged || layoutChanged || cfg.testPattern) {
    fullDirty_ = true;
  }
}

void Dashboard::loadTheme() {
  struct Theme {
    const char *id;
    uint16_t bg;
    uint16_t panel;
    uint16_t accent;
    uint16_t text;
    uint16_t muted;
    uint16_t value;
  };
  auto pack = [](uint8_t r, uint8_t g, uint8_t b) -> uint16_t {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  };
  const Theme themes[] = {
      {"aurora", pack(10, 14, 36), pack(28, 36, 78), pack(124, 92, 255), pack(244, 241, 255),
       pack(154, 163, 199), pack(94, 234, 212)},
      {"ember", pack(20, 10, 8), pack(54, 28, 16), pack(255, 106, 42), pack(255, 244, 234),
       pack(196, 164, 142), pack(255, 210, 122)},
      {"forest", pack(6, 20, 14), pack(16, 46, 30), pack(61, 220, 110), pack(232, 246, 236),
       pack(142, 174, 152), pack(214, 242, 92)},
      {"neon", pack(6, 6, 18), pack(22, 16, 44), pack(255, 43, 214), pack(247, 247, 255),
       pack(157, 157, 190), pack(57, 255, 210)},
      {"paper", pack(244, 239, 230), pack(255, 252, 246), pack(31, 75, 153), pack(26, 26, 26),
       pack(92, 86, 78), pack(14, 107, 79)},
      {"midnight", 0x10A2, 0x2124, 0x07FF, 0xFFFF, 0xAD55, 0x07E0},
  };
  const Theme *chosen = &themes[0];
  for (const Theme &theme : themes) {
    if (cfg_.theme == theme.id) {
      chosen = &theme;
      break;
    }
  }
  themeBg_ = chosen->bg;
  themePanel_ = chosen->panel;
  themeAccent_ = chosen->accent;
  themeText_ = chosen->text;
  themeMuted_ = chosen->muted;
  themeValue_ = chosen->value;
}

void Dashboard::markSpotifyDirty() {
  for (auto &w : widgets_) {
    if (w.active && w.type == "spotify") {
      markDirty(w);
    }
  }
}

void Dashboard::setAlbumArt(const uint16_t *pixels, int16_t width, int16_t height) {
  if (!pixels || width <= 0 || height <= 0 || width > 320 || height > 320) {
    clearAlbumArt();
    return;
  }
  const size_t pixelsN = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (artW_ != width || artH_ != height || !art_) {
    if (art_) {
      free(art_);
      art_ = nullptr;
    }
    art_ = static_cast<uint16_t *>(
        heap_caps_malloc(pixelsN * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!art_) {
      art_ = static_cast<uint16_t *>(malloc(pixelsN * sizeof(uint16_t)));
    }
    if (!art_) {
      artW_ = 0;
      artH_ = 0;
      return;
    }
    artW_ = width;
    artH_ = height;
  }
  memcpy(art_, pixels, pixelsN * sizeof(uint16_t));
  markSpotifyDirty();
}

void Dashboard::clearAlbumArt() {
  if (!art_) {
    return;
  }
  free(art_);
  art_ = nullptr;
  artW_ = 0;
  artH_ = 0;
  markSpotifyDirty();
}

void Dashboard::setSlot(int index, const String &label, const String &value,
                        const String &secondary) {
  const String id = "w" + String(index);
  for (auto &w : widgets_) {
    if (w.id != id) {
      continue;
    }
    const String effectiveLabel = label.isEmpty() ? w.label : label;
    if (w.label != effectiveLabel || w.value != value || w.secondary != secondary) {
      w.label = effectiveLabel;
      w.value = value;
      w.secondary = secondary;
      markDirty(w);
    }
    return;
  }
}

void Dashboard::setClock(const String &clockText) {
  if (clockWidget_.value != clockText) {
    clockWidget_.value = clockText;
    markDirty(clockWidget_);
  }
}

void Dashboard::forceRedraw() {
  fullDirty_ = true;
  fullBandY_ = -1;
}

void Dashboard::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!canvas_ || w <= 0 || h <= 0) {
    return;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > static_cast<int16_t>(canvasW_)) {
    w = static_cast<int16_t>(canvasW_) - x;
  }
  if (y + h > static_cast<int16_t>(canvasH_)) {
    h = static_cast<int16_t>(canvasH_) - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  for (int16_t row = y; row < y + h; ++row) {
    uint16_t *line = canvas_ + static_cast<size_t>(row) * canvasW_ + x;
    for (int16_t col = 0; col < w; ++col) {
      line[col] = color;
    }
  }
}

void Dashboard::setPixel(int16_t x, int16_t y, uint16_t color) {
  if (!canvas_ || x < 0 || y < 0 || x >= static_cast<int16_t>(canvasW_) ||
      y >= static_cast<int16_t>(canvasH_)) {
    return;
  }
  canvas_[static_cast<size_t>(y) * canvasW_ + x] = color;
}

void Dashboard::blitArt(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!art_ || artW_ <= 0 || artH_ <= 0 || w <= 0 || h <= 0) {
    return;
  }
  for (int16_t row = 0; row < h; ++row) {
    const int16_t sy = static_cast<int16_t>((static_cast<int32_t>(row) * artH_) / h);
    const uint16_t *src = art_ + static_cast<size_t>(sy) * artW_;
    for (int16_t col = 0; col < w; ++col) {
      const int16_t sx = static_cast<int16_t>((static_cast<int32_t>(col) * artW_) / w);
      setPixel(x + col, y + row, src[sx]);
    }
  }
}

void Dashboard::drawText(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t scale,
                         int16_t maxX) {
  int16_t cursor = x;
  for (size_t i = 0; i < text.length(); ++i) {
    if (cursor + 6 * scale > maxX) {
      break;
    }
    char c = text[i];
    if (c < 32 || c > 90) {
      if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 32);
      } else {
        cursor += 6 * scale;
        continue;
      }
    }
    const uint8_t *glyph = FONT5X7[c - 32];
    for (uint8_t col = 0; col < 5; ++col) {
      uint8_t bits = glyph[col];
      for (uint8_t row = 0; row < 7; ++row) {
        if (bits & (1 << row)) {
          fillRect(cursor + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    cursor += 6 * scale;
  }
}

void Dashboard::renderWidget(const DashboardWidget &w, bool large) {
  fillRect(w.x, w.y, w.w, w.h, themePanel_);
  int16_t textX = w.x + (large ? 28 : 18);
  const int16_t textRight = w.x + w.w - 12;
  const bool showArt = w.type == "spotify" && art_ != nullptr;
  if (showArt) {
    const int16_t pad = 14;
    int16_t side = w.h - pad * 2;
    const int16_t maxSide = w.w / 2;
    if (side > maxSide) {
      side = maxSide;
    }
    if (side < 24) {
      side = 24;
    }
    const int16_t artX = w.x + pad;
    const int16_t artY = w.y + (w.h - side) / 2;
    fillRect(artX - 2, artY - 2, side + 4, side + 4, themeAccent_);
    blitArt(artX, artY, side, side);
    textX = artX + side + 16;
  } else {
    fillRect(w.x, w.y, large ? 10 : 6, w.h, themeAccent_);
  }
  const uint8_t labelScale = large ? 3 : 2;
  const uint8_t valueScale = large ? 5 : 3;
  const uint8_t secScale = large ? 3 : 2;
  drawText(textX, w.y + (large ? 28 : 18), w.label, themeMuted_, labelScale, textRight);
  drawText(textX, w.y + (large ? 90 : 52), w.value, themeValue_, valueScale, textRight);
  if (w.secondary.length()) {
    drawText(textX, w.y + w.h - (large ? 60 : 36), w.secondary, themeText_, secScale, textRight);
  }
}

void Dashboard::renderTestPattern() {
  if (!canvas_ || canvasW_ == 0 || canvasH_ == 0) {
    return;
  }
  const uint16_t bars[] = {COLOR_WHITE, COLOR_YELLOW, COLOR_CYAN, COLOR_GREEN,
                           COLOR_MAGENTA, COLOR_RED, COLOR_BLUE, COLOR_BLACK};
  const int n = 8;
  const int16_t barW = static_cast<int16_t>(canvasW_ / n);
  for (uint16_t row = 0; row < canvasH_; ++row) {
    uint16_t *line = canvas_ + static_cast<size_t>(row) * canvasW_;
    for (int i = 0; i < n; ++i) {
      const int16_t x0 = static_cast<int16_t>(i * barW);
      const int16_t x1 =
          (i == n - 1) ? static_cast<int16_t>(canvasW_) : static_cast<int16_t>((i + 1) * barW);
      for (int16_t x = x0; x < x1; ++x) {
        line[x] = bars[i];
      }
    }
    line[0] = COLOR_WHITE;
    line[canvasW_ - 1] = COLOR_WHITE;
  }
  // Top/bottom border
  for (uint16_t x = 0; x < canvasW_; ++x) {
    canvas_[x] = COLOR_WHITE;
    canvas_[static_cast<size_t>(canvasH_ - 1) * canvasW_ + x] = COLOR_WHITE;
  }
  // Corner L marks
  const int16_t arm = 40;
  fillRect(2, 2, arm, 4, COLOR_BLACK);
  fillRect(2, 2, 4, arm, COLOR_BLACK);
  fillRect(canvasW_ - 2 - arm, 2, arm, 4, COLOR_BLACK);
  fillRect(canvasW_ - 6, 2, 4, arm, COLOR_BLACK);
  fillRect(2, canvasH_ - 6, arm, 4, COLOR_BLACK);
  fillRect(2, canvasH_ - 2 - arm, 4, arm, COLOR_BLACK);
  fillRect(canvasW_ - 2 - arm, canvasH_ - 6, arm, 4, COLOR_BLACK);
  fillRect(canvasW_ - 6, canvasH_ - 2 - arm, 4, arm, COLOR_BLACK);

  String res = String(canvasW_) + "x" + String(canvasH_);
  const int16_t textW = static_cast<int16_t>(res.length() * 6 * 4);
  drawText((canvasW_ - textW) / 2, canvasH_ / 2 - 14, res, COLOR_BLACK, 4, canvasW_);
  drawText((canvasW_ - textW) / 2 + 2, canvasH_ / 2 - 12, res, COLOR_WHITE, 4, canvasW_);
}

bool Dashboard::blitRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!display_ || !canvas_) {
    return false;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > canvasW_) {
    w = canvasW_ - x;
  }
  if (y + h > canvasH_) {
    h = canvasH_ - y;
  }
  if (w <= 0 || h <= 0) {
    return false;
  }

  const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h);
  uint16_t *region = nullptr;
  bool heapTemp = false;
  if (blitScratch_ && need <= blitScratchPixels_) {
    region = blitScratch_;
  } else {
    region = static_cast<uint16_t *>(
        heap_caps_malloc(need * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!region) {
      region = static_cast<uint16_t *>(malloc(need * sizeof(uint16_t)));
    }
    heapTemp = region != nullptr;
  }
  if (!region) {
    Serial.println("[dash] blitRegion OOM");
    return false;
  }
  for (int16_t row = 0; row < h; ++row) {
    memcpy(region + static_cast<size_t>(row) * w, canvas_ + (y + row) * canvasW_ + x,
           static_cast<size_t>(w) * sizeof(uint16_t));
  }
  const bool ok = display_->displayRegionRgb565(region, x, y, w, h);
  if (heapTemp) {
    free(region);
  }
  return ok;
}

void Dashboard::paintFullCanvas() {
  if (cfg_.testPattern) {
    renderTestPattern();
  } else {
    fillRect(0, 0, canvasW_, canvasH_, themeBg_);
    renderWidget(clockWidget_, false);
    if (cfg_.layoutMode == LayoutMode::Carousel) {
      int idx = 0;
      for (auto &w : widgets_) {
        if (!w.active) {
          continue;
        }
        if (idx == carouselIndex_) {
          renderWidget(w, true);
          break;
        }
        ++idx;
      }
    } else {
      for (auto &w : widgets_) {
        if (!w.active) {
          continue;
        }
        renderWidget(w, false);
      }
    }
  }
  clockWidget_.dirty = false;
  for (auto &w : widgets_) {
    w.dirty = false;
  }
}

void Dashboard::tick() {
  if (!display_ || !canvas_) {
    return;
  }

  if (fullDirty_) {
    paintFullCanvas();
    fullBandY_ = 0;
    fullDirty_ = false;
  }

  // One strip per tick. A full 1920×480 frame is about 3.7 MB and used to
  // block the panel for minutes before any new pixels showed up.
  if (fullBandY_ >= 0 && fullBandY_ < static_cast<int16_t>(canvasH_)) {
    constexpr int16_t kBand = 80;
    int16_t bandH = kBand;
    if (fullBandY_ + bandH > static_cast<int16_t>(canvasH_)) {
      bandH = static_cast<int16_t>(canvasH_) - fullBandY_;
    }
    if (blitRegion(0, fullBandY_, canvasW_, bandH)) {
      fullBandY_ += bandH;
    }
    return;
  }
  fullBandY_ = -1;

  if (cfg_.testPattern) {
    return;
  }

  if (cfg_.layoutMode == LayoutMode::Carousel && activeWidgetCount() > 0) {
    if (millis() - lastCarouselMs_ >= cfg_.carouselIntervalMs) {
      lastCarouselMs_ = millis();
      carouselIndex_ = (carouselIndex_ + 1) % activeWidgetCount();
      // Partial blit of the content card — a full-frame USB push takes seconds
      // on Full-Speed OTG and makes WebUI / MQTT changes feel lagged.
      int idx = 0;
      for (auto &w : widgets_) {
        if (!w.active) {
          continue;
        }
        if (idx == carouselIndex_) {
          w.dirty = true;
          break;
        }
        ++idx;
      }
    }
  }

  // One USB region per tick. The newest change goes out first so a text edit
  // is not stuck behind every other card.
  DashboardWidget *newest = nullptr;
  if (cfg_.layoutMode == LayoutMode::Carousel) {
    int idx = 0;
    for (auto &w : widgets_) {
      if (!w.active) {
        continue;
      }
      if (idx == carouselIndex_ && w.dirty) {
        newest = &w;
        break;
      }
      ++idx;
    }
  } else {
    for (auto &w : widgets_) {
      if (!w.active || !w.dirty) {
        continue;
      }
      if (newest == nullptr || w.dirtySeq >= newest->dirtySeq) {
        newest = &w;
      }
    }
  }
  if (newest != nullptr) {
    renderWidget(*newest, cfg_.layoutMode == LayoutMode::Carousel);
    blitRegion(newest->x, newest->y, newest->w, newest->h);
    newest->dirty = false;
    return;
  }

  if (clockWidget_.dirty) {
    renderWidget(clockWidget_, false);
    blitRegion(clockWidget_.x, clockWidget_.y, clockWidget_.w, clockWidget_.h);
    clockWidget_.dirty = false;
  }
}
