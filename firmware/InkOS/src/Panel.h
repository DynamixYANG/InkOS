#pragma once
#include "Domain.h"
#include <SPI.h>
#include <atomic>
namespace ink {
enum class RefreshMode { Clock, Music, Animation, Navigation };
// Per-present phase timing in microseconds. transfer = mode/LUT setup + SPI data.
struct PanelPhases {
  uint32_t powerOnUs = 0, transferUs = 0, waveformUs = 0, powerOffUs = 0, totalUs = 0;
};
class Panel {
  SPIClass spi{HSPI};
  bool ok = false, powered = false;
  uint8_t *oldFrame = nullptr;
  uint8_t *window = nullptr;
  uint32_t partials = 0, lastFull = 0, lastStandardFull = 0;
  uint32_t fastFullsSinceStandard = 0;
  uint32_t textPartials = 0, animationPartials = 0;
  PanelPhases phases; // Accumulated during the current present().
  // Drive power deliberately left on after a navigation title (see present()).
  bool held = false;
  uint32_t heldSinceMs = 0;
  void command(uint8_t);
  void data(const uint8_t *, size_t);
  void reg(uint8_t, uint8_t);
  bool ready(uint32_t);
  void busyStarted();
  bool refresh(uint32_t);
  bool powerOn();
  bool powerOff();
  void fullMode(bool fast);
  void partialMode();
  void navigationMode();
  void writeFull(const uint8_t *);
  void solid(uint8_t);

public:
  bool begin();
  // settledNavigation: the full refresh that commits a navigation selection.
  bool present(const uint8_t *, bool forceFull, RefreshMode, bool settledNavigation = false);
  // Display task only: power off a held panel after holdIdleMs without the
  // expected follow-up (another title or the settled full refresh).
  void idle();
  // Runtime tuning (NVS "nav_hold"/"nav_idle"); written by configuration.
  std::atomic<bool> holdNavigationPower{true};
  std::atomic<uint32_t> holdIdleMs{3000};
  // NVS "nav_fast": settled-navigation full refresh uses the fast OTP full
  // waveform (forced temperature). Boot/maintenance full stays standard.
  std::atomic<bool> fastNavigationFull{true};
  // After this many consecutive fast fulls the next settled full is standard.
  static constexpr uint32_t FastFullLimit = 8;
  bool powerHeld() const {
    return held && powered;
  }
  uint32_t lastDuration = 0, frames = 0, fullFrames = 0;
  Rect lastRegion = {0, 0, 0, 0};
  PanelPhases lastPhases;
  bool healthy() const {
    return ok;
  }
  const uint8_t *frame() const {
    return oldFrame;
  }
};
extern Panel panel;
} // namespace ink
