#pragma once
#include <stdint.h>
namespace ink {
// Diagnostic logging that never waits for the USB host.
//
// HWCDC::write() blocks up to 20 x 100 ms per call once its TX ring is full
// while USB stays plugged but no program reads the port (e.g. after Studio or
// a monitor closed it). Task diagnostics therefore go into a 16 KiB PSRAM
// ring of whole lines. When full, the OLDEST whole lines are evicted (FIFO)
// and counted. Writers only hold a short spinlock for a memcpy; no USB I/O.
// The control task drains whole lines to Serial without blocking, so a host
// that opens the port later receives the retained lines oldest-to-newest.
void beginLog();                                                  // before tasks start
void logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void drainLog();      // control task only (same task as USB command replies)
void clearLog();
uint32_t droppedLogLines();   // lines evicted (oldest) or rejected since boot
uint32_t bufferedLogBytes();
} // namespace ink
