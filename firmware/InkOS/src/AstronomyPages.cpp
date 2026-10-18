#include "Astronomy.h"
#include "MoonEvents.h"
#include "MdiIconsData.h"
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
// Moonrise/moonset for the current local day, cached per day and location.
static MoonDay homeMoonDay(const Settings &s, time_t now) {
  static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  static time_t cachedStart = 0;
  static double cachedLat = NAN, cachedLon = NAN;
  static MoonDay cached;
  time_t wall = localWall(now, s.localZone), start = fromZone(wall, s.localZone),
         end = fromZone(wall + 86400, s.localZone);
  portENTER_CRITICAL(&lock);
  bool hit = cachedStart == start && cachedLat == s.latitude && cachedLon == s.longitude;
  MoonDay out = cached;
  portEXIT_CRITICAL(&lock);
  if (hit) return out;
  out = moonRiseSet(start, end, s.latitude, s.longitude);
  portENTER_CRITICAL(&lock);
  cachedStart = start; cachedLat = s.latitude; cachedLon = s.longitude; cached = out;
  portEXIT_CRITICAL(&lock);
  return out;
}
// Horizon, thin crescent and direction arrow; 1-px strokes in the footprint of solarEventIcon.
void moonEventIcon(Canvas &c, int x, int y, bool rising, int size) {
  int base = y + size - 3, r = size / 4 + 1, cx = x + r + 1, cy = base - r - 2;
  c.line(x, base, x + size, base);
  // Outer limb: left half circle. Terminator: arc of a circle through both cusps, centred to the right.
  double inner = r * 0.7, ir = sqrt(inner * inner + double(r * r));
  for (int a = 90; a <= 270; a += 6) {
    double t = a * 3.141592653589793 / 180;
    c.dot(cx + int(lround(r * cos(t))), cy - int(lround(r * sin(t))));
  }
  double cusp = atan2(double(r), inner);
  for (int i = 0; i <= 12; i++) {
    double t = 3.141592653589793 - cusp + 2 * cusp * i / 12;
    c.dot(int(lround(cx + inner + ir * cos(t))), int(lround(cy - ir * sin(t))));
  }
  int ax = x + size - 4, top = y + 1, bottom = base - 3;
  c.line(ax, top, ax, bottom);
  int tip = rising ? top : bottom, tail = rising ? tip + 3 : tip - 3;
  c.line(ax, tip, ax - 3, tail);
  c.line(ax, tip, ax + 3, tail);
}
// Sun context for the aurora window: current sun state and the next two solar events.
static SunContext sunContext(time_t now, double lat, double lon, const SolarPair &pair) {
  SunContext s;
  s.sunUp = solarAltitude(now, lat, lon) > -0.833;
  s.next = pair.next.at; s.nextRising = pair.next.rising;
  if (pair.next.at && pair.next.at - now <= 86400) {
    auto after = adjacentSolarEvents(pair.next.at, lat, lon).next;
    s.following = after.at; s.followingRising = after.rising;
  }
  return s;
}
static AuroraTonight auroraTonight(const Settings &s, const Aurora &aurora, time_t now, const SolarPair &pair) {
  AuroraTonight out;
  out.window = nightWindow(now, sunContext(now, s.latitude, s.longitude, pair));
  if (out.window.kind != NightKind::PolarDay) out.peak = peakKp(aurora, out.window.start, out.window.end);
  return out;
}
AuroraTonight auroraTonight(const Settings &s, const Aurora &aurora, time_t now) {
  return auroraTonight(s, aurora, now, adjacentSolarEvents(now, s.latitude, s.longitude));
}
// Moon (x 236-336) and sun (x 336-436) columns. Both use the shared Home rows: row 1 is the
// 64 px disc / PREV, rows 2 and 3 carry moonrise / NEXT and moonset / tonight's Kp.
void renderHomeAstronomy(Canvas &c, const Settings &s, const Aurora &aurora, time_t now) {
  using namespace layout;
  c.line(WeatherDivider, HomeBlockTop, WeatherDivider, HomeBottom);
  c.line(MoonDivider, HomeBlockTop, MoonDivider, HomeBottom);
  constexpr int moonX = WeatherDivider + 12, moonCenter = (WeatherDivider + MoonDivider) / 2;
  if (!Clock::valid()) {
    c.centered(WeatherDivider, HomeRowValue[1], MoonDivider - WeatherDivider, "--", 18);
    c.centered(MoonDivider, HomeRowValue[1], HomeLeftRight - MoonDivider, "--", 18);
    return;
  }
  auto moon = moonState(now);
  moonIcon(c, moonCenter, HomeIconTop + 32, 32, moon.phase, moon.illuminated, s.latitude < 0);
  auto day = homeMoonDay(s, now);
  char t[24];
  if (day.alwaysUp || day.alwaysDown) {
    c.centered(WeatherDivider, 404, MoonDivider - WeatherDivider, day.alwaysUp ? "ABOVE" : "BELOW", 14);
    c.centered(WeatherDivider, HomeRowLabel[2], MoonDivider - WeatherDivider, "HORIZON", 14);
  } else
    for (int k = 0; k < 2; k++) {
      int baseline = HomeRowValue[1 + k];
      moonEventIcon(c, moonX, baseline - 19, !k, 18);
      time_t at = k ? day.set : day.rise;
      clockText(t, sizeof t, at ? at + 30 : 0, s.localZone); // nearest minute, like almanacs
      c.text(moonX + 26, baseline, t, 20);
    }
  auto pair = adjacentSolarEvents(now, s.latitude, s.longitude);
  for (int k = 0; k < 2; k++) {
    auto event = k ? pair.next : pair.previous;
    c.text(SolarLeft, HomeRowLabel[k], k ? "NEXT" : "PREV", 14);
    if (event.at) solarEventIcon(c, SolarLeft, HomeRowValue[k] - 19, event.rising, 18);
    clockText(t, sizeof t, event.at, s.localZone);
    c.text(SolarLeft + 26, HomeRowValue[k], t, 20);
  }
  // Tonight's highest planetary K-index (NOAA SWPC 3-hour bins), MDI aurora at native 40 px.
  KpPeak peak = auroraTonight(s, aurora, now, pair).peak;
  kpText(t, sizeof t, peak.kp);
  if (peak.kp >= 0 && kpStale(aurora, now)) strlcat(t, "?", sizeof t); // older than 6 h
  c.image(SolarLeft, HomeRowValue[2] - 38, 40, 40, mdi_assets::aurora_40);
  c.text(SolarLeft + 44, HomeRowLabel[2], "Kp", 14);
  c.text(SolarLeft + 44, HomeRowValue[2], t, 20);
}
void renderMoon(Canvas &c, const Model &m, time_t now) {
  renderChrome(c, "LUNAR CALENDAR", Page::Moon, m);
  if (!Clock::valid()) {
    c.text(12, 100, "Waiting for clock sync", 24);
    return;
  }
  auto moon = moonState(now);
  char b[96];
  int index = moonPhaseIndex(moon.phase);
  c.text(12, 88, "CURRENT PHASE", 14);
  c.fit(12, 129, 240, moonPhaseName(index), 28);
  c.text(272, 88, "ILLUMINATION", 14);
  snprintf(b, sizeof b, "%.1f%%", moon.illuminated * 100);
  c.text(272, 129, b, 28);
  auto lastNew = moonPhaseEvent(now, 0, false);
  c.text(532, 88, "LUNAR AGE / DAYS", 14);
  snprintf(b, sizeof b, "%.1f", lastNew ? double(now - lastNew) / 86400 : 0.);
  c.text(532, 129, b, 28);
  time_t wall = localWall(now, m.settings.localZone);
  for (int k = 0; k < 7; k++) {
    int x = 12 + k * 111;
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
    moonIcon(c, x + 54, 248, 34, state.phase, state.illuminated, m.settings.latitude < 0);
    const char *name = moonPhaseName(moonPhaseIndex(state.phase));
    char row[32];
    strlcpy(row, name, sizeof row);
    char *split = strchr(row, ' ');
    if (split) {
      *split++ = 0;
      c.centered(x, 308, 104, row, 14);
      c.centered(x, 326, 104, split, 14);
    } else
      c.centered(x, 316, 104, row, 14);
  }
  c.line(12, 348, 787, 348);
  int nextQuarter = (int(floor(moon.phase * 4)) + 1) % 4;
  for (int k = 0; k < 3; k++) {
    int x = 12 + k * 260;
    int quarter = k == 0 ? nextQuarter : k == 1 ? 2 : 0;
    time_t event = moonPhaseEvent(now, quarter);
    c.text(x, 380, k == 0 ? "NEXT PRIMARY PHASE" : k == 1 ? "NEXT FULL MOON" : "NEXT NEW MOON", 14);
    auto d = inZone(event, m.settings.localZone);
    snprintf(b, sizeof b, "%02d.%02d.%04d", d.tm_mday, d.tm_mon + 1, d.tm_year + 1900);
    c.text(x, 414, event ? b : "--", 20);
    c.fit(x, 446, 244, moonPhaseName(quarter * 2), 16);
  }

}
void renderSun(Canvas &c, const Model &m, time_t now) {
  renderChrome(c, "SUNRISE / SUNSET", Page::Sun, m);
  if (!Clock::valid()) {
    c.text(12, 100, "Waiting for clock sync", 24);
    return;
  }
  const auto &s = m.settings;
  time_t wall = localWall(now, s.localZone), start = fromZone(wall, s.localZone),
         end = fromZone(wall + 86400, s.localZone);
  auto sun = solarDay(fromZone(wall + 43200, s.localZone), s.latitude, s.longitude);
  char b[96];
  c.fit(12, 77, 600, s.location, 16);
  for (int k = 0; k < 2; k++) {
    int x = 12 + k * 400;
    c.centered(x, 105, 376, k ? "SUNSET" : "SUNRISE", 16);
    clockText(b, sizeof b, k ? sun.set : sun.rise, s.localZone);
    c.centered(x, 157, 376, b, 48);
  }
  if (sun.polarDay || sun.polarNight)
    c.centered(200, 183, 400,
               sun.polarDay ? "POLAR DAY / SUN ABOVE HORIZON" : "POLAR NIGHT / NO SUNRISE", 16);
  constexpr int gx = 24, gy = 198, gw = 752, gh = 160, steps = 120;
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
    c.text(std::min(x - 7, 754), 378, b, 14);
  }
  const char *labels[] = {"CIVIL DAWN", "SOLAR NOON", "CIVIL DUSK"};
  time_t times[] = {sun.dawn, sun.noon, sun.dusk};
  for (int i = 0; i < 3; i++) {
    int x = 24 + i * 260;
    c.text(x, 410, labels[i], 14);
    clockText(b, sizeof b, times[i], s.localZone);
    c.text(x, 450, b, 28);
  }

}
} // namespace ink
