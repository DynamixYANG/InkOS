#include "Aurora.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace ink {
NightWindow nightWindow(time_t now, const SunContext &s) {
  NightWindow w;
  const time_t day = 86400;
  // An event within a day defines the state (robust right at sunset/sunrise); the altitude
  // only decides when no event is near, i.e. in polar day or polar night.
  bool near = s.next && s.next - now <= day, up = near ? !s.nextRising : s.sunUp;
  if (up) {
    if (!s.next || s.nextRising || s.next - now > day) return w; // polar day: no sunset coming
    w.start = s.next;
    bool rise = s.following && s.followingRising && s.following - s.next <= day;
    w.end = rise ? s.following : s.next + day;
    w.kind = NightKind::Evening;
    return w;
  }
  w.start = now;
  if (s.next && s.nextRising && s.next - now <= day) {
    w.end = s.next;
    w.kind = NightKind::Night;
  } else {
    w.end = now + 12 * 3600;
    w.kind = NightKind::PolarNight;
  }
  return w;
}
KpPeak peakKp(const Aurora &a, time_t start, time_t end) {
  KpPeak p;
  if (end <= start) return p;
  for (int i = 0; i < a.count; i++) {
    const auto &b = a.bins[i];
    if (b.start < end && b.start + KpBinSeconds > start && b.kp > p.kp) p = {b.kp, b.start};
  }
  return p;
}
bool kpStale(const Aurora &a, time_t now) { return !a.fetched || now - a.fetched > KpStaleSeconds; }
void kpText(char *out, size_t size, float kp) {
  if (!(kp >= 0)) { snprintf(out, size, "--"); return; }
  int thirds = int(lroundf(kp * 3)), base = (thirds + 1) / 3, r = thirds - base * 3;
  snprintf(out, size, "%d%s", base, r > 0 ? "+" : r < 0 ? "-" : "");
}

// ---- minimal JSON scanner for this one product ----
namespace {
struct Cursor {
  const char *p, *end;
  void ws() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
  bool eat(char c) { ws(); if (p < end && *p == c) { p++; return true; } return false; }
  bool peek(char c) { ws(); return p < end && *p == c; }
};
// String into buf (truncated, escapes kept verbatim except \" handling); false if not a string.
bool str(Cursor &c, char *buf, size_t n) {
  if (!c.eat('"')) return false;
  size_t k = 0;
  while (c.p < c.end && *c.p != '"') {
    if (*c.p == '\\' && c.p + 1 < c.end) c.p++;
    if (k + 1 < n) buf[k++] = *c.p;
    c.p++;
  }
  if (n) buf[k] = 0;
  return c.eat('"');
}
// Scalar as text (string contents or the raw number/null/true/false token).
bool scalar(Cursor &c, char *buf, size_t n) {
  if (c.peek('"')) return str(c, buf, n);
  size_t k = 0;
  while (c.p < c.end && !strchr(",]} \n\r\t", *c.p)) { if (k + 1 < n) buf[k++] = *c.p; c.p++; }
  if (n) buf[k] = 0;
  return k > 0;
}
bool skip(Cursor &c, int depth = 0) { // any value
  if (depth > 8) return false;
  char tmp[2];
  if (c.peek('"')) return str(c, tmp, sizeof tmp);
  if (c.eat('[') || c.eat('{')) {
    char close = c.p[-1] == '[' ? ']' : '}';
    if (c.eat(close)) return true;
    do {
      if (close == '}' && !(str(c, tmp, sizeof tmp) && c.eat(':'))) return false;
      if (!skip(c, depth + 1)) return false;
    } while (c.eat(','));
    return c.eat(close);
  }
  return scalar(c, tmp, sizeof tmp);
}
long daysFromCivil(int y, int m, int d) { // Howard Hinnant's algorithm, proleptic Gregorian
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = unsigned(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1,
           doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + long(doe) - 719468;
}
time_t utcTag(const char *s) { // "2026-10-11T00:00:00" or "2026-10-11 00:00:00.000", UTC
  int y, mo, d, h, mi, se = 0;
  char sep;
  if (sscanf(s, "%4d-%2d-%2d%c%2d:%2d:%2d", &y, &mo, &d, &sep, &h, &mi, &se) < 6) return 0;
  if ((sep != 'T' && sep != ' ') || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) return 0;
  return time_t(daysFromCivil(y, mo, d)) * 86400 + h * 3600 + mi * 60 + se;
}
bool store(Aurora &a, const char *tag, const char *kp, const char *kind) {
  time_t t = utcTag(tag);
  char *e;
  float v = strtof(kp, &e);
  if (!t || e == kp || !(v >= 0 && v <= 9)) return false;
  if (a.count >= MaxKpBins) return true; // ignore the excess, keep what fits
  KpKind k = !strcmp(kind, "observed") ? KpKind::Observed : !strcmp(kind, "estimated") ? KpKind::Estimated : KpKind::Predicted;
  a.bins[a.count++] = {t, v, k};
  return true;
}
} // namespace

int parseKpForecast(const char *json, size_t length, Aurora &out) {
  Cursor c{json, json + length};
  out.count = 0;
  if (!json || !c.eat('[')) return 0;
  int tagIndex = 0, kpIndex = 1, kindIndex = 2; // legacy layout, refined by its header row
  bool header = true;
  if (!c.eat(']')) do {
      char tag[32] = "", kp[16] = "", kind[16] = "";
      if (c.eat('{')) {
        if (!c.eat('}')) do {
            char key[24];
            if (!str(c, key, sizeof key) || !c.eat(':')) return out.count = 0;
            char *dst = !strcmp(key, "time_tag") ? tag : !strcmp(key, "kp") ? kp : !strcmp(key, "observed") ? kind : nullptr;
            size_t n = dst == tag ? sizeof tag : 16;
            if (dst ? !scalar(c, dst, n) : !skip(c)) return out.count = 0;
          } while (c.eat(','));
        if (!c.eat('}')) return out.count = 0;
        if (!store(out, tag, kp, kind)) return out.count = 0;
      } else if (c.eat('[')) {
        char cell[4][32] = {};
        int i = 0;
        if (!c.eat(']')) do {
            char v[32];
            if (!scalar(c, v, sizeof v)) return out.count = 0;
            if (i < 4) strcpy(cell[i], v);
            i++;
          } while (c.eat(','));
        if (!c.eat(']')) return out.count = 0;
        if (header) {
          header = false;
          for (int k = 0; k < 4; k++) {
            if (!strcmp(cell[k], "time_tag")) tagIndex = k;
            if (!strcmp(cell[k], "kp")) kpIndex = k;
            if (!strcmp(cell[k], "observed")) kindIndex = k;
          }
          if (!strcmp(cell[0], "time_tag")) continue;
        }
        if (!store(out, cell[tagIndex], cell[kpIndex], cell[kindIndex])) return out.count = 0;
      } else return out.count = 0;
      header = false;
    } while (c.eat(','));
  if (!c.eat(']')) return out.count = 0;
  for (int i = 1; i < out.count; i++) // insertion sort; input is already ordered
    for (int j = i; j > 0 && out.bins[j].start < out.bins[j - 1].start; j--) {
      KpBin t = out.bins[j]; out.bins[j] = out.bins[j - 1]; out.bins[j - 1] = t;
    }
  return out.count;
}
} // namespace ink
