#include "Panel.h"
#include "NavigationWaveform.h"
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_timer.h>
namespace ink {
Panel panel;
constexpr int CS = 44, DC = 10, RST = 38, BUSY = 4;
// BUSY_N rising edge (controller ready) wakes the waiting display task early.
// The 2 ms poll in ready() remains as a fallback, so a missed edge only costs
// the previous polling latency.
static SemaphoreHandle_t busySignal = nullptr;
static void IRAM_ATTR busyReady() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(busySignal, &woken);
  if (woken)
    portYIELD_FROM_ISR();
}
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
    if (busySignal)
      xSemaphoreTake(busySignal, pdMS_TO_TICKS(2));
    else
      vTaskDelay(pdMS_TO_TICKS(2));
  }
  return true;
}
// BUSY_N falls within microseconds of 0x04/0x02/0x12. Wait for that edge for
// at most the previous fixed 2 ms delay instead of always sleeping 2 ms, then
// discard any stale ready signal before ready() waits for the rising edge.
void Panel::busyStarted() {
  uint32_t t = micros();
  while (digitalRead(BUSY) == HIGH && micros() - t < 2000)
    delayMicroseconds(10);
  if (busySignal)
    xSemaphoreTake(busySignal, 0);
}
bool Panel::refresh(uint32_t limit) {
  if (!powerOn())
    return false;
  int64_t t = esp_timer_get_time();
  command(0x12);
  busyStarted();
  bool done = ready(limit);
  phases.waveformUs += uint32_t(esp_timer_get_time() - t);
  return done;
}
bool Panel::powerOn() {
  if (powered)
    return true;
  int64_t t = esp_timer_get_time();
  command(0x04);
  busyStarted();
  bool done = ready(15000);
  phases.powerOnUs += uint32_t(esp_timer_get_time() - t);
  if (!done)
    return false;
  powered = true;
  return true;
}
bool Panel::powerOff() {
  if (!powered)
    return true;
  int64_t t = esp_timer_get_time();
  command(0x02);
  busyStarted();
  bool done = ready(15000);
  phases.powerOffUs += uint32_t(esp_timer_get_time() - t);
  if (!done)
    return false;
  powered = false;
  return true;
}
void Panel::writeFull(const uint8_t *p) {
  // One CS-low burst instead of 480 row transactions.
  data(p, FRAME_BYTES);
}
void Panel::solid(uint8_t v) {
  uint8_t row[100];
  memset(row, v, 100);
  spi.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(DC, HIGH);
  digitalWrite(CS, LOW);
  for (int y = 0; y < 480; y++)
    spi.writeBytes(row, 100);
  digitalWrite(CS, HIGH);
  spi.endTransaction();
}
void Panel::fullMode(bool fast) {
  command(0x92);
  if (fast) {
    // Fast OTP full update as used by GxEPD2 for UC8179 7.5" panels:
    // cascade TSFIX + forced temperature 0x5A selects a shorter OTP waveform.
    // Written right before 0x12; valid with drive power already on.
    reg(0xE0, 0x02);
    reg(0xE5, 0x5a);
  } else {
    // Standard OTP full update using the internal temperature sensor.
    reg(0xE0, 0);
    reg(0x41, 0);
  }
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
void Panel::navigationMode() {
  // Experimental title-only ~500ms total presentation. UC8179 register LUT,
  // balanced transitions; keep panel voltages/PLL unchanged. Only the title
  // gates are scanned, with border Hi-Z and VCOM floating at waveform end.
  reg(0xE0, 0);
  reg(0, 0x3f);
  const uint8_t interval[] = {0xa9, 7};
  command(0x50); data(interval, sizeof interval);
  reg(0x52, 0x0b);
  for (int lut=0;lut<6;lut++) {
    uint8_t waveform[60] = {};
    makeNavigationLut(lut, waveform);
    command(0x20+lut);
    // R20/R22/R23/R24: 10 groups; R21/R25: 7 groups. Clear unused groups too.
    data(waveform, navigationLutBytes(lut));
  }
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
  if (!busySignal)
    busySignal = xSemaphoreCreateBinary();
  if (busySignal)
    attachInterrupt(digitalPinToInterrupt(BUSY), busyReady, RISING);
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
bool Panel::present(const uint8_t *next, bool force, RefreshMode mode, bool settledNavigation) {
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
  // A navigation preview must never turn into a maintenance full refresh.
  // The settled page immediately follows with a full refresh and resets the budget.
  bool navigation = mode == RefreshMode::Navigation;
  // Time-based maintenance follows the last *standard* full refresh, so fast
  // navigation fulls never postpone the strong clean beyond 30 minutes.
  bool full = force || !lastFull || (!navigation &&
              (textPartials >= (mode == RefreshMode::Music ? 32u : 60u) ||
               animationPartials >= 1200 || (millis() - lastStandardFull > 1800000u)));
  bool fastFull = full && settledNavigation && frames && fastNavigationFull.load() &&
                  fastFullsSinceStandard < FastFullLimit;
  // A future renderer accidentally changing pixels outside the header must
  // fall back to OTP rather than applying the short waveform to body content.
  bool fastTitle = navigation && !full && navigationWindowAllowed(minx,maxx,miny,maxy);
  uint32_t start = millis();
  int64_t startUs = esp_timer_get_time();
  phases = {};
  // Held drive power may only continue into another title preview or the
  // settled full refresh. Anything else (body partials, maintenance full,
  // animation) starts from the original powered-off state.
  bool reused = held && powered && (navigation || (full && settledNavigation));
  if (held && !reused && !powerOff())
    return false;
  held = false;
  if (full) {
    fullMode(fastFull);
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
    // No post-refresh DTM1 (0x10) rewrite: R50 sets N2OCP (0x29/0xa9 bit 3),
    // and every later path writes DTM1 itself before 0x12 (full: white;
    // partial/title: the old window from oldFrame), so it is never consumed.
    partials = 0;
    textPartials = animationPartials = 0;
    fullFrames++;
    lastRegion = {0, 0, 800, 480};
    lastFull = millis();
    if (fastFull)
      fastFullsSinceStandard++;
    else {
      fastFullsSinceStandard = 0;
      lastStandardFull = lastFull;
    }
  } else {
    int x0 = std::max(0, minx - 1) * 8, x1 = std::min(99, maxx + 1) * 8 + 7,
        y0 = std::max(0, miny - 2), y1 = std::min(479, maxy + 2);
    int stride = (x1 - x0 + 1) / 8, h = y1 - y0 + 1;
    if (fastTitle) {
      x0=std::max(x0,NavigationLeft); x1=std::min(x1,NavigationRight);
      y0=std::max(y0,NavigationTop); y1=std::min(y1,NavigationBottom);
      stride=(x1-x0+1)/8; h=y1-y0+1;
      navigationMode();
    } else partialMode(); // Explicitly restores OTP after any short preview.
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
  // During a navigation sequence keep drive power on: the next title or the
  // settled full refresh follows within the selection delay, saving one
  // power-off/power-on pair. idle() bounds the held time.
  if (navigation && holdNavigationPower.load()) {
    held = true;
    heldSinceMs = millis();
  } else if (!powerOff())
    return false;
  memcpy(oldFrame, next, FRAME_BYTES);
  lastDuration = millis() - start;
  phases.totalUs = uint32_t(esp_timer_get_time() - startUs);
  phases.transferUs = phases.totalUs - phases.powerOnUs - phases.waveformUs - phases.powerOffUs;
  lastPhases = phases;
  frames++;
  Serial.printf("INKOS display mode=%s kind=%s ms=%lu partials=%lu x=%d y=%d w=%d h=%d wave=%s "
                "on_us=%lu xfer_us=%lu wave_us=%lu off_us=%lu total_us=%lu power=%s\n",
                full ? "full" : "partial",
                animation                    ? "animation"
                : navigation                 ? "navigation"
                : mode == RefreshMode::Music ? "music"
                                             : "clock",
                (unsigned long)lastDuration, (unsigned long)partials, lastRegion.x, lastRegion.y,
                lastRegion.w, lastRegion.h, fastTitle ? "nav500" : fastFull ? "otp-fast" : "otp",
                (unsigned long)phases.powerOnUs, (unsigned long)phases.transferUs,
                (unsigned long)phases.waveformUs, (unsigned long)phases.powerOffUs,
                (unsigned long)phases.totalUs,
                held ? "held" : reused ? "reused-off" : "off");
  return true;
}
void Panel::idle() {
  if (!held)
    return;
  if (!powered) {
    held = false;
    return;
  }
  uint32_t age = millis() - heldSinceMs;
  if (holdNavigationPower.load() && age < holdIdleMs.load())
    return;
  int64_t t = esp_timer_get_time();
  bool done = powerOff();
  held = false;
  Serial.printf("INKOS display idle power-off held_ms=%lu off_us=%lu ok=%d\n", (unsigned long)age,
                (unsigned long)(esp_timer_get_time() - t), done);
}
} // namespace ink
