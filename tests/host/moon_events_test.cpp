#include "MoonEvents.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace ink;
// Reference: USNO "Rise/Set/Transit" one-day API (aa.usno.navy.mil/api/rstt/oneday), minute resolution.
// Oulu rows use the Europe/Helsinki local day; the polar and southern rows use the UTC day.
// rise/set are the first event of the day in Unix seconds, 0 when the event does not occur.
struct Case { const char *name; double lat, lon; long start, end, rise, set; int up, down; };
static const Case cases[] = {
  {"2026-01-02 Oulu", 65.0121, 25.4651, 1767304800, 1767391200, 0, 0, 1, 0},
  {"2026-01-05 Oulu", 65.0121, 25.4651, 1767564000, 1767650400, 1767627180, 1767605700, 0, 0},
  {"2026-02-14 Oulu", 65.0121, 25.4651, 1771020000, 1771106400, 0, 0, 0, 1},
  {"2026-03-21 Oulu", 65.0121, 25.4651, 1774044000, 1774130400, 1774064940, 1774129680, 0, 0},
  {"2026-04-09 Oulu", 65.0121, 25.4651, 1775682000, 1775768400, 0, 0, 0, 1},
  {"2026-06-21 Oulu", 65.0121, 25.4651, 1781989200, 1782075600, 1782035460, 1781993040, 0, 0},
  {"2026-06-25 Oulu", 65.0121, 25.4651, 1782334800, 1782421200, 1782406260, 1782335820, 0, 0},
  {"2026-09-28 Oulu", 65.0121, 25.4651, 1790542800, 1790629200, 1790608020, 1790580360, 0, 0},
  {"2026-10-15 Oulu", 65.0121, 25.4651, 1792011600, 1792098000, 0, 0, 0, 1},
  {"2026-10-18 Oulu", 65.0121, 25.4651, 1792270800, 1792357200, 1792336260, 1792345320, 0, 0},
  {"2026-10-19 Oulu", 65.0121, 25.4651, 1792357200, 1792443600, 1792420680, 1792439580, 0, 0},
  {"2026-10-26 Oulu", 65.0121, 25.4651, 1792965600, 1793052000, 1793020260, 1792997220, 0, 0},
  {"2026-11-03 Oulu", 65.0121, 25.4651, 1793656800, 1793743200, 0, 1793711580, 0, 0},
  {"2026-12-21 Oulu", 65.0121, 25.4651, 1797804000, 1797890400, 1797844080, 1797831000, 0, 0},
  {"2026-10-18 Longyearbyen UTC", 78.2232, 15.6267, 1792281600, 1792368000, 0, 0, 0, 1},
  {"2026-10-22 Longyearbyen UTC", 78.2232, 15.6267, 1792627200, 1792713600, 1792683120, 0, 0, 0},
  {"2026-10-26 Longyearbyen UTC", 78.2232, 15.6267, 1792972800, 1793059200, 0, 0, 1, 0},
  {"2026-10-29 Longyearbyen UTC", 78.2232, 15.6267, 1793232000, 1793318400, 0, 0, 1, 0},
  {"2026-10-18 Sydney UTC", -33.8688, 151.2093, 1792281600, 1792368000, 1792282320, 1792335840, 0, 0},
  {"2026-03-05 Sydney UTC", -33.8688, 151.2093, 1772668800, 1772755200, 1772701980, 1772748720, 0, 0}};
static int failures = 0;
static void expect(bool ok, const char *name, const char *what) {
  if (!ok) { printf("FAIL %s: %s\n", name, what); failures++; }
}
static bool near(time_t got, long want) { return want ? got && std::labs(long(got) - want) <= 120 : !got; }
int main() {
  // Meeus example 47.a, 1992-04-12 0h TD: RA 134.688470, Dec 13.768368, distance 368409.7 km.
  LunarPosition p = lunarPosition(703036800 - 69);
  const double deg = 180 / 3.141592653589793;
  p.ra *= deg, p.dec *= deg;
  expect(std::fabs(p.ra - 134.688470) < .02 && std::fabs(p.dec - 13.768368) < .01, "Meeus 47.a", "ra/dec");
  expect(std::fabs(p.distanceKm - 368409.7) < 30, "Meeus 47.a", "distance");
  for (const Case &c : cases) {
    MoonDay d = moonRiseSet(c.start, c.end, c.lat, c.lon);
    expect(near(d.rise, c.rise), c.name, "rise");
    expect(near(d.set, c.set), c.name, "set");
    expect(d.alwaysUp == bool(c.up) && d.alwaysDown == bool(c.down), c.name, "polar flags");
    if (d.rise) expect(std::fabs(lunarHorizonAltitude(d.rise, c.lat, c.lon)) < .01, c.name, "rise altitude");
  }
  MoonDay bad = moonRiseSet(100, 50, 65, 25);
  expect(!bad.rise && !bad.set && !bad.alwaysUp && !bad.alwaysDown, "empty window", "no events");
  if (failures) return 1;
  printf("OK %d cases\n", int(sizeof cases / sizeof *cases));
}
