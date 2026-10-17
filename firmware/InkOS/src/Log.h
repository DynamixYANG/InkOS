#pragma once
#include <stddef.h>
#include <stdint.h>
namespace ink {
// Diagnostic logging that never waits for USB or flash.
//
// HWCDC::write() blocks up to 20 x 100 ms per call once its TX ring is full
// while USB stays plugged but no program reads the port. Task diagnostics
// therefore go into a 16 KiB PSRAM ring of whole lines. Writers hold a short
// spinlock for a memcpy only. The ring keeps history; two readers consume it:
//  - USB: the control task drains whole lines to Serial without blocking;
//  - File: the LogStore writer task (core 0, prio 1) persists to LittleFS.
// When full, the OLDEST whole lines are evicted (FIFO); lines a reader had not
// consumed yet are counted per reader.
enum class LogReader : uint8_t { Usb = 0, File = 1 };
void beginLog();                                                  // before tasks start
void logf(const char *format, ...) __attribute__((format(printf, 1, 2)));
void drainLog();                 // control task only (same task as USB replies)
// Copy whole unread lines (<= capacity bytes) for a reader and mark them read.
size_t takeLogLines(LogReader reader, char *out, size_t capacity);
size_t pendingLogBytes(LogReader reader);
// Copy the newest retained whole lines (<= capacity bytes), oldest first.
size_t tailLog(char *out, size_t capacity);
void clearLog();
void setLoggingEnabled(bool enabled); // false: logf() is a no-op
bool loggingEnabled();
// Called by logf() when the file reader backlog crosses a threshold.
void setLogFileNotify(void (*notify)());
uint32_t droppedLogLines();      // USB reader: lines evicted before being sent
uint32_t droppedFileLogLines();  // File reader: lines evicted before being stored
uint32_t bufferedLogBytes();     // retained history in the ring
} // namespace ink
