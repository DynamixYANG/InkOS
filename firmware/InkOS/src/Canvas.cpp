#include "Canvas.h"
#include <algorithm>
#include <math.h>
#include <string.h>
namespace ink {
void Canvas::clear(bool b) {
  memset(pixels, b ? 0 : 255, FRAME_BYTES);
}
void Canvas::dot(int x, int y, bool b) {
  if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT)
    return;
  uint8_t &m = pixels[y * 100 + x / 8];
  if (b)
    m &= ~(128 >> (x & 7));
  else
    m |= 128 >> (x & 7);
}
void Canvas::rect(int x, int y, int w, int h, bool b) {
  for (int yy = std::max(0, y); yy < std::min(HEIGHT, y + h); yy++)
    for (int xx = std::max(0, x); xx < std::min(WIDTH, x + w); xx++)
      dot(xx, yy, b);
}
void Canvas::box(int x, int y, int w, int h, int s) {
  rect(x, y, w, s);
  rect(x, y + h - s, w, s);
  rect(x, y, s, h);
  rect(x + w - s, y, s, h);
}
void Canvas::line(int x, int y, int x2, int y2, int s) {
  int dx = abs(x2 - x), sx = x < x2 ? 1 : -1, dy = -abs(y2 - y), sy = y < y2 ? 1 : -1,
      err = dx + dy;
  for (;;) {
    rect(x, y, s, s);
    if (x == x2 && y == y2)
      break;
    int a = 2 * err;
    if (a >= dy) {
      err += dy;
      x += sx;
    }
    if (a <= dx) {
      err += dx;
      y += sy;
    }
  }
}
void Canvas::circle(int x, int y, int r, int stroke) {
  for (int yy = -r; yy <= r; yy++)
    for (int xx = -r; xx <= r; xx++) {
      int q = xx * xx + yy * yy;
      if (q <= r * r && q >= (r - stroke) * (r - stroke))
        dot(x + xx, y + yy);
    }
}
static uint32_t utf8(const char *&s) {
  uint8_t a = *s++;
  if (a < 128)
    return a;
  int n = a < 224 ? 1 : a < 240 ? 2 : 3;
  uint32_t cp = a & ((1 << (6 - n)) - 1);
  for (int i = 0; i < n; i++) {
    if ((*s & 192) != 128)
      return '?';
    cp = (cp << 6) | (*s++ & 63);
  }
  return cp;
}
static const Glyph *glyph(const Font &f, uint32_t cp) {
  int l = 0, r = f.count - 1;
  while (l <= r) {
    int m = (l + r) / 2;
    uint32_t c = f.glyphs[m].code;
    if (c == cp)
      return f.glyphs + m;
    if (c < cp)
      l = m + 1;
    else
      r = m - 1;
  }
  return nullptr;
}
void Canvas::text(int x, int baseline, const char *s, int size, bool black) {
  const Font &f = font(size);
  while (*s) {
    uint32_t cp = utf8(s);
    const Glyph *g = glyph(f, cp);
    if (!g)
      g = glyph(f, '?');
    if (g) {
      for (int y = 0; y < g->h; y++)
        for (int xx = 0; xx < g->w; xx++) {
          int bit = y * g->w + xx;
          if (f.bits[g->offset + bit / 8] & (128 >> (bit & 7)))
            dot(x + g->x + xx, baseline + g->y + y, black);
        }
    }
    x += f.advance;
  }
}
int Canvas::textWidth(const char *s, int size) {
  int n = 0;
  while (*s) {
    utf8(s);
    n++;
  }
  return n * font(size).advance;
}
void Canvas::centered(int x, int y, int w, const char *s, int size) {
  text(x + (w - textWidth(s, size)) / 2, y, s, size);
}
void Canvas::fit(int x, int y, int w, const char *s, int size) {
  char out[512];
  strlcpy(out, s, sizeof out);
  if (textWidth(out, size) <= w) {
    text(x, y, out, size);
    return;
  }
  int max = std::max(1, w / font(size).advance - 3);
  const char *p = out;
  for (int i = 0; i < max && *p; i++)
    utf8(p);
  int len = p - out;
  strlcpy(out + len, "...", sizeof(out) - len);
  text(x, y, out, size);
}
void Canvas::dottedLine(int x, int y, int w) {
  for (int a = x; a < x + w; a += 6)
    line(a, y, std::min(a + 2, x + w - 1), y);
}
void Canvas::image(int x, int y, int w, int h, const uint8_t *b) {
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      if (!(b[(yy * w + xx) / 8] & (128 >> ((yy * w + xx) & 7))))
        dot(x + xx, y + yy);
}
void Canvas::icon(int x, int y, int s, int code, bool day) {
  if (code < 0) {
    text(x + s / 3, y + s * 2 / 3, "?", s > 50 ? 32 : 20);
    return;
  }
  bool sun = code <= 2, cloud = code >= 1, wet = code >= 51,
       snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  if (sun) {
    int cx = x + s * 2 / 5, cy = y + s * 2 / 5, r = s / 7;
    if (day) {
      circle(cx, cy, r, 2);
      for (int a = 0; a < 8; a++) {
        float v = a * 0.785398f;
        line(cx + cosf(v) * (r + 4), cy + sinf(v) * (r + 4), cx + cosf(v) * (r + 9),
             cy + sinf(v) * (r + 9), 1);
      }
    } else {
      circle(cx, cy, r + 3, 2);
      rect(cx, cy - r - 5, r + 7, 2 * r + 10, false);
    }
  }
  if (cloud) {
    // Outline the union, rather than drawing overlapping circles inside the cloud.
    auto inside = [&](int px, int py) {
      auto disk = [&](int cx, int cy, int r) {
        return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
      };
      return disk(s * 30 / 100, s * 50 / 100, s * 14 / 100) ||
             disk(s * 50 / 100, s * 40 / 100, s * 20 / 100) ||
             disk(s * 72 / 100, s * 52 / 100, s * 14 / 100) ||
             (px >= s * 30 / 100 && px <= s * 72 / 100 && py >= s / 2 && py <= s * 66 / 100);
    };
    for (int py = 0; py < s; py++)
      for (int px = 0; px < s; px++)
        if (inside(px, py)) {
          bool edge = !inside(px - 2, py) || !inside(px + 2, py) || !inside(px, py - 2) ||
                      !inside(px, py + 2);
          dot(x + px, y + py, edge);
        }
  }
  if (wet) {
    for (int a = 0; a < 3; a++) {
      int px = x + s * (a + 2) / 6, py = y + s * 4 / 5;
      if (snow) {
        line(px - 3, py, px + 3, py);
        line(px, py - 3, px, py + 3);
      } else
        line(px + 2, py - 3, px - 2, py + 4, 2);
    }
  }
  if (code == 45 || code == 48) {
    line(x + s / 5, y + s * 3 / 4, x + s * 4 / 5, y + s * 3 / 4);
    line(x + s / 4, y + s * 7 / 8, x + s * 3 / 4, y + s * 7 / 8);
  }
  if (code >= 95) {
    line(x + s / 2, y + s * 2 / 3, x + s / 2 - 5, y + s * 5 / 6, 2);
    line(x + s / 2 - 5, y + s * 5 / 6, x + s / 2 + 3, y + s * 4 / 5, 2);
    line(x + s / 2 + 3, y + s * 4 / 5, x + s / 2, y + s, 2);
  }
}
} // namespace ink
