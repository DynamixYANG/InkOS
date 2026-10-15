#include "Astronomy.h"
#include "Canvas.h"
#include "Layout.h"
#include "Zones.h"
#include <algorithm>
#include <cmath>
namespace ink {
static time_t localWall(time_t now, const char *zone) {
  auto t = inZone(now, zone);
  return Clock::epoch(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}
static void clockText(char *b, size_t n, time_t t, const char *zone) {
  auto d = inZone(t, zone);
  if (t)
    snprintf(b, n, "%02d:%02d", d.tm_hour, d.tm_min);
  else
    strlcpy(b, "--:--", n);
}
static void dateText(char *b, size_t n, time_t t, const char *zone) {
  auto d = inZone(t, zone);
  if (t)
    snprintf(b, n, "%02d.%02d", d.tm_mday, d.tm_mon + 1);
  else
    strlcpy(b, "--.--", n);
}
void moonIcon(Canvas &c, int x, int y, int r, double phase, double fraction, bool southern) {
  bool waxing = phase < .5;
  if (southern)
    waxing = !waxing;
  for (int yy = -r; yy <= r; yy++)
    for (int xx = -r; xx <= r; xx++)
      if (xx * xx + yy * yy <= r * r) {
        double span = sqrt(double(r * r - yy * yy)), edge = (1 - 2 * fraction) * span;
        bool lit = waxing ? xx >= edge : xx <= -edge;
        c.dot(x + xx, y + yy, !lit);
      }
  c.circle(x, y, r, 1);
}
void solarEventIcon(Canvas &c, int x, int y, bool rising, int size) {
  int mid = x + size / 2, base = y + size - 3, r = size / 4;
  c.line(x, base, x + size, base);
  for (int a = 0; a <= 180; a += 12) {
    double t = a * 3.141592653589793 / 180;
    c.dot(mid + round(r * cos(t)), base - round(r * sin(t)));
  }
  int top = y + 1, bottom = y + size / 2;
  c.line(mid, top, mid, bottom);
  int tip = rising ? top : bottom, tail = rising ? tip + 3 : tip - 3;
  c.line(mid, tip, mid - 3, tail);
  c.line(mid, tip, mid + 3, tail);
  c.line(x + 2, base - 6, x, base - 8);
  c.line(x + size - 2, base - 6, x + size, base - 8);
}
void renderHomeAstronomy(Canvas &c, const Settings &s, time_t now) {
  c.line(layout::WeatherDivider, 320, layout::WeatherDivider, layout::HomeBottom);
  c.line(layout::MoonDivider, 320, layout::MoonDivider, layout::HomeBottom);
  if (!Clock::valid()) {
    c.centered(232, 360, 64, "--", 18);
    c.text(layout::SolarLeft, 352, "SUN", 14);
    c.text(layout::SolarLeft, 376, "--:--", 16);
    return;
  }
  auto moon = moonState(now);
  moonIcon(c, 264, 350, 21, moon.phase, moon.illuminated, s.latitude < 0);
  int i = moonPhaseIndex(moon.phase);
  const char *first[] = {"NEW", "WAXING", "FIRST", "WAXING", "FULL", "WANING", "LAST", "WANING"};
  const char *second[] = {"MOON", "CRES.", "QUARTER", "GIBB.", "MOON", "GIBB.", "QUARTER", "CRES."};
  c.centered(232, 392, 64, first[i], 14);
  c.centered(232, 412, 64, second[i], 14);
  char b[32];
  snprintf(b, sizeof b, "%.0f%%", moon.illuminated * 100);
  c.centered(232, 432, 64, b, 14);
  auto pair = adjacentSolarEvents(now, s.latitude, s.longitude);
  for (int k = 0; k < 2; k++) {
    auto event = k ? pair.next : pair.previous;
    int y = 332 + k * 60;
    c.text(layout::SolarLeft, y, k ? "NEXT" : "PREV", 14);
    if (event.at)
      solarEventIcon(c, layout::SolarLeft, y + 5, event.rising, 18);
    clockText(b, sizeof b, event.at, s.localZone);
    c.text(layout::SolarLeft + 24, y + 20, b, 16);
    dateText(b, sizeof b, event.at, s.localZone);
    c.text(layout::SolarLeft + 24, y + 40, b, 14);
  }
}
void renderMoon(Canvas &c, const Model &m, time_t now) {
  renderChrome(c, "LUNAR CALENDAR", Page::Moon, m);
  if (!Clock::valid()) {
    c.text(24, 100, "Waiting for clock sync", 24);
    return;
  }
  auto moon = moonState(now);
  char b[96];
  int index = moonPhaseIndex(moon.phase);
  c.text(24, 88, "CURRENT PHASE", 14);
  c.fit(24, 129, 310, moonPhaseName(index), 28);
  c.text(350, 88, "ILLUMINATION", 14);
  snprintf(b, sizeof b, "%.1f%%", moon.illuminated * 100);
  c.text(350, 129, b, 28);
  auto lastNew = moonPhaseEvent(now, 0, false);
  c.text(610, 88, "LUNAR AGE / DAYS", 14);
  snprintf(b, sizeof b, "%.1f", lastNew ? double(now - lastNew) / 86400 : 0.);
  c.text(610, 129, b, 28);
  time_t wall = localWall(now, m.settings.localZone);
  for (int k = 0; k < 7; k++) {
    int x = 24 + k * 108;
    time_t stamp =
        k == 3 ? now : fromZone(wall + (k - 3) * 3 * 86400 + 43200, m.settings.localZone);
    auto state = moonState(stamp);
    auto date = inZone(stamp, m.settings.localZone);
    snprintf(b, sizeof b, "%02d.%02d", date.tm_mday, date.tm_mon + 1);
    if (k == 3) {
      c.rect(x + 29, 161, 50, 21);
      c.text(x + 32, 177, b, 14, false);
    } else
      c.centered(x, 177, 104, b, 14);
    moonIcon(c, x + 52, 235, 31, state.phase, state.illuminated, m.settings.latitude < 0);
    const char *name = moonPhaseName(moonPhaseIndex(state.phase));
    char row[32];
    strlcpy(row, name, sizeof row);
    char *split = strchr(row, ' ');
    if (split) {
      *split++ = 0;
      c.centered(x, 291, 104, row, 14);
      c.centered(x, 307, 104, split, 14);
    } else
      c.centered(x, 299, 104, row, 14);
  }
  c.line(24, 329, 775, 329);
  int nextQuarter = (int(floor(moon.phase * 4)) + 1) % 4;
  for (int k = 0; k < 3; k++) {
    int x = 24 + k * 254;
    int quarter = k == 0 ? nextQuarter : k == 1 ? 2 : 0;
    time_t event = moonPhaseEvent(now, quarter);
    c.text(x, 356, k == 0 ? "NEXT PRIMARY PHASE" : k == 1 ? "NEXT FULL MOON" : "NEXT NEW MOON", 14);
    auto d = inZone(event, m.settings.localZone);
    snprintf(b, sizeof b, "%02d.%02d.%04d", d.tm_mday, d.tm_mon + 1, d.tm_year + 1900);
    c.text(x, 388, event ? b : "--", 20);
    c.fit(x, 414, 237, moonPhaseName(quarter * 2), 16);
  }
  c.text(24, 438, "Local calculation / approximate astronomical dates", 14);
}
void renderSun(Canvas &c, const Model &m, time_t now) {
  renderChrome(c, "SUNRISE / SUNSET", Page::Sun, m);
  if (!Clock::valid()) {
    c.text(24, 100, "Waiting for clock sync", 24);
    return;
  }
  const auto &s = m.settings;
  time_t wall = localWall(now, s.localZone), start = fromZone(wall, s.localZone),
         end = fromZone(wall + 86400, s.localZone);
  auto sun = solarDay(fromZone(wall + 43200, s.localZone), s.latitude, s.longitude);
  char b[96];
  c.fit(24, 77, 600, s.location, 16);
  for (int k = 0; k < 2; k++) {
    int x = k ? 500 : 56;
    c.text(x, 105, k ? "SUNSET" : "SUNRISE", 16);
    clockText(b, sizeof b, k ? sun.set : sun.rise, s.localZone);
    c.text(x, 157, b, 48);
  }
  if (sun.polarDay || sun.polarNight)
    c.centered(200, 183, 400,
               sun.polarDay ? "POLAR DAY / SUN ABOVE HORIZON" : "POLAR NIGHT / NO SUNRISE", 16);
  constexpr int gx = 40, gy = 198, gw = 720, gh = 135, steps = 120;
  double alt[steps + 1], low = -1, high = 1;
  for (int i = 0; i <= steps; i++) {
    alt[i] = solarAltitude(start + (end - start) * i / steps, s.latitude, s.longitude);
    low = std::min(low, alt[i]);
    high = std::max(high, alt[i]);
  }
  double scale = gh / (high - low);
  auto py = [&](double a) { return gy + int(round((high - a) * scale)); };
  int horizon = py(0);
  c.dottedLine(gx, horizon, gw);
  int current = gx + int(std::max(0., std::min(1., double(now - start) / (end - start))) * gw);
  for (int i = 0; i < steps; i++) {
    int x = gx + i * gw / steps, x2 = gx + (i + 1) * gw / steps, y = py(alt[i]),
        y2 = py(alt[i + 1]);
    if (x <= current || i % 2 == 0)
      c.line(x, y, x2, y2);
    for (int xx = x; xx < x2 && xx <= current; xx++) {
      int top = y + (y2 - y) * (xx - x) / (x2 - x);
      for (int yy = std::min(top, horizon); yy < std::max(top, horizon); yy++)
        if ((xx + yy) % 6 == 0)
          c.dot(xx, yy);
    }
  }
  int marker = py(solarAltitude(now, s.latitude, s.longitude));
  c.circle(current, marker, 4, 4);
  for (int h = 0; h <= 24; h += 6) {
    time_t stamp = fromZone(wall + h * 3600, s.localZone);
    int x = gx + int(double(stamp - start) / (end - start) * gw);
    snprintf(b, sizeof b, "%02d", h);
    c.text(std::min(x - 7, 754), 353, b, 14);
  }
  const char *labels[] = {"CIVIL DAWN", "SOLAR NOON", "CIVIL DUSK"};
  time_t times[] = {sun.dawn, sun.noon, sun.dusk};
  for (int i = 0; i < 3; i++) {
    int x = 48 + i * 254;
    c.text(x, 383, labels[i], 14);
    clockText(b, sizeof b, times[i], s.localZone);
    c.text(x, 418, b, 28);
  }
  c.text(24, 439, "Estimated / level horizon / civil twilight -6 deg", 14);
}
} // namespace ink
