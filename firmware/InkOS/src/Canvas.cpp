#include "Canvas.h"
#include "Text.h"
#include "CjkAssets.h"
#include <algorithm>
#include <math.h>
#include <string.h>
namespace ink {
void drawWeatherIcon(Canvas &,int,int,int,int,bool,unsigned,bool);
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
static int advance(uint32_t cp, int size) {
  auto &f=font(size); if(glyph(f,cp)) return f.advance;
  CjkGlyph g; int step; return cjkLookup(cp,size,g,step)?step:f.advance;
}
void Canvas::text(int x, int baseline, const char *s, int size, bool black) {
  while(*s){
    uint32_t cp=decodeUtf8(s);auto &face=font(size);auto g=glyph(face,cp);
    const uint8_t *bits=nullptr;int gx=0,gy=0,w=0,h=0,step=face.advance;uint8_t raster[256];
    CjkGlyph cg;
    if(!g&&cjkLookup(cp,size,cg,step)){
      if(cjkRead(cg,raster,sizeof raster)){bits=raster;gx=cg.x;gy=cg.y;w=cg.w;h=cg.h;}
    }else{
      if(!g)g=glyph(face,'?');
      if(g){bits=face.bits+g->offset;gx=g->x;gy=g->y;w=g->w;h=g->h;}
    }
    if(bits)for(int y=0;y<h;y++)for(int xx=0;xx<w;xx++){
      int bit=y*w+xx;if(bits[bit/8]&(128>>(bit&7)))dot(x+gx+xx,baseline+gy+y,black);
    }
    x+=step;
  }
}
int Canvas::textWidth(const char *s, int size) {
  int width = 0;
  while (*s) width += advance(decodeUtf8(s), size);
  return width;
}
int Canvas::prefixBytes(const char *s, int width, int size, bool wordWrap) {
  const char *p = s, *end = s, *space = nullptr;
  int used = 0; uint32_t previous = 0, next = 0;
  while (*p && *p != '\n') {
    const char *start = p;
    uint32_t cp = decodeUtf8(p);
    int step = advance(cp, size);
    if (used + step > width || p - s > 508) { next = cp; break; }
    if (cp == ' ') space = start;
    used += step; end = p; previous = cp;
  }
  // Keep Latin words together, but allow Chinese to break between glyphs.
  if (wordWrap && space && space > s && next > 32 && next < 127 && previous >= 32 && previous < 127)
    end = space;
  return end - s;
}
void Canvas::centered(int x, int y, int w, const char *s, int size) {
  text(x + (w - textWidth(s, size)) / 2, y, s, size);
}
void Canvas::fit(int x, int y, int w, const char *s, int size, bool black) {
  if (w <= 0) return;
  char out[512];
  int bytes = prefixBytes(s, w, size);
  if (!s[bytes]) { text(x, y, s, size, black); return; }
  const int ellipsis = textWidth("...", size);
  if (ellipsis > w) return;
  bytes = prefixBytes(s, w - ellipsis, size);
  memcpy(out, s, bytes); memcpy(out + bytes, "...", 4);
  text(x, y, out, size, black);
}
int Canvas::wrapped(int x, int baseline, int width, const char *s, int size, int maxLines, int lineHeight) {
  int rows = 0;
  while (*s && rows < maxLines) {
    int bytes = prefixBytes(s, width, size, true);
    if (!bytes && *s != '\n') break;
    if (rows + 1 == maxLines && s[bytes] && s[bytes] != '\n') {
      fit(x, baseline + rows * lineHeight, width, s, size);
    } else {
      char row[512]; memcpy(row, s, bytes); row[bytes] = 0;
      text(x, baseline + rows * lineHeight, row, size);
    }
    s += bytes; if (*s == '\n') s++;
    while (*s == ' ') s++;
    rows++;
  }
  return rows;
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
void Canvas::icon(int x,int y,int s,int code,bool day) {
  drawWeatherIcon(*this,x,y,s,code,day,0,false);
}
} // namespace ink
