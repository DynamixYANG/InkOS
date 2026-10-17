#pragma once
#include <stdint.h>
namespace ink {
// Diagnostic log line that never waits for the USB host. HWCDC::write() blocks
// up to 20 x 100 ms per call once its TX ring is full while USB stays plugged
// but no program reads the port (e.g. after Studio or a monitor closed it).
// Lines that do not fit in the free TX space are dropped and counted.
void logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
uint32_t droppedLogLines();
} // namespace ink
