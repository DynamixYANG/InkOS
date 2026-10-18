#include "Log.h"
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <esp_heap_caps.h>
#include <stdarg.h>
namespace ink {
namespace {
constexpr size_t Capacity = 16384, MaxLine = 320, FileNotifyBytes = 8192;
// HWCDC TX ring size set in start(). Only drain while the host has emptied it
// (keeps at most one drain chunk of stale lines in the driver while the port
// is closed, instead of 4 KiB of old lines in front of the retained ones).
constexpr int UsbTxBytes = 4096, UsbIdleSlack = 128, DrainChunk = 1024;
char *ring = nullptr;
size_t head = 0, retained = 0;  // History bytes ending at head; whole lines.
size_t pending[2] = {0, 0};     // Unread bytes per reader, <= retained.
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> dropped[2];
std::atomic<bool> enabled{true};
void (*fileNotify)() = nullptr;
inline size_t at(size_t backFromHead) { return (head + Capacity - backFromHead) % Capacity; }
// Requires lock. Removes the oldest retained line.
void evictOldest() {
  size_t start = at(retained), n = 0;
  while (n < retained) {
    char c = ring[(start + n) % Capacity];
    n++;
    if (c == '\n')
      break;
  }
  retained -= n;
  for (int r = 0; r < 2; r++)
    if (pending[r] > retained) {
      pending[r] = retained; // Readers sit on line boundaries.
      dropped[r]++;
    }
}
// Requires lock. Copy whole lines starting `back` bytes before head.
size_t copyLines(size_t back, char *out, size_t capacity) {
  size_t start = at(back), lastEnd = 0;
  for (size_t i = 0; i < back && i < capacity; i++) {
    char c = ring[(start + i) % Capacity];
    out[i] = c;
    if (c == '\n')
      lastEnd = i + 1;
  }
  return lastEnd;
}
} // namespace
void beginLog() {
  if (!ring)
    ring = (char *)heap_caps_malloc(Capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void logf(const char *format, ...) {
  if (!enabled.load(std::memory_order_relaxed))
    return; // Cheap no-op when logging is disabled.
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
    dropped[0]++;
    dropped[1]++;
    return;
  }
  bool notify = false;
  portENTER_CRITICAL(&lock);
  while (Capacity - retained < length)
    evictOldest();
  size_t first = std::min(length, Capacity - head);
  memcpy(ring + head, line, first);
  memcpy(ring, line + first, length - first);
  head = (head + length) % Capacity;
  retained += length;
  pending[0] += length;
  size_t before = pending[1];
  pending[1] += length;
  notify = before < FileNotifyBytes && pending[1] >= FileNotifyBytes;
  portEXIT_CRITICAL(&lock);
  if (notify && fileNotify)
    fileNotify();
}
size_t takeLogLines(LogReader reader, char *out, size_t capacity) {
  if (!ring)
    return 0;
  int r = int(reader);
  portENTER_CRITICAL(&lock);
  size_t n = copyLines(pending[r], out, capacity);
  pending[r] -= n;
  portEXIT_CRITICAL(&lock);
  return n;
}
size_t pendingLogBytes(LogReader reader) {
  portENTER_CRITICAL(&lock);
  size_t n = pending[int(reader)];
  portEXIT_CRITICAL(&lock);
  return n;
}
size_t tailLog(char *out, size_t capacity) {
  if (!ring || !capacity)
    return 0;
  portENTER_CRITICAL(&lock);
  size_t back = retained;
  // Skip whole oldest lines until the rest fits in the caller's buffer.
  size_t start = at(back);
  while (back > capacity) {
    size_t i = 0;
    while (i < back && ring[(start + i) % Capacity] != '\n')
      i++;
    back -= std::min(back, i + 1);
    start = at(back);
  }
  size_t n = copyLines(back, out, capacity);
  portEXIT_CRITICAL(&lock);
  return n;
}
void drainLog() {
  if (!ring || !pendingLogBytes(LogReader::Usb))
    return;
  int room = Serial.availableForWrite();
#if ARDUINO_USB_MODE
  // HWCDC: a nearly empty 4 KB TX ring means the host is reading.
  if (room < UsbTxBytes - UsbIdleSlack)
    return; // Host is not consuming (port closed) or still draining.
  size_t cap = DrainChunk;
#else
  // TinyUSB CDC reports 0 while no terminal holds DTR; otherwise its FIFO room.
  if (room < 128)
    return;
  size_t cap = std::min<size_t>(size_t(room), DrainChunk);
#endif
  static char chunk[DrainChunk];
  size_t n = takeLogLines(LogReader::Usb, chunk, cap);
  if (n)
    Serial.write(reinterpret_cast<const uint8_t *>(chunk), n);
}
void clearLog() {
  portENTER_CRITICAL(&lock);
  head = retained = pending[0] = pending[1] = 0;
  portEXIT_CRITICAL(&lock);
}
void setLoggingEnabled(bool on) { enabled = on; }
bool loggingEnabled() { return enabled.load(); }
void setLogFileNotify(void (*notify)()) { fileNotify = notify; }
uint32_t droppedLogLines() { return dropped[0].load(); }
uint32_t droppedFileLogLines() { return dropped[1].load(); }
uint32_t bufferedLogBytes() {
  portENTER_CRITICAL(&lock);
  size_t n = retained;
  portEXIT_CRITICAL(&lock);
  return n;
}
} // namespace ink
