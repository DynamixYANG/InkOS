#include "Canvas.h"
#include "Energy.h"
#include "Layout.h"
#include "Zones.h"
#include <algorithm>
#include <cmath>
namespace ink {
static void hourLabel(char *b, size_t n, time_t t) {
  auto h = Clock::finland(t), p = Clock::finland(t - 3600), q = Clock::finland(t + 3600);
  const char *suffix = h.tm_hour == p.tm_hour && h.tm_mday == p.tm_mday   ? "b"
                       : h.tm_hour == q.tm_hour && h.tm_mday == q.tm_mday ? "a"
                                                                          : "";
  snprintf(b, n, "%02d%s", h.tm_hour, suffix);
}
static void bolt(Canvas &c, int x, int y) {
  c.line(x + 7, y, x + 1, y + 7);
  c.line(x + 1, y + 7, x + 6, y + 7);
  c.line(x + 6, y + 7, x, y + 14);
}
void renderCurrentPrice(Canvas &c, const Energy &e, time_t now) {
  if (!e.enabled)
    return;
  char b[32];
  double value = totalPriceCt(e, now);
  if (isfinite(value))
    snprintf(b, sizeof b, "%.3f€", value / 100);
  else
    strlcpy(b, "--.---€", sizeof b);
  int x = 400 - (c.textWidth(b, 20) + 20) / 2;
  bolt(c, x, layout::HeaderBaseline - 14);
  c.text(x + 20, layout::HeaderBaseline, b, 20);
}
void renderEnergy(Canvas &c, const Model &m, time_t now) {
  renderChrome(c, "ENERGY / FINLAND", Page::Energy, m);
  const auto &e = m.energy;
  char b[128];
  if (!e.enabled) {
    c.text(12, 111, "Electricity display is disabled", 24);
    c.text(12, 151, "Enable it in InkOS Studio / Energy.", 18);
    return;
  }
  if (!Clock::valid()) {
    c.text(12, 110, "Waiting for clock sync", 24);
    return;
  }
  // Equal elapsed hours on either side, including midnight and DST transitions.
  constexpr int radius = 12, count = radius * 2 + 1;
  time_t hour = now / 3600 * 3600, start = hour - radius * 3600;
  double values[count], low = INFINITY, high = -INFINITY;
  int lowest = -1, highest = -1, gaps = 0;
  for (int i = 0; i < count; i++) {
    values[i] = totalPriceCt(e, start + i * 3600);
    if (!isfinite(values[i])) {
      gaps++;
      continue;
    }
    if (values[i] < low) {
      low = values[i];
      lowest = i;
    }
    if (values[i] > high) {
      high = values[i];
      highest = i;
    }
  }
  if (lowest < 0) {
    c.text(12, 110, "Waiting for published prices", 24);
    c.fit(12, 150, 776, *e.error ? e.error : "Finnish price area / waiting for data", 16);
    return;
  }
  // Three equal summary columns; extrema refer to the visible rolling window.
  for (int k = 0; k < 3; k++) {
    int x = 12 + k * 260, index = k == 0 ? lowest : k == 1 ? radius : highest;
    double value = values[index];
    c.text(x, 80, k == 0 ? "WINDOW LOW" : k == 1 ? "CURRENT / ct/kWh" : "WINDOW HIGH", 14);
    if (isfinite(value))
      snprintf(b, sizeof b, "%.2f", value);
    else
      strlcpy(b, "--.--", sizeof b);
    c.text(x, 120, b, 32);
    auto date = Clock::finland(start + index * 3600);
    char label[12];
    hourLabel(label, sizeof label, start + index * 3600);
    snprintf(b, sizeof b, "%02d.%02d / %s:00", date.tm_mday, date.tm_mon + 1, label);
    c.text(x, 148, b, 16);
  }
  constexpr int gx = 12, gy = 188, gw = 776, gh = 176;
  double bottom = std::min(0., low), top = std::max(1., high);
  if (top - bottom < 1)
    top = bottom + 1;
  double scale = gh / (top - bottom);
  int zero = gy + int(top * scale);
  c.dottedLine(gx, zero, gw);
  for (int i = 0; i < count; i++) {
    int cx = gx + i * gw / count, next = gx + (i + 1) * gw / count, width = next - cx - 10,
        mid = (cx + next) / 2;
    double value = values[i];
    if (isfinite(value)) {
      int y = gy + int((top - value) * scale);
      c.rect(mid - width / 2, std::min(y, zero), width, std::max(1, abs(y - zero)));
      // Alternate labels stay legible for signed, multi-digit prices.
      if (i % 2 == 0) {
        snprintf(b, sizeof b, "%.1f", value);
        int labelX =
            std::max(gx, std::min(gx + gw - c.textWidth(b, 14), mid - c.textWidth(b, 14) / 2));
        c.text(labelX, std::max(180, std::min(y, zero) - 8), b, 14);
      }
    } else {
      c.line(mid - 4, zero, mid - 1, zero);
      c.line(mid + 2, zero, mid + 5, zero);
    }
    hourLabel(b, sizeof b, start + i * 3600);
    if (i == radius) {
      c.rect(mid - 14, 374, 29, 22);
      c.text(mid - c.textWidth(b, 14) / 2, 390, b, 14, false);
      c.centered(cx - 8, 414, next - cx + 16, "NOW", 14);
    } else
      c.centered(cx, 390, next - cx, b, 14);
  }
  auto a = Clock::finland(start), z = Clock::finland(start + (count - 1) * 3600);
  snprintf(b, sizeof b, "%02d.%02d / -12h", a.tm_mday, a.tm_mon + 1);
  c.text(12, 414, b, 14);
  snprintf(b, sizeof b, "+12h / %02d.%02d", z.tm_mday, z.tm_mon + 1);
  c.text(788 - c.textWidth(b, 14), 414, b, 14);
  c.line(12, 426, 787, 426);
  snprintf(b, sizeof b, "VAT %.1f%%   MARGIN %.2f ct   TRANSMISSION %.2f ct", e.vatPercent,
           e.marginCt, e.transmissionCt);
  c.text(12, 446, b, 14);
  snprintf(b, sizeof b, "Hourly / ct/kWh / fees %s VAT%s%s",
           e.feesIncludeVat ? "include" : "exclude", e.error[0] ? " / CACHED" : "",
           gaps ? " / unpublished hours left blank" : "");
  c.fit(12, 468, 776, b, 14);
}
} // namespace ink
