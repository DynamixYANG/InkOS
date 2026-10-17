#include "CalendarLayout.h"
#include "Canvas.h"
#include "Zones.h"
#include <algorithm>
namespace ink {
static void label(Canvas &c, int x, int y, int width, const char *text, bool black = true) {
  c.fit(x, y, width, text, 14, black);
}
static int titleRows(Canvas &c, int x, int baseline, int width, int bottom, const char *text) {
  int rows = std::max(0, (bottom - 3 - baseline) / 14 + 1);
  return baseline + 14 * c.wrapped(x, baseline, width, text, 14, rows, 14);
}
static void eventBlock(Canvas &c, const CalendarEvent &e, const weekgrid::TimedBlock &b,
                       int day, time_t now, bool today) {
  using namespace weekgrid;
  if (b.lane >= MaxLanes) return;
  int x0 = columnX(day), dayWidth = columnX(day + 1) - x0;
  int x = x0 + 3 + (dayWidth - 4) * b.lane / b.lanes;
  int right = x0 + 3 + (dayWidth - 4) * (b.lane + 1) / b.lanes - 2;
  int w = right - x, h = b.bottom - b.top;
  bool active = today && e.start <= now && now < e.end;
  c.rect(x, b.top, w, h, false); c.box(x, b.top, w, h);
  c.rect(x, b.top, active ? 3 : 2, h);
  int y = b.top + 14;
  if (h >= 48) {
    char time[24];
    if (w >= 64) snprintf(time, sizeof time, "%02d:%02d", b.start / 60, b.start % 60);
    else snprintf(time, sizeof time, "%02dh", b.start / 60);
    if (active) c.rect(x + 1, b.top + 1, w - 2, 17);
    label(c, x + 5, y, w - 9, time, !active);
    y += 14;
  }
  y = titleRows(c, x + 5, y, w - 9, b.bottom, e.title);
  if (y < b.bottom - 2 && e.location[0]) label(c, x + 5, y, w - 9, e.location);
}
void renderWeek(Canvas &c, const Model &m, time_t now) {
  using namespace weekgrid;
  auto local = inZone(now, m.settings.localZone);
  char b[160];
  strlcpy(b, "CALENDAR / NEXT 7 DAYS", sizeof b);
  renderChrome(c, b, Page::Week, m);
  if (!Clock::valid()) { c.text(24, 100, "Waiting for network time", 18); return; }
  time_t wall = agendaFirstDay(now, m.settings.localZone);
  auto first = Clock::utc(wall), last = Clock::utc(wall + 6 * 86400);
  snprintf(b, sizeof b, "%02d.%02d.%04d - %02d.%02d.%04d / %s", first.tm_mday, first.tm_mon + 1,
           first.tm_year + 1900, last.tm_mday, last.tm_mon + 1, last.tm_year + 1900, m.settings.localZone);
  c.fit(12, 70, 588, b, 14);
  if (!m.agenda.configured) c.fit(616, 70, 160, "NOT CONNECTED", 14);
  else if (m.agenda.stale) c.text(648, 70, "CACHED", 14);
  const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  const auto range = visibleRange(m.agenda, wall, m.settings.localZone, now);
  AllDayPlan allDay; planAllDay(m.agenda, wall, m.settings.localZone, allDay);
  c.text(12, AllDayTop + 14, "ALL", 14);
  c.line(12, 84, Right, 84); c.line(12, 126, Right, 126);
  c.line(12, Top - 5, Right, Top - 5);
  for (int d = 0; d <= 7; d++) c.line(columnX(d), 84, columnX(d), Bottom);
  int hours = (range.last - range.first) / 60, tick = hours > 16 ? 2 : 1;
  auto hourTick = [&](int minute) {
    int y = minuteY(minute, range);
    snprintf(b, sizeof b, "%02d:00", minute / 60);
    c.text(12, y + 5, b, 14); c.dottedLine(Left, y, Right - Left);
  };
  for (int minute = range.first; minute <= range.last; minute += tick * 60) hourTick(minute);
  if ((range.last - range.first) % (tick * 60)) hourTick(range.last);
  c.line(Left, Bottom, Right, Bottom);
  int hiddenTotal = 0;
  for (int d = 0; d < 7; d++) {
    auto date = Clock::utc(wall + d * 86400); bool today = d == 0;
    int x = columnX(d), width = columnX(d + 1) - x;
    if (today) c.rect(x + 1, 85, width - 1, 40);
    c.text(x + 8, 101, days[date.tm_wday], 14, !today);
    snprintf(b, sizeof b, "%02d.%02d", date.tm_mday, date.tm_mon + 1);
    c.text(x + 8, 119, b, 16, !today);
    DayPlan plan; planDay(m.agenda, wall + d * 86400, m.settings.localZone, range, plan);
    int overflow = plan.hidden + allDay.hidden[d];
    hiddenTotal += overflow;
    if (overflow) {
      snprintf(b, sizeof b, "+%d", overflow);
      // Header badge reports omitted blocks without covering event titles.
      c.text(x + width - c.textWidth(b, 14) - 5, 101, b, 14, !today);
    }
    for (int i = 0; i < plan.count; i++) eventBlock(c, m.agenda.events[plan.blocks[i].event], plan.blocks[i], d, now, today);
  }
  for (int i = 0; i < allDay.count; i++) {
    const auto &a = allDay.blocks[i]; if (a.lane >= AllDayRows) continue;
    int x = columnX(a.first) + 3, width = columnX(a.last) - x - 3, y = AllDayTop + a.lane * AllDayRowHeight;
    c.rect(x, y, width, 17, false); c.box(x, y, width, 17);
    label(c, x + 4, y + 13, width - 8, m.agenda.events[a.event].title);
  }
  int day = 0, minute = local.tm_hour * 60 + local.tm_min;
  if (minute >= range.first && minute <= range.last) {
    int x = columnX(day), right = columnX(day + 1), y = minuteY(minute, range);
    c.line(x + 1, y, right - 1, y);
    for (int n = 0; n < 5; n++) c.line(x - 4 + n, y - 4 + n, x - 4 + n, y + 4 - n);
  }
  if (m.agenda.error[0]) c.fit(68, 469, Right - 68, m.agenda.error, 14);
  else if (m.agenda.truncated) c.text(68, 469, "96 retained / +N more events", 14);
  else if (!m.agenda.configured) c.text(68, 469, "Connect calendars in Studio", 14);
  else if (hiddenTotal) c.text(68, 469, "+N more events", 14);
}
}
