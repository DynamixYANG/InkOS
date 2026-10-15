#include "Panel.h"
#include <algorithm>
#include <esp_heap_caps.h>
namespace ink {
Panel panel;
constexpr int CS = 44, DC = 10, RST = 38, BUSY = 4;
void Panel::command(uint8_t v) {
  spi.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(DC, LOW);
  digitalWrite(CS, LOW);
  spi.transfer(v);
  digitalWrite(CS, HIGH);
  spi.endTransaction();
}
void Panel::data(const uint8_t *p, size_t n) {
  spi.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(DC, HIGH);
  digitalWrite(CS, LOW);
  spi.writeBytes(p, n);
  digitalWrite(CS, HIGH);
  spi.endTransaction();
}
void Panel::reg(uint8_t c, uint8_t v) {
  command(c);
  data(&v, 1);
}
bool Panel::ready(uint32_t timeout) {
  uint32_t start = millis();
  while (digitalRead(BUSY) == LOW) {
    if (millis() - start >= timeout) {
      ok = false;
      Serial.println("INKOS panel timeout");
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return true;
}
bool Panel::refresh(uint32_t limit) {
  if (!powerOn())
    return false;
  command(0x12);
  delay(2);
  return ready(limit);
}
bool Panel::powerOn() {
  if (powered)
    return true;
  command(0x04);
  delay(2);
  if (!ready(15000))
    return false;
  powered = true;
  return true;
}
bool Panel::powerOff() {
  if (!powered)
    return true;
  command(0x02);
  delay(2);
  if (!ready(15000))
    return false;
  powered = false;
  return true;
}
void Panel::writeFull(const uint8_t *p) {
  for (int y = 0; y < 480; y++)
    data(p + y * 100, 100);
}
void Panel::solid(uint8_t v) {
  uint8_t row[100];
  memset(row, v, 100);
  for (int y = 0; y < 480; y++)
    data(row, 100);
}
void Panel::fullMode() {
  command(0x92);
  reg(0xE0, 0);
  reg(0x41, 0);
  reg(0, 0x1f);
  uint8_t v[] = {0x29, 7};
  command(0x50);
  data(v, 2);
  // UC8179 R52: float VCOM and border at the end of the waveform.
  reg(0x52, 0x0b);
}
void Panel::partialMode() {
    // Manufacturer OTP partial waveform only. The experimental shortened LUT
    // faded unchanged pixels and polluted the background during sustained use.
    reg(0, 0x1f);
    reg(0xE0, 2);
    reg(0xE5, 0x6e);
    uint8_t v[] = {0xa9, 7}; // Border Hi-Z in partial mode.
    command(0x50);
    data(v, 2);
  reg(0x52, 0x0b);
}
bool Panel::begin() {
  oldFrame = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  window = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  if (!oldFrame || !window)
    return false;
  memset(oldFrame, 255, FRAME_BYTES);
  pinMode(43, OUTPUT);
  digitalWrite(43, HIGH);
  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);
  pinMode(DC, OUTPUT);
  pinMode(RST, OUTPUT);
  pinMode(BUSY, INPUT);
  digitalWrite(RST, LOW);
  delay(20);
  digitalWrite(RST, HIGH);
  delay(200);
  if (!spi.begin(7, -1, 9, -1))
    return false;
  uint8_t power[] = {7, 7, 0x3f, 0x3f}, boost[] = {0x17, 0x17, 0x28, 0x17},
          size[] = {3, 0x20, 1, 0xe0};
  command(1);
  data(power, 4);
  command(6);
  data(boost, 4);
  command(4);
  delay(100);
  ok = true;
  if (!ready(15000))
    return false;
  powered = true;
  reg(0, 0x1f);
  command(0x61);
  data(size, 4);
  reg(0x15, 0);
  uint8_t v[] = {0x29, 7};
  command(0x50);
  data(v, 2);
  reg(0x60, 0x22);
  reg(0x52, 0x0b);
  return powerOff();
}
bool Panel::present(const uint8_t *next, bool force, RefreshMode mode) {
  if (!ok)
    return false;
  int minx = 100, maxx = -1, miny = 480, maxy = -1;
  for (int y = 0; y < 480; y++)
    for (int x = 0; x < 100; x++)
      if (next[y * 100 + x] != oldFrame[y * 100 + x]) {
        minx = std::min(minx, x);
        maxx = std::max(maxx, x);
        miny = std::min(miny, y);
        maxy = std::max(maxy, y);
      }
  if (maxx < 0 && !force)
    return true;
  bool animation = mode == RefreshMode::Animation;
  bool full = force || !lastFull || textPartials >= (mode == RefreshMode::Music ? 32u : 60u) ||
              animationPartials >= 1200 || (millis() - lastFull > 1800000u);
  uint32_t start = millis();
  if (full) {
    fullMode();
    if (!frames) {
      // Recover from a physically faded/unknown image before drawing the first page.
      command(0x10);
      solid(255);
      command(0x13);
      solid(255);
      if (!refresh(20000))
        return false;
    }
    command(0x10);
    solid(255);
    command(0x13);
    writeFull(next);
    if (!refresh(20000))
      return false;
    command(0x10);
    writeFull(next);
    partials = 0;
    textPartials = animationPartials = 0;
    fullFrames++;
    lastRegion = {0, 0, 800, 480};
    lastFull = millis();
  } else {
    int x0 = std::max(0, minx - 1) * 8, x1 = std::min(99, maxx + 1) * 8 + 7,
        y0 = std::max(0, miny - 2), y1 = std::min(479, maxy + 2);
    int stride = (x1 - x0 + 1) / 8, h = y1 - y0 + 1;
    partialMode();
    lastRegion = {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
    uint8_t region[] = {uint8_t(x0 >> 8), uint8_t(x0),      uint8_t(x1 >> 8),
                        uint8_t(x1),      uint8_t(y0 >> 8), uint8_t(y0),
                        uint8_t(y1 >> 8), uint8_t(y1),      0}; // PT_SCAN: gates inside only.
    command(0x91);
    command(0x90);
    data(region, 9);
    for (int y = 0; y < h; y++)
      memcpy(window + y * stride, oldFrame + (y0 + y) * 100 + x0 / 8, stride);
    command(0x10);
    data(window, stride * h);
    for (int y = 0; y < h; y++)
      memcpy(window + y * stride, next + (y0 + y) * 100 + x0 / 8, stride);
    command(0x13);
    data(window, stride * h);
    if (!refresh(5000))
      return false;
    command(0x92);
    partials++;
    if (animation)
      animationPartials++;
    else
      textPartials++;
  }
  // Power off retains RAM/registers while releasing source/gate/VCOM drive.
  if (!powerOff())
    return false;
  memcpy(oldFrame, next, FRAME_BYTES);
  lastDuration = millis() - start;
  frames++;
  Serial.printf("INKOS display mode=%s kind=%s ms=%lu partials=%lu x=%d y=%d w=%d h=%d\n",
                full ? "full" : "partial",
                animation                    ? "animation"
                : mode == RefreshMode::Music ? "music"
                                             : "clock",
                (unsigned long)lastDuration, (unsigned long)partials, lastRegion.x, lastRegion.y,
                lastRegion.w, lastRegion.h);
  return true;
}
} // namespace ink
