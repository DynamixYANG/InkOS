// Solar/lunar coordinates adapted from SunCalc 1.9.0, Vladimir Agafonkin.
// BSD-2-Clause: see assets/SunCalc-LICENSE. Approximate astronomical estimates.
#include "Astronomy.h"
#include <algorithm>
#include <cmath>
namespace ink {
static constexpr double pi = 3.14159265358979323846, rad = pi / 180, ob = 23.4397 * rad;
static double days(time_t t) {
  return double(t) / 86400 - 10957.5;
}
static double ra(double l, double b) {
  return atan2(sin(l) * cos(ob) - tan(b) * sin(ob), cos(l));
}
static double dec(double l, double b) {
  return asin(sin(b) * cos(ob) + cos(b) * sin(ob) * sin(l));
}
static double anomaly(double d) {
  return rad * (357.5291 + .98560028 * d);
}
static double longitude(double m) {
  return m + rad * (1.9148 * sin(m) + .02 * sin(2 * m) + .0003 * sin(3 * m)) + rad * 102.9372 + pi;
}
MoonState moonState(time_t t) {
  double d = days(t), sl = longitude(anomaly(d)), sd = dec(sl, 0), sr = ra(sl, 0);
  double ml = rad * (218.316 + 13.176396 * d) + rad * 6.289 * sin(rad * (134.963 + 13.064993 * d));
  double mb = rad * 5.128 * sin(rad * (93.272 + 13.229350 * d));
  double md = dec(ml, mb), mr = ra(ml, mb),
         distance = 385001 - 20905 * cos(rad * (134.963 + 13.064993 * d));
  double phi =
      acos(std::max(-1., std::min(1., sin(sd) * sin(md) + cos(sd) * cos(md) * cos(sr - mr))));
  double incidence = atan2(149598000 * sin(phi), distance - 149598000 * cos(phi));
  double angle =
      atan2(cos(sd) * sin(sr - mr), sin(sd) * cos(md) - cos(sd) * sin(md) * cos(sr - mr));
  return {.5 + .5 * incidence * (angle < 0 ? -1 : 1) / pi, (1 + cos(incidence)) / 2};
}
int moonPhaseIndex(double p) {
  return int(floor(p * 8 + .5)) % 8;
}
const char *moonPhaseName(int i) {
  static const char *names[] = {"New Moon",  "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                                "Full Moon", "Waning Gibbous",  "Last Quarter",  "Waning Crescent"};
  return names[(i % 8 + 8) % 8];
}
static double distanceFromPhase(time_t t, double target) {
  double d = moonState(t).phase - target;
  return d - floor(d + .5);
}
time_t moonPhaseEvent(time_t now, int quarter, bool future) {
  double target = (quarter % 4) / 4.;
  time_t begin = future ? now + 1 : now - 36 * 86400, end = future ? now + 36 * 86400 : now - 1,
         result = 0;
  double previous = distanceFromPhase(begin, target);
  for (time_t t = begin + 21600; t <= end + 21600; t += 21600) {
    time_t b = std::min(t, end);
    double next = distanceFromPhase(b, target);
    if (previous <= 0 && next >= 0 && next - previous < .25) {
      time_t a = b - 21600;
      for (int i = 0; i < 16 && b - a > 1; i++) {
        time_t mid = a + (b - a) / 2;
        if (distanceFromPhase(mid, target) < 0)
          a = mid;
        else
          b = mid;
      }
      if (future)
        return b;
      result = b;
    }
    previous = next;
    if (b == end)
      break;
  }
  return result;
}
SolarDay solarDay(time_t t, double latitude, double lng) {
  double lw = -lng * rad, n = round(days(t) - .0009 - lw / (2 * pi));
  double ds = .0009 + lw / (2 * pi) + n, m = anomaly(ds), l = longitude(m), decl = dec(l, 0);
  double noon = 2451545 + ds + .0053 * sin(m) - .0069 * sin(2 * l), phi = latitude * rad;
  auto epoch = [](double julian) { return time_t(llround((julian - 2440587.5) * 86400)); };
  SolarDay out;
  out.noon = epoch(noon);
  auto pair = [&](double h, time_t &rise, time_t &set) {
    double c = (sin(h * rad) - sin(phi) * sin(decl)) / (cos(phi) * cos(decl));
    if (!std::isfinite(c) || c < -1 || c > 1)
      return c;
    double j =
        2451545 + .0009 + (acos(c) + lw) / (2 * pi) + n + .0053 * sin(m) - .0069 * sin(2 * l);
    set = epoch(j);
    rise = epoch(2 * noon - j);
    return c;
  };
  double c = pair(-.833, out.rise, out.set);
  out.polarDay = c < -1;
  out.polarNight = c > 1;
  pair(-6, out.dawn, out.dusk);
  return out;
}
SolarPair adjacentSolarEvents(time_t now, double lat, double lng) {
  SolarPair out;
  for (int radius = 0; radius <= 370; radius++) {
    for (int sign : {-1, 1}) {
      if (!radius && sign == 1)
        continue;
      auto day = solarDay(now + int64_t(sign) * radius * 86400, lat, lng);
      for (auto event : {SolarEvent{day.rise, true}, SolarEvent{day.set, false}}) {
        if (!event.at)
          continue;
        if (event.at <= now && event.at > out.previous.at)
          out.previous = event;
        if (event.at > now && (!out.next.at || event.at < out.next.at))
          out.next = event;
      }
    }
    // One extra neighbouring solar cycle prevents returning a farther event first.
    if (radius >= 2 && out.previous.at && out.next.at)
      break;
  }
  return out;
}
double solarAltitude(time_t t, double lat, double lng) {
  double d = days(t), l = longitude(anomaly(d)), decl = dec(l, 0),
         hour = rad * (280.16 + 360.9856235 * d + lng) - ra(l, 0);
  return asin(sin(lat * rad) * sin(decl) + cos(lat * rad) * cos(decl) * cos(hour)) / rad;
}
} // namespace ink
