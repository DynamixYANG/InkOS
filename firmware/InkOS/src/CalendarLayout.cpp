#include "CalendarLayout.h"
#include "Zones.h"
#include <algorithm>
namespace ink {
IsoWeek isoWeek(const tm &date) {
  time_t wall = Clock::epoch(date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);
  auto thursday = Clock::utc(wall + (3 - (date.tm_wday + 6) % 7) * 86400);
  return {thursday.tm_year + 1900, thursday.tm_yday / 7 + 1};
}
time_t agendaFirstDay(time_t now, const char *zone) {
  auto local = inZone(now, zone);
  return Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}
namespace weekgrid {
int minuteY(int minute, const Range &range) {
  return Top + (std::clamp(minute, range.first, range.last) - range.first) * (Bottom - Top) /
                   (range.last - range.first);
}
static bool intersects(const CalendarEvent &e, time_t start, time_t end) {
  return e.end > e.start && e.start < end && e.end > start;
}
static void minutes(const CalendarEvent &e, time_t start, time_t end, const char *zone,
                    int &a, int &b) {
  auto s = inZone(e.start, zone), z = inZone(e.end, zone);
  a = e.start <= start ? 0 : s.tm_hour * 60 + s.tm_min;
  b = e.end >= end ? 1440 : z.tm_hour * 60 + z.tm_min + (z.tm_sec ? 1 : 0);
  // A fall-back event can end earlier on the wall clock. Keep it visible as an overlap.
  b = std::min(1440, std::max(a + 1, b));
}
Range visibleRange(const Agenda &agenda, time_t monday, const char *zone, time_t now) {
  Range range;
  for (int d = 0; d < 7; d++) {
    time_t start = fromZone(monday + d * 86400, zone), end = fromZone(monday + (d + 1) * 86400, zone);
    for (int i = 0; i < std::min(agenda.count, MAX_EVENTS); i++) {
      const auto &e = agenda.events[i];
      if (e.allDay || !intersects(e, start, end)) continue;
      int a, b; minutes(e, start, end, zone, a, b);
      range.first = std::min(range.first, a / 60 * 60);
      range.last = std::max(range.last, (b + 59) / 60 * 60);
    }
  }
  auto local = inZone(now, zone);
  time_t today = Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  if (today >= monday && today < monday + 7 * 86400) {
    range.first = std::min(range.first, local.tm_hour * 60);
    range.last = std::max(range.last, std::min(1440, (local.tm_hour + 1) * 60));
  }
  return range;
}
void planDay(const Agenda &agenda, time_t dayWall, const char *zone, const Range &range, DayPlan &plan) {
  plan.count = plan.hidden = 0;
  time_t start = fromZone(dayWall, zone), end = fromZone(dayWall + 86400, zone);
  for (int i = 0; i < std::min(agenda.count, MAX_EVENTS); i++) {
    const auto &e = agenda.events[i];
    if (e.allDay || !intersects(e, start, end)) continue;
    int a, b; minutes(e, start, end, zone, a, b);
    int top = std::min(Bottom - MinimumBlock, minuteY(a, range));
    int bottom = std::min(Bottom, std::max(top + MinimumBlock, minuteY(b, range)));
    plan.blocks[plan.count++] = {i, a, b, top, bottom};
  }
  std::sort(plan.blocks, plan.blocks + plan.count, [](const auto &a, const auto &b) {
    if (a.top != b.top) return a.top < b.top;
    if (a.bottom != b.bottom) return a.bottom > b.bottom;
    return a.event < b.event;
  });
  int cluster = 0, clusterEnd = 0, used = 0, ends[MAX_EVENTS] = {};
  auto finish = [&](int until) {
    for (int i = cluster; i < until; i++) plan.blocks[i].lanes = std::min(used, MaxLanes);
  };
  for (int i = 0; i < plan.count; i++) {
    auto &b = plan.blocks[i];
    if (i && b.top >= clusterEnd + 2) { finish(i); cluster = i; used = 0; }
    int lane = 0;
    while (lane < used && ends[lane] + 2 > b.top) lane++;
    if (lane == used) used++;
    ends[lane] = b.bottom; b.lane = lane;
    clusterEnd = i == cluster ? b.bottom : std::max(clusterEnd, b.bottom);
    if (lane >= MaxLanes) plan.hidden++;
  }
  finish(plan.count);
}
void planAllDay(const Agenda &agenda, time_t monday, const char *zone, AllDayPlan &plan) {
  plan.count = 0; std::fill(plan.hidden, plan.hidden + 7, 0);
  for (int i = 0; i < std::min(agenda.count, MAX_EVENTS); i++) {
    const auto &e = agenda.events[i];
    if (!e.allDay) continue;
    int first = 7, last = 0;
    for (int d = 0; d < 7; d++)
      if (intersects(e, fromZone(monday + d * 86400, zone), fromZone(monday + (d + 1) * 86400, zone))) {
        first = std::min(first, d); last = d + 1;
      }
    if (first < last) plan.blocks[plan.count++] = {i, first, last, 0};
  }
  std::sort(plan.blocks, plan.blocks + plan.count, [](const auto &a, const auto &b) {
    if (a.first != b.first) return a.first < b.first;
    if (a.last != b.last) return a.last > b.last;
    return a.event < b.event;
  });
  int ends[MAX_EVENTS] = {}, used = 0;
  for (int i = 0; i < plan.count; i++) {
    auto &b = plan.blocks[i]; int lane = 0;
    while (lane < used && ends[lane] > b.first) lane++;
    if (lane == used) used++;
    ends[lane] = b.last; b.lane = lane;
    if (lane >= AllDayRows) for (int d = b.first; d < b.last; d++) plan.hidden[d]++;
  }
}
}
}
