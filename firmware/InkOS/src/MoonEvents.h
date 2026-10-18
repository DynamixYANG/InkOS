#pragma once
#include <time.h>
namespace ink {
// Apparent geocentric Moon (Meeus ch. 47, ~10" accuracy) and moonrise/moonset search.
struct LunarPosition {
  double ra = 0, dec = 0, distanceKm = 0, parallax = 0; // radians, km, radians
};
struct MoonDay {
  time_t rise = 0, set = 0;        // 0: no such event in the interval
  bool alwaysUp = false, alwaysDown = false; // no crossing at all in the interval
};
LunarPosition lunarPosition(time_t utc);
// Geocentric altitude of the Moon's centre minus the rise/set altitude
// h0 = 0.7275 * parallax - 0.5667 deg (refraction 34', semidiameter, parallax), in degrees.
double lunarHorizonAltitude(time_t utc, double latitude, double longitude);
// First rise and first set in [start, end), e.g. one local calendar day.
MoonDay moonRiseSet(time_t start, time_t end, double latitude, double longitude);
} // namespace ink
