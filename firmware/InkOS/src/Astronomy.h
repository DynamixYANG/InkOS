#pragma once
#include <time.h>
namespace ink {
struct MoonState {
  double phase = 0, illuminated = 0;
};
struct SolarDay {
  time_t rise = 0, set = 0, noon = 0, dawn = 0, dusk = 0;
  bool polarDay = false, polarNight = false;
};
struct SolarEvent {
  time_t at = 0;
  bool rising = false;
};
struct SolarPair {
  SolarEvent previous, next;
};
MoonState moonState(time_t);
int moonPhaseIndex(double phase);
const char *moonPhaseName(int);
time_t moonPhaseEvent(time_t now, int quarter, bool future = true);
SolarDay solarDay(time_t localNoon, double latitude, double longitude);
SolarPair adjacentSolarEvents(time_t now, double latitude, double longitude);
double solarAltitude(time_t, double latitude, double longitude);
} // namespace ink
