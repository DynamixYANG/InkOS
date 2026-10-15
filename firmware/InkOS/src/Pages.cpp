#include "Canvas.h"
#include "Layout.h"
#include "Zones.h"
#include <algorithm>
#include <esp_timer.h>
namespace ink {
static const char *WEEK[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *MONTH[] = {"JANUARY",   "FEBRUARY", "MARCH",    "APRIL",
                              "MAY",       "JUNE",     "JULY",     "AUGUST",
                              "SEPTEMBER", "OCTOBER",  "NOVEMBER", "DECEMBER"};
static void number(char *b, size_t n, float v, const char *suffix = "", int decimals = 0) {
  if (isnan(v))
    snprintf(b, n, "--%s", suffix);
  else
    snprintf(b, n, "%.*f%s", decimals, v, suffix);
}
void renderChrome(Canvas &c, const char *title, Page page, const Model &m) {
  c.clear();
  c.text(layout::Margin, layout::HeaderBaseline, title, 20);
  char q[64];
  snprintf(q, sizeof q, "INKOS / 0%d", int(page) + 1);
  c.text(650, layout::HeaderBaseline, q, 14);
  c.line(layout::Margin, layout::HeaderRule, layout::Right, layout::HeaderRule);
  c.line(layout::Margin, layout::FooterRule, layout::Right, layout::FooterRule);
  c.text(24, 469, "KEY1 < PREV   KEY2 NEXT >   KEY3 HOME", 14);
  const char *names[] = {"HOME", "MUSIC", "WEATHER", "WEEK", "MOON", "SUN", "ENERGY"};
  snprintf(q, sizeof q, "0%d / 07  %s", int(page) + 1, names[int(page)]);
  c.text(410, 469, q, 14);
  c.text(665, 469, m.wifi ? "ONLINE" : "OFFLINE", 14);
}
static void calendar(Canvas &c, int x, int y, int w, int h, const tm &date) {
  int year = date.tm_year + 1900, month = date.tm_mon + 1;
  Holiday days[48];
  int n = holidays(year, days, 48);
  char b[48];
  snprintf(b, sizeof b, "%s %d", MONTH[date.tm_mon], year);
  c.text(x, y + 17, b, 16);
  const char *d[] = {"M", "T", "W", "T", "F", "S", "S"};
  int offset = (Clock::utc(Clock::epoch(year, month, 1)).tm_wday + 6) % 7;
  int rows = (offset + Clock::monthDays(year, month) + 6) / 7;
  int cw = w / 7, ch = (h - 46) / rows;
  for (int i = 0; i < 7; i++)
    c.centered(x + i * cw, y + 39, cw, d[i], 14);
  for (int day = 1; day <= Clock::monthDays(year, month); day++) {
    int k = offset + day - 1, xx = x + (k % 7) * cw, yy = y + 47 + (k / 7) * ch;
    bool pub = false, flag = false;
    for (int i = 0; i < n; i++)
      if (days[i].month == month && days[i].day == day) {
        pub = days[i].publicHoliday;
        flag = days[i].flag;
      }
    if (day == date.tm_mday)
      c.rect(xx + cw / 2 - 14, yy - 2, 29, 26);
    else if (pub)
      c.circle(xx + cw / 2, yy + 10, 14);
    snprintf(b, sizeof b, "%d", day);
    c.text(xx + (cw - c.textWidth(b, 18)) / 2, yy + 17, b, 18, day != date.tm_mday);
    if (flag) {
      int fx = xx + cw - 8;
      c.line(fx, yy - 1, fx, yy + 8);
      c.line(fx, yy - 1, fx + 5, yy + 2);
      c.line(fx + 5, yy + 2, fx, yy + 3);
    }
  }
}
static const Holiday *nextHoliday(Holiday *list, int count, const tm &t) {
  for (int i = 0; i < count; i++)
    if (list[i].month * 32 + list[i].day >= (t.tm_mon + 1) * 32 + t.tm_mday)
      return list + i;
  return nullptr;
}
void renderHome(Canvas &c, const Model &m, time_t now, unsigned phase) {
  renderChrome(c, "TIME / CALENDAR", Page::Home, m);
  renderCurrentPrice(c, m.energy, now);
  tm fi = inZone(now, m.settings.localZone);
  char b[128];
  c.fit(24, 80, 392, m.settings.location, 16);
  snprintf(b, sizeof b, "%02d", fi.tm_hour);
  c.text(18, 180, Clock::valid() ? b : "--", 96);
  int advance = font(96).advance;
  snprintf(b, sizeof b, "%02d", fi.tm_min);
  c.text(18 + 3 * advance, 180, Clock::valid() ? b : "--", 96);
  // Center the colon on the numeral cap height instead of the font baseline.
  if (!m.settings.animations || !Clock::valid() || !(now & 1)) {
    c.circle(18 + 2 * advance + advance / 2, 133, 3, 3);
    c.circle(18 + 2 * advance + advance / 2, 157, 3, 3);
  }
  snprintf(b, sizeof b, "%s  %02d.%02d.%04d", WEEK[fi.tm_wday], fi.tm_mday, fi.tm_mon + 1,
           fi.tm_year + 1900);
  c.text(24, 212, Clock::valid() ? b : "Waiting for network time", 18);
  c.line(24, 224, layout::HomeLeftRight, 224);
  for (int i = 0; i < 2; i++) {
    int x = 24 + i * 208;
    auto local = inZone(now, m.settings.worlds[i].zone);
    c.fit(x, 248, 184, m.settings.worlds[i].label, 16);
    snprintf(b, sizeof b, "%02d:%02d", local.tm_hour, local.tm_min);
    c.text(x, 280, Clock::valid() ? b : "--:--", 32);
    int offset = zoneOffset(now, m.settings.worlds[i].zone) / 60;
    snprintf(b, sizeof b, "UTC%+d:%02d / %02d.%02d", offset / 60, abs(offset) % 60, local.tm_mday,
             local.tm_mon + 1);
    c.text(x, 304, b, 14);
  }
  c.line(layout::HomeDivider, 64, layout::HomeDivider, layout::HomeBottom);
  if (Clock::valid())
    calendar(c, layout::HomeRight, 63, 312, 246, fi);
  else
    c.text(layout::HomeRight, 125, "Waiting for clock sync", 18);
  c.line(layout::HomeRight, layout::HomeLowerRule, layout::Right, layout::HomeLowerRule);
  const CalendarEvent *event = nextEvent(m.agenda, now);
  c.text(layout::HomeRight, 332,
         event && event->start <= now && !event->allDay ? "NOW"
         : m.agenda.stale                               ? "UP NEXT / CACHED"
                                                        : "UP NEXT",
         14);
  if (event) {
    auto t = inZone(event->start, m.settings.localZone);
    if (event->allDay)
      snprintf(b, sizeof b, "%02d.%02d / ALL DAY", t.tm_mday, t.tm_mon + 1);
    else
      snprintf(b, sizeof b, "%02d.%02d  %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
    c.text(layout::HomeRight, 356, b, 16);
    c.fit(layout::HomeRight, 380, 312, event->title, 18);
  } else
    c.fit(layout::HomeRight, 356, 312,
          m.agenda.configured ? "No upcoming events" : "Set up calendar subscriptions", 16);
  c.line(layout::HomeRight, 392, layout::Right, 392);
  Holiday list[48];
  int n = holidays(fi.tm_year + 1900, list, 48);
  auto next = nextHoliday(list, n, fi);
  c.text(layout::HomeRight, 412, "FINNISH HOLIDAY / FLAG DAY", 14);
  if (next) {
    snprintf(b, sizeof b, "%02d.%02d %s", next->day, next->month, next->name);
    c.fit(layout::HomeRight, 436, 310, b, 16);
  } else
    c.text(layout::HomeRight, 436, "01.01 New Year's Day", 16);
  c.line(24, layout::HomeLowerRule, layout::HomeLeftRight, layout::HomeLowerRule);
  if (m.weather.valid) {
    const auto &w = m.weather;
    bool test = m.iconTest >= 0 && esp_timer_get_time() < m.iconTestUntilUs;
    int code = test ? m.iconTest : w.code;
    if (m.settings.animations || test)
      c.animatedIcon(24, 336, 48, code, test ? true : w.daylight, phase);
    else
      c.icon(24, 336, 48, w.code, w.daylight);
    number(b, sizeof b, w.temp, "°C", 1);
    c.text(80, 364, b, 28);
    c.fit(24, 392, 184, test ? "ICON TEST / AUTO RESTORE" : condition(w.code), 16);
    char feels[24], wind[24];
    number(feels, sizeof feels, w.feels, " C");
    number(wind, sizeof wind, w.wind, " m/s");
    snprintf(b, sizeof b, "Feels %s", feels);
    c.text(24, 412, b, 14);
    snprintf(b, sizeof b, "Wind %s", wind);
    c.text(24, 432, b, 14);
  } else {
    c.fit(24, 356, 184, m.settings.location, 18);
    c.text(24, 388, "Fetching weather...", 14);
    if (m.weather.error[0])
      c.fit(24, 420, 184, m.weather.error, 14);
  }
  renderHomeAstronomy(c, m.settings, now);
}
static int wrap(Canvas &c, int x, int baseline, int width, const char *text, int size,
                int maxLines) {
  char row[512] = {};
  int rows = 0;
  const char *start = text;
  while (*start && rows < maxLines) {
    int count = std::max(1, width / font(size).advance), bytes = 0, chars = 0, lastSpace = -1;
    const char *p = start;
    while (*p && chars < count) {
      if (*p == ' ')
        lastSpace = p - start;
      uint8_t ch = *p;
      int len = ch < 128 ? 1 : ch < 224 ? 2 : ch < 240 ? 3 : 4;
      for (int j = 0; j < len && *p; j++)
        p++;
      chars++;
      bytes = p - start;
    }
    if (*p && lastSpace > 0)
      bytes = lastSpace;
    bytes = std::min(bytes, 510);
    memcpy(row, start, bytes);
    row[bytes] = 0;
    if (rows == maxLines - 1 && start[bytes]) {
      c.fit(x, baseline + rows * (size + 8), width, start, size);
    } else
      c.text(x, baseline + rows * (size + 8), row, size);
    start += bytes;
    while (*start == ' ')
      start++;
    rows++;
    if (!bytes)
      break;
  }
  return rows;
}
void renderMusic(Canvas &c, const Model &m, time_t now, int64_t us) {
  renderChrome(c, "NOW PLAYING / SPOTIFY", Page::Music, m);
  const auto &s = m.music;
  char b[128];
  if (s.valid && s.artValid)
    c.image(24, 70, ART, ART, s.art);
  else {
    c.box(24, 70, ART, ART);
    c.circle(96, 142, 48, 2);
    c.circle(96, 142, 10, 2);
  }
  c.text(193, 90,
         !s.authorized ? "ACCOUNT NOT CONNECTED"
         : s.stale     ? "PLAYBACK DATA STALE"
         : !s.valid    ? "NO ACTIVE PLAYBACK"
         : s.playing   ? "PLAYING"
                       : "PAUSED",
         14);
  wrap(c, 193, 130, 575,
       s.valid        ? s.title
       : s.authorized ? "Nothing playing yet"
                      : "Connect your Spotify account",
       24, 2);
  c.fit(193, 192, 575,
        s.valid        ? s.artist
        : s.authorized ? "Start music on your phone or speaker."
                       : "One-time browser authorization",
        18);
  if (s.valid) {
    uint32_t pos = playbackPosition(s, us);
    snprintf(b, sizeof b, "%u:%02u / %u:%02u", pos / 60000, pos / 1000 % 60, s.duration / 60000,
             s.duration / 1000 % 60);
    c.text(193, 217, b, 14);
  } else
    c.text(193, 217, "No computer server needed after setup", 14);
  c.line(24, 238, 775, 238);
  c.text(24, 263, "LYRICS", 14);
  int idx = currentLyric(s, us);
  if (s.valid && s.lyricStatus == LyricStatus::Timed && s.lineCount) {
    if (idx > 0)
      c.fit(24, 294, 752, s.lines[idx - 1].text, 18);
    if (idx >= 0)
      wrap(c, 24, 339, 752, s.lines[idx].text, 28, 2);
    else
      c.text(24, 339, "Instrumental intro", 28);
    if (idx + 1 < s.lineCount)
      c.fit(24, 420, 752, s.lines[idx + 1].text, 18);
  } else {
    const char *msg = !s.authorized                                ? "Waiting for authorization"
                      : s.lyricStatus == LyricStatus::Instrumental ? "Instrumental"
                      : s.lyricStatus == LyricStatus::Untimed      ? "Only untimed lyrics available"
                      : s.lyricStatus == LyricStatus::Missing      ? "No matching timed lyrics"
                      : s.lyricStatus == LyricStatus::Unavailable  ? "Lyrics service unavailable"
                                                                   : "Waiting for a track";
    wrap(c, 24, 330, 752, msg, 24, 2);
    c.fit(24, 391, 752,
          s.error[0] ? s.error : "Timed lyrics: LRCLIB / matched by track, artist and duration",
          14);
  }
}
void renderForecast(Canvas &c, const Model &m, time_t now) {
  char heading[80];
  snprintf(heading, sizeof heading, "%s / SEVEN DAY OUTLOOK", m.settings.location);
  renderChrome(c, heading, Page::Forecast, m);
  const auto &w = m.weather;
  char b[128];
  if (!w.valid) {
    c.text(24, 124, "Fetching forecast...", 32);
    c.fit(24, 174, 752, w.error, 18);
    return;
  }
  constexpr int left = 62, right = 776, top = 112, bottom = 248, dayWidth = 102;
  time_t first = w.days[0].epoch, end = w.days[6].epoch + 86400;
  float lo = 100, hi = -100, rainMax = 1;
  for (int i = 0; i < w.hourCount; i++) {
    const auto &hour = w.hours[i];
    if (hour.epoch < first || hour.epoch >= end)
      continue;
    if (isfinite(hour.temp)) {
      lo = std::min(lo, hour.temp);
      hi = std::max(hi, hour.temp);
    }
    if (isfinite(hour.rain))
      rainMax = std::max(rainMax, hour.rain);
  }
  if (hi < lo) {
    lo = -5;
    hi = 5;
  }
  lo = floorf(lo / 5) * 5 - 2;
  hi = ceilf(hi / 5) * 5 + 2;
  auto yy = [&](float t) { return bottom - int((t - lo) / (hi - lo) * (bottom - top)); };
  auto xx = [&](time_t t) {
    return left + int(double(t - first) / (end - first) * (right - left));
  };
  c.text(24, 76, "TEMPERATURE / °C", 14);
  snprintf(b, sizeof b, "HOURLY RAIN / 0-%.1f mm%s", rainMax,
           now - w.fetched > 7200 ? " / CACHED" : "");
  c.text(right - c.textWidth(b, 14), 76, b, 14);
  for (int v = int(ceilf(lo / 5)) * 5; v <= hi; v += 5) {
    int y = yy(v);
    snprintf(b, sizeof b, "%d", v);
    c.text(std::max(layout::Margin, 50 - c.textWidth(b, 14)), y + 5, b, 14);
    c.dottedLine(left, y, right - left);
  }
  for (int d = 0; d < 7; d++) {
    int x = left + d * dayWidth;
    auto local = inZone(w.days[d].epoch + 3600, m.settings.localZone);
    snprintf(b, sizeof b, "%s %02d", WEEK[local.tm_wday], local.tm_mday);
    c.centered(x, 100, dayWidth, b, 16);
    if (d)
      c.line(x, top, x, 424);
  }
  for (int i = 0; i < w.hourCount; i++) {
    const auto &hour = w.hours[i];
    int x = xx(hour.epoch);
    if (x < left || x >= right)
      continue;
    if (!hour.day)
      for (int y = top + 3; y < bottom; y += 8)
        c.dot(x, y);
    if (isfinite(hour.rain) && hour.rain > 0) {
      int h = std::min(24, int(hour.rain / rainMax * 24) + 1);
      c.rect(x, 280 - h, std::min(3, right - x), h);
    }
    if (i && w.hours[i - 1].epoch >= first && isfinite(w.hours[i - 1].temp) && isfinite(hour.temp))
      c.line(xx(w.hours[i - 1].epoch), yy(w.hours[i - 1].temp), x, yy(hour.temp), 2);
  }
  c.line(left, 280, right - 1, 280);
  for (int d = 0; d < 7; d++) {
    int x = left + d * dayWidth;
    const auto &q = w.days[d];
    c.icon(x + (dayWidth - 40) / 2, 304, 40, q.code);
    if (isfinite(q.high) && isfinite(q.low))
      snprintf(b, sizeof b, "%d°/%d°", int(lroundf(q.high)), int(lroundf(q.low)));
    else
      strlcpy(b, "-- / --", sizeof b);
    c.centered(x, 372, dayWidth, b, 20);
    char probability[16], rain[24];
    if (q.probability >= 0)
      snprintf(probability, sizeof probability, "%d%%", q.probability);
    else
      strlcpy(probability, "--%", sizeof probability);
    number(rain, sizeof rain, q.rain, "mm", 1);
    snprintf(b, sizeof b, "%s %s", probability, rain);
    c.centered(x, 396, dayWidth, b, 14);
    number(b, sizeof b, q.wind, " m/s");
    c.centered(x, 420, dayWidth, b, 14);
  }
}
} // namespace ink
