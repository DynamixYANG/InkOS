#include "Canvas.h"
#include "Text.h"
#include <esp_heap_caps.h>
#include <string.h>
namespace ink {
void textRenderingChecks(int &passed, int &total) {
  auto check = [&](bool ok) { total++; if (ok) passed++; else Serial.printf("INKOS selftest failed_case=%d\n", total); };
  const char *p = "中A文";
  check(decodeUtf8(p) == 0x4E2D && decodeUtf8(p) == 'A' && decodeUtf8(p) == 0x6587 && !*p);
  for (const char *bad : {"\xE4\xB8", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80"}) {
    const char *q = bad; check(decodeUtf8(q) == 0xFFFD && q > bad);
  }
  char clipped[6]; copyUtf8(clipped, "中文", sizeof clipped);
  check(!strcmp(clipped, "中"));
  auto pixels = (uint8_t *)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  check(pixels != nullptr); if (!pixels) return;
  Canvas c(pixels);
  check(c.textWidth("中A文", 14) == 28 + font(14).advance && c.textWidth("中文", 28) == 56);
  check(c.textWidth("Oulu 12:34", 18) == 10 * font(18).advance);
  check(c.prefixBytes("中文ABC", 28, 14) == 6);
  check(c.prefixBytes("中文ABC", 27, 14) == 3);
  check(c.prefixBytes("Hello world", 50, 14, true) == 5);
  c.clear(); c.fit(10, 24, 35, "中文测试很长的标题", 14);
  int black = 0; bool contained = true;
  for (int y = 0; y < HEIGHT; y++) for (int x = 0; x < WIDTH; x++)
    if (!(pixels[y * 100 + x / 8] & (128 >> (x & 7)))) { black++; contained &= x >= 10 && x < 45; }
  check(black > 5 && contained);
  c.clear(); int rows = c.wrapped(10, 24, 28, "中文标题AB", 14, 3, 16);
  check(rows == 3);
  // Two different Chinese glyphs must rasterize differently, with visible ink.
  c.clear(); c.text(0, 24, "中文", 14);
  int first = 0, second = 0, differences = 0;
  for (int y = 0; y < 32; y++) for (int x = 0; x < 14; x++) {
    bool a = !(pixels[y * 100 + x / 8] & (128 >> (x & 7)));
    int bx = x + 14; bool b = !(pixels[y * 100 + bx / 8] & (128 >> (bx & 7)));
    first += a; second += b; differences += a != b;
  }
  check(first > 12 && second > 12 && differences > 10);
  c.clear(); c.rect(0, 0, WIDTH, HEIGHT); c.text(10, 24, "繁體中文", 14, false);
  int white = 0; for (int i = 0; i < FRAME_BYTES; i++) white += __builtin_popcount(pixels[i]);
  check(white > 40);
  free(pixels);
}
}
