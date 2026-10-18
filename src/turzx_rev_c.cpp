#include "turzx_rev_c.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {
constexpr uint8_t kHello[] = {0x01, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xc5, 0xd3};
constexpr uint8_t kOptions[] = {0x7d, 0xef, 0x69, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x2d};
constexpr uint8_t kSetBrightness[] = {0x7b, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};
constexpr uint8_t kStopVideo[] = {0x79, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kStopMedia[] = {0x96, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kTurnOff[] = {0x83, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01};
// 0 disables the panel's idle sleep. A non-zero value blanks the panel even
// while frames are being sent, which looks like a dead screen.
constexpr uint8_t kSleepWhenIdleMinutes = 0x00;
constexpr uint8_t kQueryStatus[] = {0xcf, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kPreUpdate[] = {0x86, 0xef, 0x69, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kStartDisplayBitmap[] = {0x2c};
constexpr uint8_t kDisplayBitmap8[] = {0xc8, 0xef, 0x69, 0x00, 0x38, 0x40};
constexpr uint8_t kUpdateBitmap[] = {0xcc, 0xef, 0x69, 0x00};

uint16_t rgb888To565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct NativeRect {
  int rotW;
  int rotH;
  int x0;
  int y0;
};

// Same placement as LcdCommRevC._generate_update_image for an 8.8" panel.
// rotW/rotH are the patch size after the orientation rotate.
NativeRect nativeRect(TurzxOrientation orientation, int logicalW, int logicalH, int x, int y, int srcW,
                      int srcH) {
  switch (orientation) {
    case TurzxOrientation::Landscape:
      // rotate 270°, then y0 = get_height() - y - rotated width
      return NativeRect{srcH, srcW, x, logicalH - y - srcH};
    case TurzxOrientation::ReverseLandscape:
      // rotate 90°, then x0 = get_width() - x - rotated height
      return NativeRect{srcH, srcW, logicalW - x - srcW, y};
    case TurzxOrientation::Portrait:
      // rotate 180°. Both offsets use get_height() on this panel.
      return NativeRect{srcW, srcH, logicalH - y - srcH, logicalH - x - srcW};
    case TurzxOrientation::ReversePortrait:
      return NativeRect{srcW, srcH, y, x};
    default: {
      const TurzxOrientation unreachable = orientation;
      (void)unreachable;
      return NativeRect{srcW, srcH, x, y};
    }
  }
}

// Inverse of PIL's counter-clockwise rotate(..., expand=True) for the 8.8" cases.
uint16_t sampleRotated(TurzxOrientation orientation, const uint16_t *src, int srcW, int srcH, int rx,
                       int ry) {
  int sx = rx;
  int sy = ry;
  switch (orientation) {
    case TurzxOrientation::Landscape:
      sx = ry;
      sy = srcH - 1 - rx;
      break;
    case TurzxOrientation::ReverseLandscape:
      sx = srcW - 1 - ry;
      sy = rx;
      break;
    case TurzxOrientation::Portrait:
      sx = srcW - 1 - rx;
      sy = srcH - 1 - ry;
      break;
    case TurzxOrientation::ReversePortrait:
      break;
    default: {
      const TurzxOrientation unreachable = orientation;
      (void)unreachable;
      break;
    }
  }
  if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH) {
    return 0;
  }
  return src[static_cast<size_t>(sy) * static_cast<size_t>(srcW) + sx];
}

void rgb565ToChannels(uint16_t px, uint8_t *dst, bool withAlpha) {
  uint8_t r = (px >> 11) & 0x1F;
  uint8_t g = (px >> 5) & 0x3F;
  uint8_t b = px & 0x1F;
  r = static_cast<uint8_t>((r << 3) | (r >> 2));
  g = static_cast<uint8_t>((g << 2) | (g >> 4));
  b = static_cast<uint8_t>((b << 3) | (b >> 2));
  dst[0] = b;
  dst[1] = g;
  dst[2] = r;
  if (withAlpha) {
    dst[3] = 0xFF;
  }
}

// Serializes a byte stream the way Python's b'\x00'.join(chunked(data, 249)) does:
// a 0x00 is inserted only between 249-byte chunks, never after the last one.
class ByteFramer {
 public:
  ByteFramer(TurzxTransport &transport, bool separate) : transport_(transport), separate_(separate) {}

  bool write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      if (!writeByte(data[i])) {
        return false;
      }
    }
    return true;
  }

  bool finish() {
    if (fill_ == 0) {
      return true;
    }
    const size_t n = fill_;
    fill_ = 0;
    return transport_.write(buf_, n) == n;
  }

 private:
  bool writeByte(uint8_t value) {
    if (separate_ && fill_ == 249) {
      buf_[249] = 0x00;
      if (transport_.write(buf_, 250) != 250) {
        return false;
      }
      fill_ = 0;
      if ((++flushes_ & 0x0F) == 0) {
        yield();
      }
    }
    buf_[fill_++] = value;
    if (!separate_ && fill_ == sizeof(buf_)) {
      const size_t n = fill_;
      fill_ = 0;
      if (transport_.write(buf_, n) != n) {
        return false;
      }
      if ((++flushes_ & 0x0F) == 0) {
        yield();
      }
    }
    return true;
  }

  TurzxTransport &transport_;
  bool separate_;
  uint8_t buf_[250] = {};
  size_t fill_ = 0;
  uint16_t flushes_ = 0;
};
}  // namespace

