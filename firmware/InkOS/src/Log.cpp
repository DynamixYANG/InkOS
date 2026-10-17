#include "Log.h"
#include <Arduino.h>
#include <algorithm>
#include <atomic>
#include <stdarg.h>
namespace ink {
static std::atomic<uint32_t> dropped{0};
void logf(const char *format, ...) {
  char line[320];
  va_list args;
  va_start(args, format);
  int n = vsnprintf(line, sizeof line, format, args);
  va_end(args);
  if (n <= 0)
    return;
  size_t length = std::min(size_t(n), sizeof line - 1);
  if (Serial.availableForWrite() < int(length)) {
    dropped++;
    return;
  }
  Serial.write(reinterpret_cast<const uint8_t *>(line), length);
}
uint32_t droppedLogLines() {
  return dropped.load();
}
} // namespace ink
