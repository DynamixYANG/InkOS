#include "Domain.h"
#include <algorithm>
namespace ink {
Model *model = nullptr;
SemaphoreHandle_t modelMutex = nullptr;
QueueHandle_t displayEvents = nullptr;
void invalidate() {
  uint8_t e = 1;
  if (displayEvents)
    xQueueOverwrite(displayEvents, &e);
}
bool Clock::valid() {
  return time(nullptr) > 1700000000;
}
tm Clock::utc(time_t t) {
  tm r;
  gmtime_r(&t, &r);
  return r;
}
time_t Clock::epoch(int y, int m, int d, int h, int minute) {
  tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_hour = h;
  t.tm_min = minute;
  return mktime(&t);
}
int Clock::monthDays(int y, int m) {
  const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return days[m - 1] + (m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
}
tm Clock::finland(time_t t) {
  int y = utc(t).tm_year + 1900;
  int march = 31 - utc(epoch(y, 3, 31)).tm_wday;
  int october = 31 - utc(epoch(y, 10, 31)).tm_wday;
  bool dst = t >= epoch(y, 3, march, 1) && t < epoch(y, 10, october, 1);
  return utc(t + (dst ? 10800 : 7200));
}
tm Clock::beijing(time_t t) {
  return utc(t + 28800);
}
tm Clock::london(time_t t) {
  int y = utc(t).tm_year + 1900;
  int march = 31 - utc(epoch(y, 3, 31)).tm_wday;
  int october = 31 - utc(epoch(y, 10, 31)).tm_wday;
  return utc(t + (t >= epoch(y, 3, march, 1) && t < epoch(y, 10, october, 1) ? 3600 : 0));
}
time_t Clock::fromFinland(time_t wall) {
  time_t candidate = wall - 7200;
  tm local = finland(candidate);
  time_t converted =
      epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min) +
      local.tm_sec;
  return candidate + wall - converted;
}
time_t localMidnight(time_t now) {
  tm local = Clock::finland(now);
  return Clock::fromFinland(Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday));
}
time_t weekMonday(time_t now) {
  tm local = Clock::finland(now);
  time_t wall = Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  return Clock::fromFinland(wall - ((local.tm_wday + 6) % 7) * 86400);
}
Page pageAfter(Page p, int delta) {
  int result = (int(p) + delta) % PAGE_COUNT;
  return Page(result < 0 ? result + PAGE_COUNT : result);
}
const CalendarEvent *nextEvent(const Agenda &a, time_t now) {
  const CalendarEvent *future = nullptr, *ongoing = nullptr, *allDay = nullptr;
  for (int i = 0; i < a.count; i++) {
    const auto &e = a.events[i];
    if (e.end <= now)
      continue;
    if (e.start <= now) {
      if (e.allDay)
        allDay = &e;
      else if (!ongoing || e.start < ongoing->start)
        ongoing = &e;
    } else if (!future || e.start < future->start)
      future = &e;
  }
  return ongoing ? ongoing : future ? future : allDay;
}
static int weekdayAfter(int y, int m, int d, int weekday) {
  int wd = Clock::utc(Clock::epoch(y, m, d)).tm_wday;
  return d + (weekday - wd + 7) % 7;
}
int holidays(int y, Holiday *out, int cap) {
  int n = 0;
  auto add = [&](int m, int d, const char *name, bool pub = true, bool flag = false) {
    if (n < cap)
      out[n++] = {m, d, name, pub, flag};
  };
  add(1, 1, "New Year's Day");
  add(1, 6, "Epiphany");
  add(5, 1, "May Day", true, true);
  add(12, 6, "Independence Day", true, true);
  add(12, 25, "Christmas Day");
  add(12, 26, "Boxing Day");
  int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25,
      g = (b - f + 1) / 3, h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4,
      l = (32 + 2 * e + 2 * i - h - k) % 7, m = (a + 11 * h + 22 * l) / 451,
      v = h + l - 7 * m + 114;
  time_t easter = Clock::epoch(y, v / 31, v % 31 + 1);
  const int shifts[] = {-2, 0, 1, 39, 49};
  const char *names[] = {"Good Friday", "Easter Sunday", "Easter Monday", "Ascension Day",
                         "Whit Sunday"};
  for (int j = 0; j < 5; j++) {
    tm t = Clock::utc(easter + shifts[j] * 86400);
    add(t.tm_mon + 1, t.tm_mday, names[j]);
  }
  add(6, weekdayAfter(y, 6, 20, 6), "Midsummer Day", true, true);
  tm saints = Clock::utc(Clock::epoch(y, 10, weekdayAfter(y, 10, 31, 6)));
  add(saints.tm_mon + 1, saints.tm_mday, "All Saints' Day");
  const Holiday flags[] = {{2, 5, "Runeberg Day", false, true},
                           {2, 28, "Kalevala Day", false, true},
                           {3, 19, "Equality Day", false, true},
                           {4, 9, "Finnish Language Day", false, true},
                           {4, 27, "War Veterans Day", false, true},
                           {5, 9, "Europe Day", false, true},
                           {5, 12, "Finnish Heritage Day", false, true},
                           {6, 4, "Defence Forces Day", false, true},
                           {7, 6, "Poetry and Summer Day", false, true},
                           {10, 1, "Civic Participation Day", false, true},
                           {10, 10, "Finnish Literature Day", false, true},
                           {10, 24, "United Nations Day", false, true},
                           {11, 6, "Swedish Day", false, true},
                           {11, 20, "Children's Day", false, true},
                           {12, 8, "Finnish Music Day", false, true}};
  for (auto &q : flags)
    add(q.month, q.day, q.name, false, true);
  add(5, weekdayAfter(y, 5, 8, 0), "Mother's Day", false, true);
  add(5, weekdayAfter(y, 5, 15, 0), "Remembrance Day", false, true);
  add(11, weekdayAfter(y, 11, 8, 0), "Father's Day", false, true);
  tm nature = Clock::utc(Clock::epoch(y, 9, weekdayAfter(y, 9, 1, 6)) - 7 * 86400);
  add(nature.tm_mon + 1, nature.tm_mday, "Finnish Nature Day", false, true);
  std::sort(out, out + n, [](const Holiday &a, const Holiday &b) {
    return a.month * 32 + a.day < b.month * 32 + b.day;
  });
  return n;
}
uint32_t playbackPosition(const Music &m, int64_t us) {
  uint64_t p = m.position;
  if (m.playing && !m.stale && us > m.sampleUs)
    p += (us - m.sampleUs) / 1000;
  return std::min<uint64_t>(p, m.duration);
}
int currentLyric(const Music &m, int64_t us, int offset) {
  int64_t p = int64_t(playbackPosition(m, us)) + offset;
  int r = -1;
  for (int i = 0; i < m.lineCount && m.lines[i].atMs <= p; i++)
    r = i;
  return r;
}
const char *condition(int c) {
  if (c < 0)
    return "Unavailable";
  if (c == 0)
    return "Clear";
  if (c <= 2)
    return "Partly cloudy";
  if (c == 3)
    return "Overcast";
  if (c == 45 || c == 48)
    return "Fog";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86)
    return "Snow";
  if (c >= 95)
    return "Thunderstorm";
  if (c >= 51)
    return "Rain";
  return "Unknown";
}
} // namespace ink