TurzxRevC::TurzxRevC(TurzxTransport &transport) : transport_(transport) {}

bool TurzxRevC::begin() {
  ready_ = false;
  if (!transport_.begin()) {
    return false;
  }
  if (!transport_.isReady() && !transport_.wakeDevice()) {
    return false;
  }
  if (!transport_.isReady()) {
    return false;
  }
  if (!hello()) {
    // Protocol handshake failed. Drop the CDC handle so a later hotplug/retry
    // can reopen cleanly — do not TURNOFF here (that re-enumerates and races
    // with an already-present panel after a cable reconnect).
    transport_.end();
    return false;
  }
  sendCommand(kStopVideo, sizeof(kStopVideo));
  sendCommand(kStopMedia, sizeof(kStopMedia), nullptr, 0, 0x00, 1024);
  setOrientation(TurzxOrientation::Landscape);
  ready_ = true;
  Serial.println("[turzx] Panel link up; idle sleep disabled");
  return true;
}

void TurzxRevC::end() {
  ready_ = false;
  transport_.end();
}

uint16_t TurzxRevC::width() const {
  return orientation_ == TurzxOrientation::Portrait ||
                 orientation_ == TurzxOrientation::ReversePortrait
             ? kPortraitW
             : kPortraitH;
}

uint16_t TurzxRevC::height() const {
  return orientation_ == TurzxOrientation::Portrait ||
                 orientation_ == TurzxOrientation::ReversePortrait
             ? kPortraitH
             : kPortraitW;
}

bool TurzxRevC::setBrightness(uint8_t percent) {
  percent = std::min<uint8_t>(percent, 100);
  uint8_t level = static_cast<uint8_t>((percent / 100.0f) * 255.0f);
  return sendCommand(kSetBrightness, sizeof(kSetBrightness), &level, 1);
}

bool TurzxRevC::screenOff() {
  sendCommand(kStopVideo, sizeof(kStopVideo));
  sendCommand(kStopMedia, sizeof(kStopMedia), nullptr, 0, 0x00, 1024);
  const bool off = sendCommand(kTurnOff, sizeof(kTurnOff));
  ready_ = false;
  // TURNOFF re-enumerates the panel. Drop the CDC handle so the next begin()
  // can wake a fresh port. The disconnect callback may have closed it already.
  transport_.end();
  return off;
}

bool TurzxRevC::setOrientation(TurzxOrientation orientation) {
  orientation_ = orientation;
  // start mode 0 (default, not PlayImage/PlayVideo), pad, no flip, sleep.
  uint8_t payload[4] = {0x00, 0x00, 0x00, kSleepWhenIdleMinutes};
  return sendCommand(kOptions, sizeof(kOptions), payload, sizeof(payload));
}

bool TurzxRevC::clear(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t w = width();
  const uint16_t h = height();
  std::vector<uint16_t> buf(static_cast<size_t>(w) * h, rgb888To565(r, g, b));
  return displayFullRgb565(buf.data(), w, h);
}

