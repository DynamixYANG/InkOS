#pragma once
#include "Domain.h"
namespace ink {
struct Glyph {
  uint32_t code, offset;
  int16_t x, y;
  uint8_t w, h;
};
struct Font {
  const uint8_t *bits;
  const Glyph *glyphs;
  uint16_t count;
  uint8_t advance, size;
};
const Font &font(int size);
class Canvas {
public:
  uint8_t *pixels;
  explicit Canvas(uint8_t *p) : pixels(p) {}
  void clear(bool black = false);
  void dot(int x, int y, bool black = true);
  void rect(int x, int y, int w, int h, bool black = true);
  void box(int x, int y, int w, int h, int stroke = 1);
  void line(int x, int y, int x2, int y2, int stroke = 1);
  void circle(int x, int y, int r, int stroke = 1);
  void text(int x, int baseline, const char *text, int size = 16, bool black = true);
  int textWidth(const char *, int size = 16);
  void fit(int x, int baseline, int width, const char *, int size = 20);
  void centered(int x, int baseline, int width, const char *, int size = 16);
  void icon(int x, int y, int size, int code, bool day = true);
  void animatedIcon(int x, int y, int size, int code, bool day, unsigned phase);
  void image(int x, int y, int w, int h, const uint8_t *bits);
  void dottedLine(int x, int y, int w);
};
void renderChrome(Canvas &, const char *, Page, const Model &);
void renderHome(Canvas &, const Model &, time_t, unsigned phase = 0);
void renderMusic(Canvas &, const Model &, time_t, int64_t);
void renderForecast(Canvas &, const Model &, time_t);
void renderWeek(Canvas &, const Model &, time_t);
void renderMoon(Canvas &, const Model &, time_t);
void renderSun(Canvas &, const Model &, time_t);
void renderEnergy(Canvas &, const Model &, time_t);
void renderCurrentPrice(Canvas &, const Energy &, time_t);
void renderHomeAstronomy(Canvas &, const Settings &, time_t);
void moonIcon(Canvas &,int x,int y,int radius,double phase,double illuminated,bool southern=false);
void solarEventIcon(Canvas &,int x,int y,bool rising,int size=18);
} // namespace ink
