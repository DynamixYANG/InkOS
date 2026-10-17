#pragma once
#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>
namespace ink {
// Persistent diagnostics on the external LittleFS (see Log.h for the ring).
// Writer task: core 0, prio 1; never touches frameMutex or the panel.
//   /logs/current.log          appended in batches (>= 8 KiB pending or 15 s)
//   /logs/inkos-NNNNNN.tar.gz  rotated at 64 KiB: ustar with one .log, gzip
//                              (ROM miniz raw deflate + CRC32), streaming.
// Retention: newest 16 archives and <= 512 KiB of archives; logging pauses if
// filesystem free space would drop below 512 KiB (calendar/energy caches).
constexpr uint32_t LogRotateBytes = 65536, LogArchiveLimit = 16,
                   LogArchiveBytes = 524288, LogFsReserveBytes = 524288;
void startLogStore();               // after ensureStorage(), before tasks log
void applyLoggingEnabled(bool on);  // runtime toggle; flushes on disable
cJSON *logCommand(const char *cmd, cJSON *j); // nullptr if not a log command
struct LogFileInfo {
  char name[32];
  uint32_t size, mtime;
};
// Newest last. Used by remote listing and the USB mass-storage snapshot.
int listLogFiles(LogFileInfo *out, int max);
bool readLogFile(const char *name, uint32_t offset, uint8_t *out, size_t n);
// While a USB host has the read-only log disk mounted, rotation/deletion pause
// so the presented snapshot stays readable.
void setLogHostActive(bool active);
} // namespace ink
