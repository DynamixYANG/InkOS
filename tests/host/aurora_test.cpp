#include "Aurora.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
using namespace ink;
static int failures = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static std::string slurp(const char *path) {
  std::string s; FILE *f = fopen(path, "rb"); if (!f) return s;
  char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s;
}
static time_t utc(int y, int mo, int d, int h, int mi = 0) { // test-side civil -> epoch
  y -= mo <= 2; long era = y / 400; unsigned yoe = y - era * 400, doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return (era * 146097 + long(yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468) * 86400L + h * 3600 + mi * 60;
}
int main() {
  static Aurora a;
  // ---- parser: captured NOAA SWPC noaa-planetary-k-index-forecast.json, 2026-10-18 ----
  std::string json = slurp(KP_SAMPLE);
  EXPECT(json.size() > 1000);
  EXPECT(parseKpForecast(json.data(), json.size(), a) == 81);
  EXPECT(a.bins[0].start == utc(2026, 10, 11, 0) && std::fabs(a.bins[0].kp - 4.00f) < 1e-4 && a.bins[0].kind == KpKind::Observed);
  EXPECT(a.bins[80].start == utc(2026, 10, 21, 0) && std::fabs(a.bins[80].kp - 2.67f) < 1e-4 && a.bins[80].kind == KpKind::Predicted);
  int est = 0; for (int i = 0; i < a.count; i++) est += a.bins[i].kind == KpKind::Estimated; EXPECT(est == 6);
  // Tonight in Oulu, 18.10 sunset 17:30 EEST to 19.10 sunrise 08:10 EEST: max 4.67 in the 00Z bin.
  auto p = peakKp(a, utc(2026, 10, 18, 14, 30), utc(2026, 10, 19, 5, 10));
  EXPECT(std::fabs(p.kp - 4.67f) < 1e-4 && p.at == utc(2026, 10, 19, 0));
  p = peakKp(a, utc(2026, 10, 19, 14, 25), utc(2026, 10, 20, 5, 15));
  EXPECT(std::fabs(p.kp - 3.67f) < 1e-4 && p.at == utc(2026, 10, 19, 21));
  // Partial overlap counts: window 22:00-01:00Z touches the 21Z (1.67) and 00Z (4.67) bins.
  p = peakKp(a, utc(2026, 10, 18, 22), utc(2026, 10, 19, 1));
  EXPECT(std::fabs(p.kp - 4.67f) < 1e-4);
  // A window ending exactly at a bin start does not include that bin.
  p = peakKp(a, utc(2026, 10, 18, 18), utc(2026, 10, 19, 0));
  EXPECT(std::fabs(p.kp - 1.67f) < 1e-4);
  EXPECT(peakKp(a, utc(2026, 11, 1, 0), utc(2026, 11, 1, 12)).kp < 0); // beyond the forecast
  EXPECT(peakKp(a, 100, 100).kp < 0);
  // Legacy array-of-arrays layout with header row; malformed inputs are rejected.
  const char *legacy = "[[\"time_tag\",\"kp\",\"observed\",\"noaa_scale\"],[\"2026-10-18 00:00:00\",\"2.33\",\"observed\",null],"
                       "[\"2026-10-18 03:00:00\",\"5.67\",\"predicted\",\"G1\"]]";
  static Aurora b;
  EXPECT(parseKpForecast(legacy, strlen(legacy), b) == 2 && b.bins[1].start == utc(2026, 10, 18, 3) && std::fabs(b.bins[1].kp - 5.67f) < 1e-4);
  for (const char *bad : {"", "{}", "[{\"time_tag\":\"2026-13-01T00:00:00\",\"kp\":1}]", "[{\"time_tag\":\"2026-10-01T00:00:00\",\"kp\":12}]",
                          "[{\"time_tag\":\"2026-10-01T00:00:00\",\"kp\":1}", "[{\"time_tag\":\"2026-10-01T00:00:00\" \"kp\":1}]"})
    EXPECT(parseKpForecast(bad, strlen(bad), b) == 0);
  EXPECT(parseKpForecast("[]", 2, b) == 0);
  // ---- NOAA thirds ----
  char t[8];
  const float in[] = {0, .33f, 1, 3.67f, 4, 4.33f, 4.67f, 8.67f, 9, -1};
  const char *want[] = {"0", "0+", "1", "4-", "4", "4+", "5-", "9-", "9", "--"};
  for (int i = 0; i < 10; i++) { kpText(t, sizeof t, in[i]); EXPECT(!strcmp(t, want[i])); }
  kpText(t, sizeof t, NAN); EXPECT(!strcmp(t, "--"));
  // ---- staleness ----
  a.fetched = 0; EXPECT(kpStale(a, utc(2026, 10, 18, 12)));
  a.fetched = utc(2026, 10, 18, 6); EXPECT(!kpStale(a, utc(2026, 10, 18, 12)) && kpStale(a, utc(2026, 10, 18, 12, 1)));
  // ---- tonight window ----
  // Day, sunset today and sunrise tomorrow: window crosses local midnight.
  SunContext s{true, utc(2026, 10, 18, 14, 30), utc(2026, 10, 19, 5, 12), false, true};
  auto w = nightWindow(utc(2026, 10, 18, 9), s);
  EXPECT(w.kind == NightKind::Evening && w.start == s.next && w.end == s.following);
  // Already night (after sunset, before midnight): now -> sunrise.
  s = {false, utc(2026, 10, 19, 5, 12), utc(2026, 10, 19, 14, 27), true, false};
  w = nightWindow(utc(2026, 10, 18, 20), s);
  EXPECT(w.kind == NightKind::Night && w.start == utc(2026, 10, 18, 20) && w.end == s.next);
  // Night after midnight.
  w = nightWindow(utc(2026, 10, 19, 2), s);
  EXPECT(w.kind == NightKind::Night && w.start == utc(2026, 10, 19, 2) && w.end == s.next);
  // Polar day: sun up and the next sunset weeks away -> no window ("--").
  s = {true, utc(2026, 7, 24, 22), utc(2026, 7, 25, 0), false, true};
  w = nightWindow(utc(2026, 6, 21, 12), s);
  EXPECT(w.kind == NightKind::PolarDay && w.end <= w.start);
  // Polar night: sun down and the next sunrise weeks away -> now + 12 h.
  s = {false, utc(2027, 1, 22, 10), utc(2027, 1, 22, 12), true, false};
  w = nightWindow(utc(2026, 12, 21, 12), s);
  EXPECT(w.kind == NightKind::PolarNight && w.start == utc(2026, 12, 21, 12) && w.end == utc(2026, 12, 22, 0));
  // Last sunset before polar night: following sunrise > 24 h away -> capped at 24 h.
  s = {true, utc(2026, 11, 26, 11), utc(2027, 1, 16, 11), false, true};
  w = nightWindow(utc(2026, 11, 26, 10, 30), s);
  EXPECT(w.kind == NightKind::Evening && w.start == s.next && w.end == s.next + 86400);
  // Polar-day edge: next event is a sunset weeks away even though the last event was a set.
  s = {true, utc(2026, 7, 26, 0), utc(2026, 7, 26, 1), false, true};
  EXPECT(nightWindow(utc(2026, 6, 21, 12), s).kind == NightKind::PolarDay);
  // Sun down, next event is a sunset (sun briefly above in between is impossible): polar night rule.
  s = {false, utc(2026, 12, 25, 11), 0, false, false};
  EXPECT(nightWindow(utc(2026, 12, 21, 12), s).kind == NightKind::PolarNight);
  // Altitude says up a minute after sunset, but the next event is the sunrise: it is night.
  s = {true, utc(2026, 10, 19, 5, 12), utc(2026, 10, 19, 14, 27), true, false};
  EXPECT(nightWindow(utc(2026, 10, 18, 14, 31), s).kind == NightKind::Night);
  // Sunset with no sunrise event found at all: 24 h cap.
  s = {true, utc(2026, 10, 18, 14, 30), 0, false, false};
  w = nightWindow(utc(2026, 10, 18, 9), s);
  EXPECT(w.kind == NightKind::Evening && w.end == utc(2026, 10, 19, 14, 30));
  if (failures) return 1;
  printf("OK\n");
}
