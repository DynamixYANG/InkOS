#pragma once
#include <stdint.h>
#include <stddef.h>
namespace ink {
// Byte-aligned title-only bounds. No header rule, price, status or body pixels.
constexpr int NavigationLeft=8, NavigationRight=303, NavigationTop=4, NavigationBottom=34;
constexpr bool navigationWindowAllowed(int minByte,int maxByte,int minY,int maxY) {
  return minByte>=NavigationLeft/8 && maxByte<=NavigationRight/8 &&
    minY>=NavigationTop && maxY<=NavigationBottom && maxByte>=minByte && maxY>=minY;
}
constexpr uint8_t NavigationPhaseFrames=6;
constexpr size_t navigationLutBytes(int lut) { return lut==1 || lut==5 ? 42 : 60; }
inline void makeNavigationLut(int lut,uint8_t (&bytes)[60]) {
  for(auto &v:bytes) v=0;
  // Six-frame balanced differential pulses strengthen erasure after the
  // one-frame (~0.3s) trial left old glyphs visible. WW/KK/VCOM/
  // border stay at their DC baseline; KW and WK have opposite balanced phases.
  bytes[0]=lut==2?0x5a:lut==3?0x84:0;
  bytes[1]=NavigationPhaseFrames; bytes[3]=NavigationPhaseFrames; bytes[5]=1;
}
} // namespace ink
