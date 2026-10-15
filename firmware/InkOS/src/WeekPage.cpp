#include "Canvas.h"
#include "Zones.h"
namespace ink {
void renderWeek(Canvas &c, const Model &m, time_t now) {
  renderChrome(c, "CALENDAR / WEEK AGENDA", Page::Week, m);
  const char *week[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
  auto local = inZone(now, m.settings.localZone);
  time_t wall = Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) -
                ((local.tm_wday + 6) % 7) * 86400;
  auto first = Clock::utc(wall), last = Clock::utc(wall + 6 * 86400);
  char b[160];
  snprintf(b, sizeof b, "%02d.%02d - %02d.%02d / %s", first.tm_mday, first.tm_mon + 1, last.tm_mday,
           last.tm_mon + 1, m.settings.localZone);
  c.fit(24, 76, 472, b, 14);
  if (!m.agenda.configured)
    c.text(520, 76, "Connect a calendar in Studio", 14);
  else if (m.agenda.stale)
    c.text(618, 76, "CACHED / RETRY", 14);
  c.text(24, 100, "DAY", 14);
  c.text(128, 100, "TIME", 14);
  c.text(248, 100, "EVENT", 14);
  c.text(584, 100, "PLACE", 14);
  c.line(24, 108, 775, 108);
  c.line(112, 116, 112, 432);
  for (int d = 0; d < 7; d++) {
    int y = 112 + d * 46;
    auto date = Clock::utc(wall + d * 86400);
    bool today = date.tm_yday == local.tm_yday && date.tm_year == local.tm_year;
    if (today)
      c.rect(24, y + 4, 72, 38);
    c.text(32, y + 19, week[d], 14, !today);
    snprintf(b, sizeof b, "%02d.%02d", date.tm_mday, date.tm_mon + 1);
    c.text(32, y + 37, b, 16, !today);
    time_t start = fromZone(wall + d * 86400, m.settings.localZone),
           end = fromZone(wall + (d + 1) * 86400, m.settings.localZone);
    const CalendarEvent *visible[2] = {};
    int count = 0;
    for (int i = 0; i < m.agenda.count; i++) {
      const auto &e = m.agenda.events[i];
      if (e.start >= end || e.end <= start)
        continue;
      if (count < 2)
        visible[count] = &e;
      count++;
    }
    if (!count)
      c.text(248, y + 27, "No events", 16);
    for (int j = 0; j < 2 && visible[j]; j++) {
      const auto &e = *visible[j];
      int baseline = y + 19 + j * 18;
      auto a = inZone(e.start, m.settings.localZone), z = inZone(e.end, m.settings.localZone);
      if (e.allDay)
        strlcpy(b, "ALL DAY", sizeof b);
      else if (e.start < start && e.end >= end)
        strlcpy(b, "CONTINUES", sizeof b);
      else if (e.start < start)
        snprintf(b, sizeof b, "TO %02d:%02d", z.tm_hour, z.tm_min);
      else if (e.end > end)
        snprintf(b, sizeof b, "%02d:%02d +", a.tm_hour, a.tm_min);
      else
        snprintf(b, sizeof b, "%02d:%02d-%02d:%02d", a.tm_hour, a.tm_min, z.tm_hour, z.tm_min);
      c.fit(128, baseline, 104, b, 14);
      c.fit(248, baseline, 312, e.title, 16);
      c.fit(584, baseline, 144, e.location, 14);
    }
    // Dedicated overflow column never paints over either event or its location.
    if (count > 2) {
      snprintf(b, sizeof b, "+%d", count - 2);
      c.fit(744, y + 27, 32, b, 14);
    }
    if (d < 6)
      c.line(128, y + 45, 775, y + 45);
  }
  if (m.agenda.truncated)
    c.text(24, 442, "96 nearest events retained / additional events omitted", 14);
  else if (m.agenda.error[0])
    c.fit(24, 442, 752, m.agenda.error, 14);
}
} // namespace ink
