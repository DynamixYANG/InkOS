#include "Canvas.h"
#include <math.h>
namespace ink {
static void cloud(Canvas &c, int x, int y, int s) {
  auto inside = [&](int a, int b) {
    auto disk = [&](int cx, int cy, int r) {
      return (a - cx) * (a - cx) + (b - cy) * (b - cy) <= r * r;
    };
    return disk(s * 25 / 100, s * 47 / 100, s * 16 / 100) ||
           disk(s / 2, s * 35 / 100, s * 23 / 100) ||
           disk(s * 75 / 100, s * 47 / 100, s * 16 / 100) ||
           (a >= s / 4 && a <= s * 3 / 4 && b >= s * 45 / 100 && b <= s * 63 / 100);
  };
  for (int b = 0; b < s; b++)
    for (int a = 0; a < s; a++)
      if (inside(a, b))
        c.dot(x + a, y + b,
              !inside(a - 1, b) || !inside(a + 1, b) || !inside(a, b - 1) || !inside(a, b + 1));
}
void Canvas::animatedIcon(int x, int y, int s, int code, bool day, unsigned phase) {
  if (code < 0) {
    icon(x, y, s, code, day);
    return;
  }
  phase %= 8;
  if (code <= 2) {
    int cx = x + s * 2 / 5, cy = y + s * 2 / 5, r = s / 7;
    if (day) {
      circle(cx, cy, r, 1);
      for (int a = 0; a < 8; a++) {
        float v = a * 0.785398163f + phase * 0.09817477f;
        line(cx + lroundf(cosf(v) * (r + 4)), cy + lroundf(sinf(v) * (r + 4)),
             cx + lroundf(cosf(v) * (r + 10)), cy + lroundf(sinf(v) * (r + 10)));
      }
    } else {
      circle(cx, cy, r + 2, 1);
      rect(cx, cy - r - 4, r + 6, 2 * r + 9, false);
    }
  }
  if (code >= 1) {
    const int shift[] = {0, 1, 2, 3, 4, 3, 2, 1};
    int a = shift[phase];
    cloud(*this, x + 2 + a, y + 2, s * 2 / 3);
    cloud(*this, x + s / 5 - a, y + s / 6, s * 4 / 5);
  }
  bool wet = code >= 51;
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  if (wet)
    for (int i = 0; i < 3; i++) {
      int py = y + s * 7 / 10 + ((phase + i * 2) % 4) * s / 16, px = x + s * (i + 2) / 6;
      if (snow) {
        line(px - 2, py, px + 2, py);
        line(px, py - 2, px, py + 2);
      } else
        line(px + 2, py - 3, px - 1, py + 3);
    }
  if (code == 45 || code == 48) {
    line(x + s / 6, y + s * 4 / 5, x + s * 5 / 6, y + s * 4 / 5);
    line(x + s / 4, y + s * 9 / 10, x + s * 3 / 4, y + s * 9 / 10);
  }
  if (code >= 95) {
    line(x + s / 2, y + s * 2 / 3, x + s / 2 - 5, y + s * 5 / 6, 2);
    line(x + s / 2 - 5, y + s * 5 / 6, x + s / 2 + 3, y + s * 4 / 5, 2);
  }
}
} // namespace ink
