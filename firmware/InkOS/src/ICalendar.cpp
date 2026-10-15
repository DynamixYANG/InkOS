#include "ICalendar.h"
#include "Zones.h"
#include <algorithm>
#include <esp_heap_caps.h>
#include <new>
#include <uICAL.h>
namespace ink {
namespace {
constexpr int MAX_RAW = 256, MAX_DATES = 32;
struct Stamp {
  time_t wall = 0;
  char zone[48] = {0};
  bool valid = false, date = false;
};
struct Raw {
  char uid[128] = {0}, title[128] = {0}, location[96] = {0}, rule[512] = {0};
  Stamp start, end;
  time_t recurrence = 0, exclude[MAX_DATES] = {0}, extra[MAX_DATES] = {0};
  int exCount = 0, extraCount = 0, sequence = 0;
  time_t duration = 0;
  bool cancelled = false;
};
uint32_t hash(const char *p) {
  uint32_t h = 2166136261u;
  while (*p)
    h = (h ^ uint8_t(*p++)) * 16777619u;
  return h;
}
void copyText(char *out, size_t cap, const String &input) {
  String value;
  value.reserve(input.length());
  bool escape = false;
  for (unsigned i = 0; i < input.length(); i++) {
    char c = input[i];
    if (escape) {
      value += (c == 'n' || c == 'N') ? ' ' : c;
      escape = false;
    } else if (c == '\\')
      escape = true;
    else if (c != '\r')
      value += c;
  }
  size_t n = std::min<size_t>(value.length(), cap - 1);
  while (n && ((uint8_t(value[n]) & 192) == 128))
    n--;
  memcpy(out, value.c_str(), n);
  out[n] = 0;
}
String parameter(const String &head, const char *key) {
  int at = head.indexOf(String(";") + key + "=");
  if (at < 0)
    return "";
  at += strlen(key) + 2;
  int end = head.indexOf(';', at);
  String v = head.substring(at, end < 0 ? head.length() : end);
  v.replace("\"", "");
  return v;
}
Stamp stamp(String value, String zone, const char *fallback) {
  Stamp s;
  value.trim();
  bool utc = value.endsWith("Z");
  if (utc)
    value.remove(value.length() - 1);
  if (value.length() != 8 && value.length() != 15)
    return s;
  for (unsigned i = 0; i < value.length(); i++)
    if (i == 8) {
      if (value[i] != 'T')
        return s;
    } else if (!isdigit(value[i]))
      return s;
  int y = value.substring(0, 4).toInt(), m = value.substring(4, 6).toInt(),
      d = value.substring(6, 8).toInt();
  int h = value.length() > 8 ? value.substring(9, 11).toInt() : 0,
      mi = value.length() > 8 ? value.substring(11, 13).toInt() : 0,
      se = value.length() > 8 ? value.substring(13, 15).toInt() : 0;
  if (y < 1970 || y > 2199 || m < 1 || m > 12 || d < 1 || d > Clock::monthDays(y, m) || h > 23 ||
      mi > 59 || se > 59)
    return s;
  if (utc)
    zone = "UTC";
  else if (zone.isEmpty())
    zone = fallback;
  if (!findZone(zone.c_str())) {
    const char *families[] = {"Europe/", "America/", "Asia/", "Australia/", "Pacific/"};
    for (auto prefix : families) {
      int p = zone.indexOf(prefix);
      if (p >= 0) {
        zone = zone.substring(p);
        break;
      }
    }
  }
  if (!findZone(zone.c_str()))
    return s;
  strlcpy(s.zone, zone.c_str(), sizeof s.zone);
  s.wall = Clock::epoch(y, m, d, h, mi) + se;
  s.valid = true;
  s.date = value.length() == 8;
  return s;
}
time_t absolute(const Stamp &s) {
  return fromZone(s.wall, s.zone);
}
time_t isoDuration(const String &v) {
  if (!v.startsWith("P"))
    return 0;
  int64_t total = 0, n = 0;
  bool time = false;
  for (unsigned i = 1; i < v.length(); i++) {
    char c = v[i];
    if (c == 'T') {
      time = true;
      continue;
    }
    if (isdigit(c)) {
      n = n * 10 + c - '0';
      if (n > 1000000)
        return 0;
      continue;
    }
    if (c == 'W')
      total += n * 604800;
    else if (c == 'D')
      total += n * 86400;
    else if (c == 'H' && time)
      total += n * 3600;
    else if (c == 'M' && time)
      total += n * 60;
    else if (c == 'S' && time)
      total += n;
    else
      return 0;
    n = 0;
  }
  return total > 0 && total <= 366 * 86400 ? time_t(total) : 0;
}
bool append(Agenda &a, const Raw &r, time_t start, time_t end, time_t begin, time_t until) {
  if (end <= begin || start >= until || end <= start)
    return true;
  for (int i = 0; i < a.count; i++)
    if (a.events[i].start == start && a.events[i].end == end &&
        !strcmp(a.events[i].title, r.title) && !strcmp(a.events[i].location, r.location))
      return true;
  int index = a.count;
  if (index >= MAX_EVENTS) {
    a.truncated = true;
    index = 0;
    for (int i = 1; i < a.count; i++)
      if (a.events[i].start > a.events[index].start)
        index = i;
    if (start >= a.events[index].start)
      return true;
  } else
    a.count++;
  auto &e = a.events[index];
  e.start = start;
  e.end = end;
  e.allDay = r.start.date;
  strlcpy(e.title, *r.title ? r.title : "Untitled event", sizeof e.title);
  strlcpy(e.location, r.location, sizeof e.location);
  return true;
}
String untilInWall(String rule, const Stamp &start) {
  int i = rule.indexOf("UNTIL=");
  if (i < 0)
    return rule;
  int e = rule.indexOf(';', i);
  String value = rule.substring(i + 6, e < 0 ? rule.length() : e);
  auto s = stamp(value, "", start.zone);
  if (!s.valid)
    return rule;
  time_t wall = absolute(s) + zoneOffset(absolute(s), start.zone);
  auto t = Clock::utc(wall);
  char b[32];
  snprintf(b, sizeof b, "%04d%02d%02dT%02d%02d%02dZ", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           s.date ? 23 : t.tm_hour, s.date ? 59 : t.tm_min, s.date ? 59 : t.tm_sec);
  return rule.substring(0, i + 6) + b + (e < 0 ? String("") : rule.substring(e));
}
} // namespace
bool parseCalendar(const char *ics, const char *floatingZone, time_t begin, time_t end,
                   Agenda &out) {
  if (!strstr(ics, "BEGIN:VCALENDAR") || !strstr(ics, "END:VCALENDAR")) {
    strlcpy(out.error, "Subscription did not return an ICS calendar", sizeof out.error);
    return false;
  }
  Raw *rows = (Raw *)heap_caps_calloc(MAX_RAW, sizeof(Raw), MALLOC_CAP_SPIRAM);
  Raw *current = (Raw *)heap_caps_calloc(1, sizeof(Raw), MALLOC_CAP_SPIRAM);
  if (!rows || !current) {
    free(rows);
    free(current);
    return false;
  }
  int count = 0, alarmDepth = 0;
  bool active = false, good = true;
  String logical;
  logical.reserve(2048);
  auto line = [&](const String &v) {
    if (v == "BEGIN:VEVENT") {
      memset(current, 0, sizeof(Raw));
      active = true;
      alarmDepth = 0;
      return;
    }
    if (v == "END:VEVENT") {
      active = false;
      if (current->start.valid || current->recurrence) {
        time_t finish = current->end.valid
                            ? absolute(current->end)
                            : absolute(current->start) + (current->duration     ? current->duration
                                                          : current->start.date ? 86400
                                                                                : 3600);
        if (current->recurrence || *current->rule || finish > begin) {
          if (count < MAX_RAW)
            rows[count++] = *current;
          else {
            good = false;
            strlcpy(out.error, "Calendar contains too many active series", sizeof out.error);
          }
        }
      }
      return;
    }
    if (!active)
      return;
    if (v == "BEGIN:VALARM") {
      alarmDepth++;
      return;
    }
    if (v == "END:VALARM") {
      alarmDepth = std::max(0, alarmDepth - 1);
      return;
    }
    if (alarmDepth)
      return;
    int colon = v.indexOf(':');
    if (colon < 0)
      return;
    String head = v.substring(0, colon), value = v.substring(colon + 1),
           name = head.substring(0, head.indexOf(';') < 0 ? head.length() : head.indexOf(';'));
    String tz = parameter(head, "TZID");
    if (name == "UID")
      copyText(current->uid, sizeof current->uid, value);
    else if (name == "SUMMARY")
      copyText(current->title, sizeof current->title, value);
    else if (name == "LOCATION")
      copyText(current->location, sizeof current->location, value);
    else if (name == "STATUS")
      current->cancelled = value == "CANCELLED";
    else if (name == "SEQUENCE")
      current->sequence = value.toInt();
    else if (name == "DURATION")
      current->duration = isoDuration(value);
    else if (name == "RRULE") {
      if (value.length() >= sizeof current->rule ||
          !(value.startsWith("FREQ=DAILY") || value.startsWith("FREQ=WEEKLY") ||
            value.startsWith("FREQ=MONTHLY") || value.startsWith("FREQ=YEARLY"))) {
        good = false;
        strlcpy(out.error, "Calendar recurrence type is unsupported", sizeof out.error);
      } else
        strlcpy(current->rule, value.c_str(), sizeof current->rule);
    } else if (name == "DTSTART" || name == "DTEND" || name == "RECURRENCE-ID") {
      if (name == "RECURRENCE-ID" && parameter(head, "RANGE") == "THISANDFUTURE") {
        good = false;
        strlcpy(out.error, "Future-range overrides are unsupported", sizeof out.error);
        return;
      }
      auto s = stamp(value, tz, floatingZone);
      if (!s.valid) {
        good = false;
        strlcpy(out.error, "Unsupported calendar time zone or date", sizeof out.error);
        return;
      }
      if (name == "DTSTART")
        current->start = s;
      else if (name == "DTEND")
        current->end = s;
      else
        current->recurrence = absolute(s);
    } else if (name == "EXDATE" || name == "RDATE") {
      int p = 0;
      while (p < int(value.length())) {
        int q = value.indexOf(',', p);
        auto s = stamp(value.substring(p, q < 0 ? value.length() : q), tz,
                       current->start.valid ? current->start.zone : floatingZone);
        if (!s.valid) {
          good = false;
          strlcpy(out.error, "Unsupported recurrence date", sizeof out.error);
          break;
        }
        int &n = name == "EXDATE" ? current->exCount : current->extraCount;
        time_t *dest = name == "EXDATE" ? current->exclude : current->extra;
        if (n >= MAX_DATES) {
          good = false;
          strlcpy(out.error, "Too many recurrence exceptions", sizeof out.error);
          break;
        }
        dest[n++] = absolute(s);
        if (q < 0)
          break;
        p = q + 1;
      }
    }
  };
  bool ignored = false;
  const char *p = ics;
  while (*p && good) {
    const char *q = strchr(p, '\n');
    if (!q)
      q = p + strlen(p);
    size_t n = q - p;
    if (n && p[n - 1] == '\r')
      n--;
    bool folded = *p == ' ' || *p == '\t';
    if (!folded && !logical.isEmpty()) {
      line(logical);
      logical = "";
    }
    if (!folded) {
      String prefix;
      for (size_t k = 0; k < std::min<size_t>(n, 32) && p[k] != ':' && p[k] != ';'; k++)
        prefix += p[k];
      ignored = prefix == "DESCRIPTION" || prefix == "X-ALT-DESC" || prefix == "ATTENDEE" ||
                prefix == "ORGANIZER";
    }
    if (ignored) {
      p = *q ? q + 1 : q;
      continue;
    }
    if (n > 2048 || logical.length() + n > 2048) {
      good = false;
      strlcpy(out.error, "Calendar line exceeds size limit", sizeof out.error);
      break;
    }
    if (folded) {
      p++;
      if (n)
        n--;
    }
    logical.concat(p, n);
    p = *q ? q + 1 : q;
  }
  if (good && !logical.isEmpty())
    line(logical);
  if (active) {
    good = false;
    strlcpy(out.error, "Incomplete calendar event", sizeof out.error);
  }
  if (good)
    try {
      for (int i = 0; i < count; i++) {
        auto &r = rows[i];
        if (r.cancelled || !r.start.valid)
          continue;
        bool superseded = false;
        for (int j = 0; j < count; j++)
          if (j != i && *r.uid && !strcmp(r.uid, rows[j].uid) &&
              r.recurrence == rows[j].recurrence &&
              (rows[j].sequence > r.sequence || (rows[j].sequence == r.sequence && j > i)))
            superseded = true;
        if (superseded)
          continue;
        time_t start = absolute(r.start),
               duration = r.end.valid
                              ? (r.start.date ? r.end.wall - r.start.wall : absolute(r.end) - start)
                          : r.duration   ? r.duration
                          : r.start.date ? 86400
                                         : 3600;
        if (duration <= 0 || duration > 366 * 86400)
          continue;
        auto emit = [&](time_t absoluteStart, time_t wallStart) {
          for (int n = 0; n < r.exCount; n++)
            if (r.exclude[n] == absoluteStart)
              return;
          if (!r.recurrence && *r.uid)
            for (int j = 0; j < count; j++)
              if (rows[j].recurrence == absoluteStart && !strcmp(r.uid, rows[j].uid))
                return;
          time_t finish = r.start.date ? fromZone(wallStart + duration, r.start.zone)
                                       : absoluteStart + duration;
          append(out, r, absoluteStart, finish, begin, end);
        };
        if (!*r.rule || r.recurrence)
          emit(start, r.start.wall);
        else {
          String rule = untilInWall(r.rule, r.start);
          auto rr = uICAL::new_ptr<uICAL::RRule>(uICAL::string(rule),
                                                 uICAL::DateTime((uICAL::seconds_t)r.start.wall));
          auto iter = uICAL::new_ptr<uICAL::RRuleIter>(
              rr, uICAL::DateTime((uICAL::seconds_t)(begin - duration - 86400)),
              uICAL::DateTime((uICAL::seconds_t)(end + 86400)));
          int iterations = 0;
          while (iter->next()) {
            if (++iterations > 4096) {
              good = false;
              strlcpy(out.error, "Recurrence expansion exceeds limit", sizeof out.error);
              break;
            }
            auto t = iter->now().datestamp();
            time_t wall = Clock::epoch(t.year, t.month, t.day, t.hour, t.minute) + t.second;
            emit(fromZone(wall, r.start.zone), wall);
          }
        }
        for (int n = 0; n < r.extraCount; n++)
          emit(r.extra[n], r.extra[n] + zoneOffset(r.extra[n], r.start.zone));
      }
    } catch (const uICAL::Error &) {
      good = false;
      strlcpy(out.error, "Calendar recurrence could not be expanded", sizeof out.error);
    } catch (...) {
      good = false;
      strlcpy(out.error, "Calendar processing failed", sizeof out.error);
    }
  free(rows);
  free(current);
  if (good)
    std::sort(out.events, out.events + out.count,
              [](const CalendarEvent &a, const CalendarEvent &b) {
                return a.start < b.start || (a.start == b.start && a.end < b.end);
              });
  return good;
}
} // namespace ink