bool TurzxRevC::displayFullRgb565(const uint16_t *pixels, uint16_t width, uint16_t height) {
  if (!pixels || width == 0 || height == 0) {
    return false;
  }
  return generateAndSendFull(pixels, width, height);
}

bool TurzxRevC::displayRegionRgb565(const uint16_t *pixels, uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t height) {
  if (!pixels || width == 0 || height == 0) {
    return false;
  }
  if (x == 0 && y == 0 && width == this->width() && height == this->height()) {
    return displayFullRgb565(pixels, width, height);
  }
  return generateAndSendPartial(pixels, x, y, width, height);
}

bool TurzxRevC::hello() {
  for (int attempt = 0; attempt < 10; ++attempt) {
    transport_.flushInput();
    if (!sendCommand(kHello, sizeof(kHello))) {
      delay(200);
      continue;
    }
    uint8_t resp[23] = {};
    if (!readExact(resp, sizeof(resp), 1500)) {
      delay(200);
      continue;
    }
    String id;
    for (uint8_t ch : resp) {
      if (ch >= 32 && ch < 127) {
        id += static_cast<char>(ch);
      }
    }
    if (id.startsWith("chs_")) {
      displayId_ = id;
      int lastDot = id.lastIndexOf('.');
      if (lastDot > 0) {
        romVersion_ = id.substring(lastDot + 1).toInt();
        if (romVersion_ < 80 || romVersion_ > 100) {
          romVersion_ = 90;
        }
      }
      Serial.printf("[turzx] Display ID: %s (rom %d)\n", displayId_.c_str(), romVersion_);
      return true;
    }
    delay(250);
  }
  return false;
}

void TurzxRevC::padTo250(uint8_t *buf, size_t &len, size_t capacity, uint8_t padByte) {
  if (len == 0) {
    return;
  }
  size_t target = ((len + 249) / 250) * 250;
  while (len < target && len < capacity) {
    buf[len++] = padByte;
  }
}

bool TurzxRevC::sendRaw(const uint8_t *data, size_t len, size_t readSize) {
  if (!data || len == 0) {
    return false;
  }
  size_t written = transport_.write(data, len);
  if (written != len) {
    return false;
  }
  if (readSize > 0) {
    std::vector<uint8_t> discard(readSize);
    readExact(discard.data(), readSize, 2000);
  }
  return true;
}

bool TurzxRevC::sendCommand(const uint8_t *cmd, size_t cmdLen, const uint8_t *payload,
                            size_t payloadLen, uint8_t padByte, size_t readSize) {
  std::vector<uint8_t> message;
  message.reserve(cmdLen + payloadLen + 250);
  message.insert(message.end(), cmd, cmd + cmdLen);
  if (payload && payloadLen) {
    message.insert(message.end(), payload, payload + payloadLen);
  }
  size_t len = message.size();
  size_t target = ((len + 249) / 250) * 250;
  message.resize(target, padByte);
  return sendRaw(message.data(), message.size(), readSize);
}

bool TurzxRevC::readExact(uint8_t *buf, size_t len, uint32_t timeoutMs) {
  size_t got = transport_.read(buf, len, timeoutMs);
  return got == len;
}

bool TurzxRevC::generateAndSendFull(const uint16_t *pixels, uint16_t w, uint16_t h) {
  // 8.8" full frames are the portrait scan buffer: rotate first, then BGRA.
  // See LcdCommRevC._generate_full_image.
  sendCommand(kPreUpdate, sizeof(kPreUpdate));
  sendCommand(kStartDisplayBitmap, sizeof(kStartDisplayBitmap), nullptr, 0, 0x2c);

  const uint16_t sizeWord = static_cast<uint16_t>((kPortraitW * kPortraitW) / 64);
  uint8_t sizePayload[2] = {static_cast<uint8_t>((sizeWord >> 8) & 0xFF),
                            static_cast<uint8_t>(sizeWord & 0xFF)};
  sendCommand(kDisplayBitmap8, sizeof(kDisplayBitmap8), sizePayload, sizeof(sizePayload));

  const NativeRect rect = nativeRect(orientation_, width(), height(), 0, 0, w, h);
  Serial.printf("[turzx] full frame %ux%u -> %dx%d BGRA\n", w, h, rect.rotW, rect.rotH);

  ByteFramer out(transport_, true);
  uint8_t px[4];
  for (int ry = 0; ry < rect.rotH; ++ry) {
    for (int rx = 0; rx < rect.rotW; ++rx) {
      rgb565ToChannels(sampleRotated(orientation_, pixels, w, h, rx, ry), px, true);
      if (!out.write(px, 4)) {
        return false;
      }
    }
  }
  if (!out.finish()) {
    return false;
  }
  std::vector<uint8_t> discard(1024);
  readExact(discard.data(), discard.size(), 2000);
  return sendCommand(kQueryStatus, sizeof(kQueryStatus), nullptr, 0, 0x00, 1024);
}

