#include "Zones.h"
#include "Domain.h"
#include <string.h>
namespace ink {
// rule: 0 fixed, 1 EU last-Sunday transitions, 2 US, 3 eastern Australia.
static const ZoneRule zones[] = {{"UTC", 0, 0, 0},
                                 {"Europe/Helsinki", 120, 180, 1},
                                 {"Europe/London", 0, 60, 1},
                                 {"Europe/Stockholm", 60, 120, 1},
                                 {"Europe/Oslo", 60, 120, 1},
                                 {"Europe/Berlin", 60, 120, 1},
                                 {"Europe/Paris", 60, 120, 1},
                                 {"Europe/Tallinn", 120, 180, 1},
                                 {"Europe/Athens", 120, 180, 1},
                                 {"Europe/Zurich", 60, 120, 1},
                                 {"Europe/Madrid", 60, 120, 1},
                                 {"Europe/Rome", 60, 120, 1},
                                 {"Europe/Moscow", 180, 180, 0},
                                 {"Asia/Shanghai", 480, 480, 0},
                                 {"Asia/Hong_Kong", 480, 480, 0},
                                 {"Asia/Singapore", 480, 480, 0},
                                 {"Asia/Tokyo", 540, 540, 0},
                                 {"Asia/Seoul", 540, 540, 0},
                                 {"Asia/Kolkata", 330, 330, 0},
                                 {"Asia/Dubai", 240, 240, 0},
                                 {"America/New_York", -300, -240, 2},
                                 {"America/Chicago", -360, -300, 2},
                                 {"America/Denver", -420, -360, 2},
                                 {"America/Los_Angeles", -480, -420, 2},
                                 {"America/Phoenix", -420, -420, 0},
                                 {"Australia/Sydney", 600, 660, 3},
                                 {"Australia/Brisbane", 600, 600, 0},
                                 {"Pacific/Honolulu", -600, -600, 0}};
const ZoneRule *findZone(const char *id) {
  if (!id)
    return nullptr;
  if (!strcmp(id, "FLE Standard Time"))
    id = "Europe/Helsinki";
  if (!strcmp(id, "GMT Standard Time"))
    id = "Europe/London";
  if (!strcmp(id, "W. Europe Standard Time"))
    id = "Europe/Berlin";
  if (!strcmp(id, "China Standard Time"))
    id = "Asia/Shanghai";
  if (!strcmp(id, "Eastern Standard Time"))
    id = "America/New_York";
  if (!strcmp(id, "Pacific Standard Time"))
    id = "America/Los_Angeles";
  if (!strcmp(id, "Etc/UTC") || !strcmp(id, "GMT"))
    id = "UTC";
  for (auto &z : zones)
    if (!strcmp(z.id, id))
      return &z;
  return nullptr;
}
static int sunday(int y, int month, int n) {
  if (n == 5) {
    int last = Clock::monthDays(y, month);
    return last - Clock::utc(Clock::epoch(y, month, last)).tm_wday;
  }
  return 1 + (7 - Clock::utc(Clock::epoch(y, month, 1)).tm_wday) % 7 + (n - 1) * 7;
}
int zoneOffset(time_t t, const char *id) {
  auto z = findZone(id);
  if (!z)
    return 0;
  int y = Clock::utc(t).tm_year + 1900;
  bool dst = false;
  if (z->rule == 1)
    dst =
        t >= Clock::epoch(y, 3, sunday(y, 3, 5), 1) && t < Clock::epoch(y, 10, sunday(y, 10, 5), 1);
  if (z->rule == 2)
    dst = t >= Clock::epoch(y, 3, sunday(y, 3, 2), 2) - z->standardMinutes * 60 &&
          t < Clock::epoch(y, 11, sunday(y, 11, 1), 2) - z->summerMinutes * 60;
  if (z->rule == 3)
    dst = t < Clock::epoch(y, 4, sunday(y, 4, 1), 3) - z->summerMinutes * 60 ||
          t >= Clock::epoch(y, 10, sunday(y, 10, 1), 2) - z->standardMinutes * 60;
  return (dst ? z->summerMinutes : z->standardMinutes) * 60;
}
tm inZone(time_t t, const char *id) {
  return Clock::utc(t + zoneOffset(t, id));
}
time_t fromZone(time_t wall, const char *id) {
  auto z = findZone(id);
  if (!z)
    return wall;
  time_t standard = wall - z->standardMinutes * 60;
  if (zoneOffset(standard, id) == z->standardMinutes * 60)
    return standard;
  return wall - z->summerMinutes * 60;
}
} // namespace ink
