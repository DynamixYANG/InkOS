#pragma once
#include "Domain.h"
#include <SPI.h>
namespace ink {
enum class RefreshMode { Clock, Music, Animation };
class Panel {
  SPIClass spi{HSPI};
  bool ok = false, powered = false;
  uint8_t *oldFrame = nullptr;
  uint8_t *window = nullptr;
  uint32_t partials = 0, lastFull = 0;
  uint32_t textPartials = 0, animationPartials = 0;
  void command(uint8_t);
  void data(const uint8_t *, size_t);
  void reg(uint8_t, uint8_t);
  bool ready(uint32_t);
  bool refresh(uint32_t);
  bool powerOn();
  bool powerOff();
  void fullMode();
  void partialMode();
  void writeFull(const uint8_t *);
  void solid(uint8_t);

public:
  bool begin();
  bool present(const uint8_t *, bool forceFull, RefreshMode);
  uint32_t lastDuration = 0, frames = 0, fullFrames = 0;
  Rect lastRegion = {0, 0, 0, 0};
  bool healthy() const {
    return ok;
  }
  const uint8_t *frame() const {
    return oldFrame;
  }
};
extern Panel panel;
} // namespace ink
