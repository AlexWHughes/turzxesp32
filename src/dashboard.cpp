#include "dashboard.h"

#include <cstring>
#include <vector>

#include <esp_heap_caps.h>

namespace {
constexpr uint16_t COLOR_BG = 0x10A2;      // dark blue
constexpr uint16_t COLOR_PANEL = 0x2124;   // panel
constexpr uint16_t COLOR_ACCENT = 0x07FF;  // cyan
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_MUTED = 0xAD55;
constexpr uint16_t COLOR_VALUE = 0x07E0;

// 5x7 font, ASCII 32..127
const uint8_t FONT5X7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x00, 0x08, 0x14, 0x22, 0x41},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x41, 0x22, 0x14, 0x08, 0x00},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x01, 0x01},  // F
    {0x3E, 0x41, 0x41, 0x51, 0x32},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x03, 0x04, 0x78, 0x04, 0x03},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
};
}  // namespace

bool Dashboard::begin(TurzxRevC &display, const AppConfig &cfg) {
  display_ = &display;
  canvasW_ = display.width();
  canvasH_ = display.height();
  const size_t bytes = static_cast<size_t>(canvasW_) * canvasH_ * sizeof(uint16_t);
  canvas_ = static_cast<uint16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!canvas_) {
    canvas_ = static_cast<uint16_t *>(malloc(bytes));
  }
  if (!canvas_) {
    Serial.println("[dash] Failed to allocate canvas");
    return false;
  }
  for (size_t i = 0; i < static_cast<size_t>(canvasW_) * canvasH_; ++i) {
    canvas_[i] = COLOR_BG;
  }
  applyConfig(cfg);
  fullDirty_ = true;
  return true;
}

void Dashboard::applyConfig(const AppConfig &cfg) {
  widgets_.clear();
  layoutDefaults();
  for (int i = 0; i < 6; ++i) {
    if (cfg.widgetLabels[i].isEmpty() && cfg.widgetTopics[i].isEmpty()) {
      continue;
    }
    DashboardWidget w;
    w.id = "w" + String(i);
    w.label = cfg.widgetLabels[i].isEmpty() ? ("Sensor " + String(i + 1)) : cfg.widgetLabels[i];
    w.topic = cfg.widgetTopics[i];
    w.value = "--";
    w.x = 40 + (i % 3) * 620;
    w.y = 160 + (i / 3) * 140;
    w.w = 560;
    w.h = 110;
    w.dirty = true;
    widgets_.push_back(w);
  }
  clockWidget_.id = "clock";
  clockWidget_.label = "Clock";
  clockWidget_.value = "--:--:--";
  clockWidget_.x = 40;
  clockWidget_.y = 30;
  clockWidget_.w = canvasW_ - 80;
  clockWidget_.h = 100;
  clockWidget_.dirty = true;
  fullDirty_ = true;
}

void Dashboard::layoutDefaults() {}

void Dashboard::setWidgetValue(const String &topic, const String &value) {
  for (auto &w : widgets_) {
    if (w.topic == topic) {
      if (w.value != value) {
        w.value = value;
        w.dirty = true;
      }
    }
  }
}

void Dashboard::setClock(const String &clockText) {
  if (clockWidget_.value != clockText) {
    clockWidget_.value = clockText;
    clockWidget_.dirty = true;
  }
}

void Dashboard::forceRedraw() { fullDirty_ = true; }

void Dashboard::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!canvas_) {
    return;
  }
  for (int16_t row = y; row < y + h; ++row) {
    if (row < 0 || row >= canvasH_) {
      continue;
    }
    for (int16_t col = x; col < x + w; ++col) {
      if (col < 0 || col >= canvasW_) {
        continue;
      }
      canvas_[row * canvasW_ + col] = color;
    }
  }
}

void Dashboard::drawText(int16_t x, int16_t y, const String &text, uint16_t color, uint8_t scale) {
  int16_t cursor = x;
  for (size_t i = 0; i < text.length(); ++i) {
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

void Dashboard::renderWidget(const DashboardWidget &w) {
  fillRect(w.x, w.y, w.w, w.h, COLOR_PANEL);
  fillRect(w.x, w.y, 6, w.h, COLOR_ACCENT);
  drawText(w.x + 20, w.y + 18, w.label, COLOR_MUTED, 2);
  drawText(w.x + 20, w.y + 52, w.value, COLOR_VALUE, 3);
}

void Dashboard::blitRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!display_ || !canvas_) {
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
  if (x + w > canvasW_) {
    w = canvasW_ - x;
  }
  if (y + h > canvasH_) {
    h = canvasH_ - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }

  std::vector<uint16_t> region(static_cast<size_t>(w) * h);
  for (int16_t row = 0; row < h; ++row) {
    memcpy(region.data() + row * w, canvas_ + (y + row) * canvasW_ + x,
           static_cast<size_t>(w) * sizeof(uint16_t));
  }
  display_->displayRegionRgb565(region.data(), x, y, w, h);
}

void Dashboard::tick() {
  if (!display_ || !canvas_) {
    return;
  }

  if (fullDirty_) {
    fillRect(0, 0, canvasW_, canvasH_, COLOR_BG);
    drawText(40, canvasH_ - 40, "TURZX ESP32-S3  " FIRMWARE_VERSION, COLOR_MUTED, 2);
    renderWidget(clockWidget_);
    for (auto &w : widgets_) {
      renderWidget(w);
      w.dirty = false;
    }
    clockWidget_.dirty = false;
    display_->displayFullRgb565(canvas_, canvasW_, canvasH_);
    fullDirty_ = false;
    return;
  }

  if (clockWidget_.dirty) {
    renderWidget(clockWidget_);
    blitRegion(clockWidget_.x, clockWidget_.y, clockWidget_.w, clockWidget_.h);
    clockWidget_.dirty = false;
  }
  for (auto &w : widgets_) {
    if (!w.dirty) {
      continue;
    }
    renderWidget(w);
    blitRegion(w.x, w.y, w.w, w.h);
    w.dirty = false;
  }
}
