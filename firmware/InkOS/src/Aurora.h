#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>
// Planetary K-index forecast (NOAA SWPC) and the "tonight" window used on Home.
// Pure code: no Arduino, heap or clock access, so it is unit-tested on the host.
namespace ink {
constexpr int MaxKpBins = 112;           // the product carries ~81 bins (7 days back + 3 ahead)
constexpr time_t KpBinSeconds = 3 * 3600;
constexpr time_t KpStaleSeconds = 6 * 3600;
enum class KpKind : uint8_t { Observed, Estimated, Predicted };
struct KpBin {
  time_t start = 0; // UTC start of the 3-hour bin
  float kp = 0;
  KpKind kind = KpKind::Predicted;
};
struct Aurora {
  KpBin bins[MaxKpBins];
  int count = 0;
  time_t fetched = 0; // device clock at the last successful parse; 0 = never
  uint32_t revision = 0;
};
enum class NightKind : uint8_t { Evening, Night, PolarNight, PolarDay };
struct NightWindow {
  time_t start = 0, end = 0; // [start, end); empty for PolarDay
  NightKind kind = NightKind::PolarDay;
};
struct KpPeak {
  float kp = -1; // < 0: no bin overlaps the window
  time_t at = 0; // start of the (first) maximal bin
};
// Sun state at now (solar altitude above the -0.833 deg rise/set horizon) and the next two
// solar events after now (from adjacentSolarEvents). 0 = none found.
struct SunContext {
  bool sunUp = false;
  time_t next = 0, following = 0;
  bool nextRising = false, followingRising = false;
};
// Coming dark window. The sun is up when the next event within 24 h is a sunset (else sunUp). Sun up: next sunset to the following sunrise (capped at 24 h).
// Sun down: now to the next sunrise. No sunrise within 24 h (polar night): now + 12 h.
// No sunset within 24 h while the sun is up (polar day): PolarDay, empty.
NightWindow nightWindow(time_t now, const SunContext &sun);
KpPeak peakKp(const Aurora &, time_t start, time_t end);
bool kpStale(const Aurora &, time_t now);
// NOAA thirds: 4.00 -> "4", 4.33 -> "4+", 4.67 -> "5-"; negative -> "--".
void kpText(char *out, size_t size, float kp);
// Accepts the current array-of-objects JSON and the older array-of-arrays layout (header row).
// Returns the number of bins stored (sorted by time); 0 on malformed input.
int parseKpForecast(const char *json, size_t length, Aurora &out);
} // namespace ink
