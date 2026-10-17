#include "CalendarLayout.h"
#include "Zones.h"
#include <esp_heap_caps.h>
#include <new>
namespace ink {
void calendarLayoutChecks(int &passed, int &total) {
  using namespace weekgrid;
  auto check = [&](bool value) { total++; if (value) passed++; else Serial.printf("INKOS selftest failed_case=%d\n", total); };
  for (auto sample : {Clock::epoch(2021, 1, 1), Clock::epoch(2021, 1, 4), Clock::epoch(2024, 12, 30), Clock::epoch(2026, 10, 15)}) {
    auto w = isoWeek(Clock::utc(sample));
    if (sample == Clock::epoch(2021, 1, 1)) check(w.year == 2020 && w.number == 53);
    else if (sample == Clock::epoch(2021, 1, 4)) check(w.year == 2021 && w.number == 1);
    else if (sample == Clock::epoch(2024, 12, 30)) check(w.year == 2025 && w.number == 1);
    else check(w.year == 2026 && w.number == 42);
  }
  auto agenda = (Agenda *)heap_caps_malloc(sizeof(Agenda), MALLOC_CAP_SPIRAM);
  auto day = (DayPlan *)heap_caps_malloc(sizeof(DayPlan), MALLOC_CAP_SPIRAM);
  auto all = (AllDayPlan *)heap_caps_malloc(sizeof(AllDayPlan), MALLOC_CAP_SPIRAM);
  if (!agenda || !day || !all) { check(false); free(agenda); free(day); free(all); return; }
  new (agenda) Agenda(); new (day) DayPlan(); new (all) AllDayPlan();
  time_t monday = Clock::epoch(2026, 10, 12);
  const char *zone = "Europe/Helsinki";
  auto add = [&](time_t wallStart, time_t wallEnd, bool allDay = false) {
    auto &e = agenda->events[agenda->count++];
    e.start = fromZone(wallStart, zone); e.end = fromZone(wallEnd, zone); e.allDay = allDay;
  };
  add(monday + 9 * 3600, monday + 11 * 3600);
  add(monday + 9 * 3600 + 600, monday + 10 * 3600);
  add(monday + 9 * 3600 + 1200, monday + 10 * 3600 + 600);
  add(monday + 14 * 3600, monday + 15 * 3600);
  auto range = visibleRange(*agenda, monday, zone, fromZone(monday + 10 * 3600, zone));
  planDay(*agenda, monday, zone, range, *day);
  check(range.first == 480 && range.last == 1200 && day->count == 4 && day->hidden == 1);
  check(day->blocks[0].lane == 0 && day->blocks[0].lanes == 2 && day->blocks[3].lane == 0 && day->blocks[3].lanes == 1);
  bool separated = true;
  for (int i = 0; i < day->count; i++) for (int j = i + 1; j < day->count; j++) {
    const auto &a = day->blocks[i], &b = day->blocks[j];
    if (a.lane == b.lane) separated &= a.bottom + 2 <= b.top || b.bottom + 2 <= a.top;
  }
  check(separated);
  new (agenda) Agenda();
  add(monday - 3600, monday + 1800); // Monday continuation.
  add(monday + 86400 - 1800, monday + 86400 + 1800);
  add(monday + 3 * 86400, monday + 3 * 86400); // Invalid zero duration.
  range = visibleRange(*agenda, monday, zone, fromZone(monday + 10 * 3600, zone));
  planDay(*agenda, monday, zone, range, *day);
  check(range.first == 0 && range.last == 1440 && day->count == 2);
  planDay(*agenda, monday + 86400, zone, range, *day);
  check(day->count == 1 && day->blocks[0].start == 0 && day->blocks[0].end == 30);
  new (agenda) Agenda();
  add(monday, monday + 3 * 86400, true);
  add(monday, monday + 86400, true); add(monday, monday + 86400, true);
  add(monday + 3 * 86400, monday + 4 * 86400, true);
  planAllDay(*agenda, monday, zone, *all);
  check(all->count == 4 && all->blocks[0].first == 0 && all->blocks[0].last == 3 && all->hidden[0] == 1);
  check(all->blocks[3].lane == 0 && all->hidden[1] == 0);
  new (agenda) Agenda();
  monday = Clock::epoch(2026, 10, 19);
  agenda->count = 1; agenda->events[0].start = Clock::epoch(2026, 10, 25, 0, 50);
  agenda->events[0].end = Clock::epoch(2026, 10, 25, 1, 10);
  range = visibleRange(*agenda, monday, zone, Clock::epoch(2026, 10, 25, 0, 30));
  planDay(*agenda, monday + 6 * 86400, zone, range, *day);
  check(day->count == 1 && day->blocks[0].bottom > day->blocks[0].top && day->blocks[0].start == 230);
  check(minuteY(range.first, range) == Top && minuteY(range.last, range) == Bottom);
  free(agenda); free(day); free(all);
}
}
