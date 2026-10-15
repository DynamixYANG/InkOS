#pragma once
#include <time.h>
namespace ink {
struct ZoneRule {
  const char *id;
  int standardMinutes;
  int summerMinutes;
  int rule;
};
const ZoneRule *findZone(const char *);
tm inZone(time_t, const char *);
time_t fromZone(time_t wall, const char *);
int zoneOffset(time_t, const char *);
} // namespace ink
