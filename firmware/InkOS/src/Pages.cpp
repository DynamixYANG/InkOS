#include "Canvas.h"
#include "Layout.h"
#include "CalendarLayout.h"
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
  c.fit(layout::Margin, layout::HeaderBaseline, layout::HeaderTitleWidth, title, 20);
  renderDeviceStatus(c, m);
  c.line(layout::Margin, layout::HeaderRule, layout::Right, layout::HeaderRule);
}
void renderNavigationTitle(Canvas &c, Page page, const Settings &settings, time_t now) {
  const char *titles[] = {"TIME / CALENDAR", "NOW PLAYING / SPOTIFY", "", "",
                          "LUNAR CALENDAR", "SUNRISE / SUNSET", "ENERGY / FINLAND"};
  char title[96];
  if (page == Page::Forecast)
    snprintf(title, sizeof title, "WEATHER / %s", settings.location);
  else if (page == Page::Week)
    snprintf(title, sizeof title, "CALENDAR / NEXT 7 DAYS");
  else
    strlcpy(title, titles[int(page)], sizeof title);
  // Keep the current body, price, page number and footer untouched until commit.
  c.rect(layout::Margin, 4, layout::HeaderTitleWidth, 31, false);
  c.fit(layout::Margin, layout::HeaderBaseline, layout::HeaderTitleWidth, title, 20);
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
  constexpr int rail = 32;
  int cw = (w - rail) / 7, ch = (h - 46) / rows;
  c.text(x + (rail - 4 - c.textWidth("WK", 18))/2, y + 39, "WK", 18);
  time_t firstRow = Clock::epoch(year, month, 1) - offset * 86400;
  auto current = isoWeek(date);
  for (int row = 0; row < rows; row++) {
    auto wk = isoWeek(Clock::utc(firstRow + row * 7 * 86400));
    int yy = y + 47 + row * ch;
    snprintf(b, sizeof b, "%02d", wk.number);
    if (wk.year == current.year && wk.number == current.number)
      c.line(x + 6, yy + 21, x + rail - 10, yy + 21);
    int weekX = x + (rail - 4 - c.textWidth(b, 18))/2;
    c.text(weekX, yy + 17, b, 18); c.text(weekX + 1, yy + 17, b, 18);
  }
  for (int i = 0; i < 7; i++)
    c.centered(x + rail + i * cw, y + 39, cw, d[i], 14);
  for (int day = 1; day <= Clock::monthDays(year, month); day++) {
    int k = offset + day - 1, xx = x + rail + (k % 7) * cw, yy = y + 47 + (k / 7) * ch;
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
  auto fi = inZone(now, m.settings.localZone);
  char b[160];
  c.fit(12, 66, 280, m.settings.location, 16);
  snprintf(b, sizeof b, "%02d", fi.tm_hour);
  c.text(6, 166, Clock::valid() ? b : "--", 96);
  int advance = font(96).advance;
  snprintf(b, sizeof b, "%02d", fi.tm_min);
  c.text(6 + 3 * advance, 166, Clock::valid() ? b : "--", 96);
  if (!m.settings.animations || !Clock::valid() || !(now & 1)) {
    c.circle(6 + 2 * advance + advance / 2, 119, 3, 3);
    c.circle(6 + 2 * advance + advance / 2, 143, 3, 3);
  }
  snprintf(b, sizeof b, "%s  %02d.%02d.%04d", WEEK[fi.tm_wday], fi.tm_mday, fi.tm_mon + 1, fi.tm_year + 1900);
  c.text(12, 216, Clock::valid() ? b : "Waiting for network time", 18);
  for (int i = 0; i < 2; i++) {
    int y = 90 + i * 86;
    auto local = inZone(now, m.settings.worlds[i].zone);
    c.fit(324, y, 112, m.settings.worlds[i].label, 16);
    snprintf(b, sizeof b, "%02d:%02d", local.tm_hour, local.tm_min);
    c.text(324, y + 34, Clock::valid() ? b : "--:--", 28);
  }
  c.line(layout::HomeDivider, 54, layout::HomeDivider, layout::HomeBottom);
  if (Clock::valid()) calendar(c, layout::HomeRight, 54, 312, 274, fi);
  else c.text(layout::HomeRight, 125, "Waiting for clock sync", 18);
  c.line(layout::HomeRight, 336, layout::Right, 336);
  const auto *event = nextEvent(m.agenda, now);
  c.text(layout::HomeRight, 356, event && event->start <= now && !event->allDay ? "NOW"
                           : m.agenda.stale ? "UP NEXT / CACHED" : "UP NEXT", 14);
  if (event) {
    auto t = inZone(event->start, m.settings.localZone);
    if (event->allDay) snprintf(b, sizeof b, "%02d.%02d / ALL DAY", t.tm_mday, t.tm_mon + 1);
    else snprintf(b, sizeof b, "%02d.%02d  %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
    c.text(layout::HomeRight, 380, b, 16);
    c.fit(layout::HomeRight, 404, 312, event->title, 18);
  } else c.fit(layout::HomeRight, 380, 312, m.agenda.configured ? "No upcoming events" : "Set up calendars in Studio", 16);
  c.line(layout::HomeRight, 420, layout::Right, 420);
  Holiday list[48]; int n = holidays(fi.tm_year + 1900, list, 48);
  auto next = nextHoliday(list, n, fi);
  c.text(layout::HomeRight, 440, "FINNISH HOLIDAY / FLAG DAY", 14);
  if (next) snprintf(b, sizeof b, "%02d.%02d %s", next->day, next->month, next->name);
  else strlcpy(b, "01.01 New Year's Day", sizeof b);
  c.fit(layout::HomeRight, 464, 312, b, 16);
  c.line(12, layout::HomeLowerRule, layout::HomeLeftRight, layout::HomeLowerRule);
  if (m.weather.valid) {
    const auto &w = m.weather;
    number(b, sizeof b, w.temp, "°C", 1);
    bool test=m.iconTest>=0&&esp_timer_get_time()<m.iconTestUntilUs;
    int code=test?m.iconTest:w.code;bool daylight=test?true:w.daylight;
    if (m.settings.animations||test) c.animatedIcon(12, 274, 64, code, daylight, phase);
    else c.icon(12, 274, 64, code, daylight);
    c.text(84, 314, b, 28);
    c.fit(12, 352, 192, condition(w.code), 18);
    char feels[24], wind[24]; number(feels, sizeof feels, w.feels, " C"); number(wind, sizeof wind, w.wind, " m/s");
    snprintf(b, sizeof b, "Feels %s", feels); c.metricIcon(12, 378, 16, WeatherMetric::Temperature); c.text(36, 392, b, 16);
    snprintf(b, sizeof b, "Wind %s", wind); c.metricIcon(12, 414, 16, WeatherMetric::Wind); c.text(36, 428, b, 16);
  } else {
    c.fit(12, 312, 192, m.settings.location, 18);
    c.text(12, 352, "Fetching weather...", 14);
    if (m.weather.error[0]) c.fit(12, 392, 192, m.weather.error, 14);
  }
  renderHomeAstronomy(c, m.settings, now);
}
static int wrap(Canvas &c, int x, int baseline, int width, const char *text, int size,
                int maxLines) {
  return c.wrapped(x, baseline, width, text, size, maxLines, size + 8);
}
void renderMusic(Canvas &c, const Model &m, time_t now, int64_t us) {
  renderChrome(c, "NOW PLAYING / SPOTIFY", Page::Music, m);
  const auto &s = m.music;
  char b[128];
  if (s.valid && s.artValid)
    c.image(12, 66, ART, ART, s.art);
  else {
    c.box(12, 66, ART, ART);
    c.circle(84, 138, 48, 2);
    c.circle(84, 138, 10, 2);
  }
  c.text(180, 90,
         !s.authorized ? "ACCOUNT NOT CONNECTED"
         : s.stale     ? "PLAYBACK DATA STALE"
         : !s.valid    ? "NO ACTIVE PLAYBACK"
         : s.playing   ? "PLAYING"
                       : "PAUSED",
         14);
  wrap(c, 180, 130, 608,
       s.valid        ? s.title
       : s.authorized ? "Nothing playing yet"
                      : "Connect your Spotify account",
       24, 2);
  c.fit(180, 192, 608,
        s.valid        ? s.artist
        : s.authorized ? "Start music on your phone or speaker."
                       : "One-time browser authorization",
        18);
  if (s.valid) {
    uint32_t pos = playbackPosition(s, us);
    snprintf(b, sizeof b, "%u:%02u / %u:%02u", pos / 60000, pos / 1000 % 60, s.duration / 60000,
             s.duration / 1000 % 60);
    c.text(180, 217, b, 14);
  }
  c.line(12, 238, 787, 238);
  c.text(12, 263, "LYRICS", 14);
  int idx = currentLyric(s, us);
  if (s.valid && s.lyricStatus == LyricStatus::Timed && s.lineCount) {
    if (idx > 0)
      c.fit(12, 302, 776, s.lines[idx - 1].text, 18);
    if (idx >= 0)
      wrap(c, 12, 354, 776, s.lines[idx].text, 28, 2);
    else
      c.text(12, 354, "Instrumental intro", 28);
    if (idx + 1 < s.lineCount)
      c.fit(12, 450, 776, s.lines[idx + 1].text, 18);
  } else {
    const char *msg = !s.authorized                                ? "Waiting for authorization"
                      : s.lyricStatus == LyricStatus::Instrumental ? "Instrumental"
                      : s.lyricStatus == LyricStatus::Untimed      ? "Only untimed lyrics available"
                      : s.lyricStatus == LyricStatus::Missing      ? "No matching timed lyrics"
                      : s.lyricStatus == LyricStatus::Unavailable  ? "Lyrics service unavailable"
                                                                   : "Waiting for a track";
    wrap(c, 12, 330, 776, msg, 24, 2);
    c.fit(12, 391, 776,
          s.error[0] ? s.error : "",
          14);
  }
}
void renderForecast(Canvas &c, const Model &m, time_t now) {
  char heading[80];
  snprintf(heading, sizeof heading, "WEATHER / %s", m.settings.location);
  renderChrome(c, heading, Page::Forecast, m);
  const auto &w = m.weather;
  char b[128];
  if (!w.valid) {
    c.text(12, 124, "Fetching forecast...", 32);
    c.fit(12, 174, 776, w.error, 18);
    return;
  }
  constexpr int left = 50, right = 788, top = 108, bottom = 274;
  time_t first = w.days[0].epoch, end = w.days[6].epoch + 86400;
  float lo = 94, hi = -100, rainMax = 1;
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
  c.metricIcon(12, 56, 16, WeatherMetric::Temperature);
  c.text(36, 70, "TEMPERATURE / °C", 14);
  snprintf(b, sizeof b, "HOURLY RAIN / 0-%.1f mm%s", rainMax,
           now - w.fetched > 7200 ? " / CACHED" : "");
  c.metricIcon(right - c.textWidth(b, 14) - 22, 56, 16, WeatherMetric::Rain);
  c.text(right - c.textWidth(b, 14), 70, b, 14);
  for (int v = int(ceilf(lo / 5)) * 5; v <= hi; v += 5) {
    int y = yy(v);
    snprintf(b, sizeof b, "%d", v);
    c.text(std::max(layout::Margin, 50 - c.textWidth(b, 14)), y + 5, b, 14);
    c.dottedLine(left, y, right - left);
  }
  for (int d = 0; d < 7; d++) {
    int x = left + (right-left)*d/7, dayWidth = (right-left)*(d+1)/7-(right-left)*d/7;
    auto local = inZone(w.days[d].epoch + 3600, m.settings.localZone);
    snprintf(b, sizeof b, "%s %02d", WEEK[local.tm_wday], local.tm_mday);
    c.centered(x, 94, dayWidth, b, 16);
    if (d)
      c.line(x, top, x, 466);
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
      c.rect(x, 310 - h, std::min(3, right - x), h);
    }
    if (i && w.hours[i - 1].epoch >= first && isfinite(w.hours[i - 1].temp) && isfinite(hour.temp))
      c.line(xx(w.hours[i - 1].epoch), yy(w.hours[i - 1].temp), x, yy(hour.temp), 2);
  }
  c.line(left, 310, right - 1, 310);
  for (int d = 0; d < 7; d++) {
    int x = left + (right-left)*d/7, dayWidth = (right-left)*(d+1)/7-(right-left)*d/7;
    const auto &q = w.days[d];
    c.icon(x + (dayWidth - 40) / 2, 334, 40, q.code);
    if (isfinite(q.high) && isfinite(q.low))
      snprintf(b, sizeof b, "%d°/%d°", int(lroundf(q.high)), int(lroundf(q.low)));
    else
      strlcpy(b, "-- / --", sizeof b);
    c.centered(x, 404, dayWidth, b, 20);
    char probability[16], rain[24];
    if (q.probability >= 0)
      snprintf(probability, sizeof probability, "%d%%", q.probability);
    else
      strlcpy(probability, "--%", sizeof probability);
    number(rain, sizeof rain, q.rain, "mm", 1);
    snprintf(b, sizeof b, "%s %s", probability, rain);
    int rainWidth=22+c.textWidth(b,14),rainX=x+(dayWidth-rainWidth)/2;
    c.metricIcon(rainX,418,16,WeatherMetric::Rain);c.text(rainX+22,432,b,14);
    number(b, sizeof b, q.wind, " m/s");
    int windWidth=22+c.textWidth(b,14),windX=x+(dayWidth-windWidth)/2;
    c.metricIcon(windX,446,16,WeatherMetric::Wind);c.text(windX+22,460,b,14);
  }
}
} // namespace ink
