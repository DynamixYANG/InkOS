#pragma once
#include <stdint.h>
namespace ink {
// Coalesce low-priority publishers without ever delaying navigation. The cap
// prevents frequent background updates from starving the display indefinitely.
class BackgroundSchedule {
  int64_t first = -1, last = -1;
public:
  bool pending() const { return first >= 0; }
  void request(int64_t now) { if (first < 0) first = now; last = now; }
  bool ready(int64_t now) const {
    return pending() && (now - last >= 40000 || now - first >= 150000);
  }
  void clear() { first = last = -1; }
};
// Small render identity; comparing this avoids copying the 99 KB Model while
// idle, waiting for selection, or reusing a frame prepared during quiet time.
struct RenderToken {
  uint32_t navigation = 0, model = 0, settings = 0, weather = 0, music = 0,
           artwork = 0, agenda = 0, energy = 0;
  int64_t minute = 0, animation = 0;
  int lyric = -1;
  bool stale = false;
  bool operator==(const RenderToken &v) const {
    return navigation==v.navigation && model==v.model && settings==v.settings &&
      weather==v.weather && music==v.music && artwork==v.artwork &&
      agenda==v.agenda && energy==v.energy && minute==v.minute &&
      animation==v.animation && lyric==v.lyric && stale==v.stale;
  }
};
} // namespace ink
