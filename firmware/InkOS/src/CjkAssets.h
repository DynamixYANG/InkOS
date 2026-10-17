#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ink {
bool beginCjkAssets();
bool cjkAssetsHealthy();
struct CjkGlyph {uint32_t offset; int8_t x,y; uint8_t w,h;};
bool cjkLookup(uint32_t code,int size,CjkGlyph &glyph,int &advance);
bool cjkRead(const CjkGlyph &glyph,uint8_t *pixels,size_t capacity);
}
