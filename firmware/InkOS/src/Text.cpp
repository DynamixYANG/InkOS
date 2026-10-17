#include "Text.h"
#include <string.h>
namespace ink {
uint32_t decodeUtf8(const char *&s) {
  if (!*s) return 0;
  uint8_t first = uint8_t(*s++);
  if (first < 0x80) return first;
  int count = first >= 0xC2 && first <= 0xDF ? 1 :
              first >= 0xE0 && first <= 0xEF ? 2 :
              first >= 0xF0 && first <= 0xF4 ? 3 : 0;
  if (!count) return 0xFFFD;
  const char *p = s;
  uint32_t cp = first & ((1 << (6 - count)) - 1);
  for (int i = 0; i < count; i++) {
    if (!*p || (uint8_t(*p) & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (uint8_t(*p++) & 0x3F);
  }
  if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) ||
      (count == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
    return 0xFFFD;
  s = p;
  return cp;
}
void copyUtf8(char *out, const char *text, size_t capacity) {
  if (!capacity) return;
  const char *p = text, *end = text;
  while (*p) {
    decodeUtf8(p);
    if (size_t(p - text) >= capacity) break;
    end = p;
  }
  size_t bytes = end - text;
  memcpy(out, text, bytes); out[bytes] = 0;
}
}
