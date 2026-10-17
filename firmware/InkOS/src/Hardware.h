#pragma once
#include <stdint.h>
namespace ink {
class EncoderDecoder {
  uint8_t previous = 3;
  int edges = 0;
public:
  inline __attribute__((always_inline)) void begin(uint8_t state) { previous = state & 3; edges = 0; }
  inline __attribute__((always_inline)) int update(uint8_t state) {
    state &= 3;
    if ((previous ^ state) == 3) edges = 0; // Missed/impossible transition.
    else if(previous!=state) edges += ((previous>>1)^(state&1)) ? -1 : 1;
    previous = state;
    if (state != 3) return 0;
    int movement = edges >= 4 ? 1 : edges <= -4 ? -1 : 0;
    edges = 0;
    return movement;
  }
};
// All access is serialized by encoderLock, including capture in the ISR.
class EncoderInput {
  EncoderDecoder decoder;
  int movement = 0;
  int64_t lockedUntil = 0;
public:
  void begin(uint8_t state) { decoder.begin(state); movement = 0; lockedUntil = 0; }
  inline __attribute__((always_inline)) bool edge(uint8_t state, int64_t now) {
    if (now < lockedUntil) { decoder.begin(state); return false; }
    int step = decoder.update(state);
    if (step > 0 && movement < 64) movement++;
    else if (step < 0 && movement > -64) movement--;
    return step != 0;
  }
  int take(int64_t now) {
    int result = now < lockedUntil ? 0 : movement;
    movement = 0;
    return result;
  }
  void lock(uint8_t state, int64_t now, uint32_t milliseconds) {
    lockedUntil = now + int64_t(milliseconds) * 1000;
    if (!milliseconds) return; // Disabled means preserve pending input, too.
    movement = 0;
    decoder.begin(state); // Never replay a detent captured before the lock.
  }
  bool locked(int64_t now) const { return now < lockedUntil; }
};
int batteryPercent(float voltage);
int wifiLevel(int rssi);
void beginHardware();
int takeEncoderMovement();
void lockEncoder(uint32_t milliseconds);
bool encoderLocked();
void updateHardwareStatus();
} // namespace ink
