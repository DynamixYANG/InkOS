// Moon position: J. Meeus, Astronomical Algorithms, 2nd ed., chapter 47 (ELP-2000/82 truncated
// to the periodic terms of tables 47.A/47.B), nutation main terms (ch. 22), sidereal time (ch. 12).
// Rise/set: altitude crossing of h0 = 0.7275 * parallax - 34' (Meeus ch. 15), refined to 1 s.
#include "MoonEvents.h"
#include "MoonTerms.h"
#include <cmath>
namespace ink {
static constexpr double Pi = 3.14159265358979323846, Rad = Pi / 180;
static constexpr double DeltaT = 69.2; // TT - UT seconds, 2025-2030 (IERS/USNO estimates)
static double reduce(double degrees) { return fmod(fmod(degrees, 360) + 360, 360); }
static double julian(time_t t) { return double(t) / 86400 + 2440587.5; }
struct Apparent { LunarPosition pos; double nutationRa = 0; };
static Apparent apparent(time_t utc) {
  double jde = julian(utc) + DeltaT / 86400, T = (jde - 2451545) / 36525;
  double Lp = reduce(218.3164477 + T * (481267.88123421 + T * (-0.0015786 + T * (1 / 538841. - T / 65194000.))));
  double D = reduce(297.8501921 + T * (445267.1114034 + T * (-0.0018819 + T * (1 / 545868. - T / 113065000.))));
  double M = reduce(357.5291092 + T * (35999.0502909 + T * (-0.0001536 + T / 24490000.)));
  double Mp = reduce(134.9633964 + T * (477198.8675055 + T * (0.0087414 + T * (1 / 69699.9 + T / 14712000.))));
  double F = reduce(93.2720950 + T * (483202.0175233 + T * (-0.0036539 + T * (-1 / 3526000. + T / 863310000.))));
  double A1 = reduce(119.75 + 131.849 * T), A2 = reduce(53.09 + 479264.290 * T), A3 = reduce(313.45 + 481266.484 * T);
  double E = 1 - T * (0.002516 + 0.0000074 * T), E2 = E * E;
  D *= Rad; M *= Rad; Mp *= Rad; F *= Rad;
  double sl = 0, sr = 0, sb = 0;
  for (const auto &k : LunarLR) {
    double arg = k.d * D + k.m * M + k.mp * Mp + k.f * F, e = k.m ? (k.m == 1 || k.m == -1 ? E : E2) : 1;
    sl += e * k.l * sin(arg);
    sr += e * k.r * cos(arg);
  }
  for (const auto &k : LunarB) {
    double arg = k.d * D + k.m * M + k.mp * Mp + k.f * F, e = k.m ? (k.m == 1 || k.m == -1 ? E : E2) : 1;
    sb += e * k.b * sin(arg);
  }
  sl += 3958 * sin(A1 * Rad) + 1962 * sin((Lp * Rad) - F) + 318 * sin(A2 * Rad);
  sb += -2235 * sin(Lp * Rad) + 382 * sin(A3 * Rad) + 175 * sin(A1 * Rad - F) + 175 * sin(A1 * Rad + F) +
        127 * sin(Lp * Rad - Mp) - 115 * sin(Lp * Rad + Mp);
  // Nutation, main terms (arcseconds).
  double omega = reduce(125.04452 - 1934.136261 * T) * Rad, L = reduce(280.4665 + 36000.7698 * T) * Rad,
         L2 = reduce(218.3165 + 481267.8813 * T) * Rad;
  double dpsi = -17.20 * sin(omega) - 1.32 * sin(2 * L) - 0.23 * sin(2 * L2) + 0.21 * sin(2 * omega);
  double deps = 9.20 * cos(omega) + 0.57 * cos(2 * L) + 0.10 * cos(2 * L2) - 0.09 * cos(2 * omega);
  double eps = (23.4392911111 - T * (0.0130041667 + T * (1.6389e-7 - T * 5.0361e-7)) + deps / 3600) * Rad;
  double lambda = (Lp + sl / 1e6 + dpsi / 3600) * Rad, beta = sb / 1e6 * Rad, distance = 385000.56 + sr / 1000;
  Apparent out;
  out.pos.ra = atan2(sin(lambda) * cos(eps) - tan(beta) * sin(eps), cos(lambda));
  out.pos.dec = asin(sin(beta) * cos(eps) + cos(beta) * sin(eps) * sin(lambda));
  out.pos.distanceKm = distance;
  out.pos.parallax = asin(6378.14 / distance);
  out.nutationRa = dpsi / 3600 * Rad * cos(eps);
  return out;
}
LunarPosition lunarPosition(time_t utc) { return apparent(utc).pos; }
double lunarHorizonAltitude(time_t utc, double latitude, double longitude) {
  auto a = apparent(utc);
  double jd = julian(utc), T = (jd - 2451545) / 36525;
  double gmst = reduce(280.46061837 + 360.98564736629 * (jd - 2451545) + T * T * (0.000387933 - T / 38710000));
  double hour = gmst * Rad + a.nutationRa + longitude * Rad - a.pos.ra, phi = latitude * Rad;
  double h = asin(sin(phi) * sin(a.pos.dec) + cos(phi) * cos(a.pos.dec) * cos(hour)) / Rad;
  return h - (0.7275 * a.pos.parallax / Rad - 0.5667);
}
MoonDay moonRiseSet(time_t start, time_t end, double latitude, double longitude) {
  MoonDay out;
  if (end <= start || !std::isfinite(latitude) || !std::isfinite(longitude)) return out;
  constexpr time_t Step = 600;
  time_t a = start;
  double fa = lunarHorizonAltitude(a, latitude, longitude);
  bool crossed = false, firstUp = fa > 0;
  while (a < end && (!out.rise || !out.set)) {
    time_t b = a + Step < end ? a + Step : end;
    double fb = lunarHorizonAltitude(b, latitude, longitude);
    if ((fa <= 0) != (fb <= 0)) {
      bool rising = fb > 0;
      time_t lo = a, hi = b;
      double flo = fa;
      while (hi - lo > 1) {
        time_t mid = lo + (hi - lo) / 2;
        double fm = lunarHorizonAltitude(mid, latitude, longitude);
        if ((fm <= 0) == (flo <= 0)) { lo = mid; flo = fm; } else hi = mid;
      }
      // Round to the nearest second of the crossing; events at exactly `end` belong to the next day.
      time_t at = hi;
      if (at < end) {
        crossed = true;
        if (rising && !out.rise) out.rise = at;
        if (!rising && !out.set) out.set = at;
      }
    }
    a = b; fa = fb;
  }
  if (!crossed) {
    out.alwaysUp = firstUp;
    out.alwaysDown = !firstUp;
  }
  return out;
}
} // namespace ink
