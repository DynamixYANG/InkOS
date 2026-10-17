#include "Log.h"
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <esp_heap_caps.h>
#include <stdarg.h>
namespace ink {
namespace {
constexpr size_t Capacity = 16384, MaxLine = 320;
// HWCDC TX ring size set in start(). Only drain while the host has emptied it
// (keeps at most one drain chunk of stale lines in the driver while the port
// is closed, instead of 4 KiB of old lines in front of the retained ones).
constexpr int UsbTxBytes = 4096, UsbIdleSlack = 128, DrainChunk = 1024;
char *ring = nullptr;
size_t head = 0, tail = 0, used = 0; // Bytes; every stored line ends with '\n'.
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> dropped{0};
// Requires lock. Removes the oldest complete line.
void evictOldest() {
  while (used) {
    char c = ring[tail];
    tail = (tail + 1) % Capacity;
    used--;
    if (c == '\n')
      break;
  }
  dropped++;
}
} // namespace
void beginLog() {
  if (!ring)
    ring = (char *)heap_caps_malloc(Capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void logf(const char *format, ...) {
  char line[MaxLine];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(line, sizeof line, format, args);
  va_end(args);
  if (n <= 0)
    return;
  size_t length = std::min(size_t(n), sizeof line - 1);
  if (line[length - 1] != '\n') {
    if (length == sizeof line - 1)
      length--; // Truncated: keep room for the terminator.
    line[length++] = '\n';
  }
  if (!ring) {
    dropped++;
    return;
  }
  portENTER_CRITICAL(&lock);
  while (Capacity - used < length)
    evictOldest();
  size_t first = std::min(length, Capacity - head);
  memcpy(ring + head, line, first);
  memcpy(ring, line + first, length - first);
  head = (head + length) % Capacity;
  used += length;
  portEXIT_CRITICAL(&lock);
}
void drainLog() {
  if (!ring)
    return;
  int room = Serial.availableForWrite();
  if (room < UsbTxBytes - UsbIdleSlack)
    return; // Host is not consuming (port closed) or still draining.
  static char chunk[DrainChunk];
  size_t n = 0;
  portENTER_CRITICAL(&lock);
  // Copy whole lines only, so a concurrent eviction can never tear output.
  size_t scan = 0, lastEnd = 0;
  while (scan < used && scan < DrainChunk) {
    if (ring[(tail + scan) % Capacity] == '\n')
      lastEnd = scan + 1;
    scan++;
  }
  for (size_t i = 0; i < lastEnd; i++)
    chunk[i] = ring[(tail + i) % Capacity];
  n = lastEnd;
  tail = (tail + n) % Capacity;
  used -= n;
  portEXIT_CRITICAL(&lock);
  if (n)
    Serial.write(reinterpret_cast<const uint8_t *>(chunk), n);
}
void clearLog() {
  portENTER_CRITICAL(&lock);
  head = tail = used = 0;
  portEXIT_CRITICAL(&lock);
}
uint32_t droppedLogLines() {
  return dropped.load();
}
uint32_t bufferedLogBytes() {
  portENTER_CRITICAL(&lock);
  size_t n = used;
  portEXIT_CRITICAL(&lock);
  return n;
}
} // namespace ink