bool TurzxRevC::generateAndSendPartial(const uint16_t *pixels, uint16_t x, uint16_t y, uint16_t w,
                                       uint16_t h) {
  // 8.8" partials use the same rotate, then one native scan-line per rotated row.
  // ROM > 88 sends BGRA; older ROM sends BGR. See _generate_update_image.
  const NativeRect rect = nativeRect(orientation_, width(), height(), x, y, w, h);
  if (rect.rotW <= 0 || rect.rotH <= 0 || rect.x0 < 0 || rect.y0 < 0) {
    Serial.printf("[turzx] partial skipped %u,%u %ux%u -> %d,%d %dx%d\n", x, y, w, h, rect.x0,
                  rect.y0, rect.rotW, rect.rotH);
    return false;
  }

  const bool bgra = romVersion_ > 88;
  const int pixelBytes = bgra ? 4 : 3;
  const uint32_t rawLen = static_cast<uint32_t>(rect.rotH) *
                          (5u + static_cast<uint32_t>(rect.rotW) * static_cast<uint32_t>(pixelBytes));
  const uint32_t imageSize = rawLen + 2;

  std::vector<uint8_t> payload;
  payload.insert(payload.end(), kUpdateBitmap, kUpdateBitmap + sizeof(kUpdateBitmap));
  payload.push_back((imageSize >> 16) & 0xFF);
  payload.push_back((imageSize >> 8) & 0xFF);
  payload.push_back(imageSize & 0xFF);
  payload.push_back(0x00);
  payload.push_back(0x00);
  payload.push_back(0x00);
  payload.push_back((updateCount_ >> 24) & 0xFF);
  payload.push_back((updateCount_ >> 16) & 0xFF);
  payload.push_back((updateCount_ >> 8) & 0xFF);
  payload.push_back(updateCount_ & 0xFF);
  ++updateCount_;

  const size_t padded = ((payload.size() + 249) / 250) * 250;
  payload.resize(padded, 0x00);
  if (!sendRaw(payload.data(), payload.size())) {
    return false;
  }

  ByteFramer out(transport_, rawLen > 250);
  uint8_t px[4];
  for (int row = 0; row < rect.rotH; ++row) {
    const uint32_t offset = (static_cast<uint32_t>(rect.x0) + static_cast<uint32_t>(row)) * kPortraitW +
                            static_cast<uint32_t>(rect.y0);
    const uint8_t hdr[5] = {
        static_cast<uint8_t>((offset >> 16) & 0xFF), static_cast<uint8_t>((offset >> 8) & 0xFF),
        static_cast<uint8_t>(offset & 0xFF),        static_cast<uint8_t>((rect.rotW >> 8) & 0xFF),
        static_cast<uint8_t>(rect.rotW & 0xFF),
    };
    if (!out.write(hdr, sizeof(hdr))) {
      return false;
    }
    for (int col = 0; col < rect.rotW; ++col) {
      rgb565ToChannels(sampleRotated(orientation_, pixels, w, h, col, row), px, bgra);
      if (!out.write(px, static_cast<size_t>(pixelBytes))) {
        return false;
      }
    }
  }
  if (!out.finish()) {
    return false;
  }
  const uint8_t tail[] = {0xef, 0x69};
  if (transport_.write(tail, sizeof(tail)) != sizeof(tail)) {
    return false;
  }
  return sendCommand(kQueryStatus, sizeof(kQueryStatus), nullptr, 0, 0x00, 1024);
}
