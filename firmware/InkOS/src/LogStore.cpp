#include "LogStore.h"
#include "Log.h"
#include "Storage.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <algorithm>
#include <atomic>
#include <vector>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <miniz.h>
#include <time.h>
namespace ink {
namespace {
constexpr char Dir[] = "/logs", Current[] = "/logs/current.log", Temp[] = "/logs/.rotate.tmp";
constexpr size_t BatchBytes = 16384, ReadChunkMax = 2048, TailMax = 3072;
constexpr uint32_t FlushMs = 15000;
TaskHandle_t writer = nullptr;
std::atomic<bool> hostActive{false}, clearRequested{false}, ready{false};
std::atomic<uint32_t> discarded{0}, rotations{0}, flushes{0};
SemaphoreHandle_t fsLock = nullptr; // Serialises writer vs. clear/list/read.
uint32_t nextSeq = 1;
std::atomic<const char *> lastError{""}; // Last rotation failure stage, for log_list.
struct Lock {
  Lock() { xSemaphoreTake(fsLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(fsLock); }
};
void notifyWriter() {
  if (writer)
    xTaskNotifyGive(writer);
}
bool archiveName(const char *name, uint32_t *seq) {
  unsigned v = 0;
  int consumed = 0;
  if (sscanf(name, "inkos-%6u.tar.gz%n", &v, &consumed) != 1 || name[consumed])
    return false;
  if (seq)
    *seq = v;
  return true;
}
bool validName(const char *name) {
  return !strcmp(name, "current.log") || archiveName(name, nullptr);
}
uint32_t fsFree() {
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}
int listUnlocked(LogFileInfo *out, int max) {
  File dir = LittleFS.open(Dir);
  if (!dir || !dir.isDirectory())
    return 0;
  int n = 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    const char *name = f.name();
    if (!f.isDirectory() && validName(name)) {
      strlcpy(out[n].name, name, sizeof out[n].name);
      out[n].size = f.size();
      out[n].mtime = f.getLastWrite();
      n++;
    }
    f.close();
  }
  // Archives by sequence, current.log last.
  std::sort(out, out + n, [](const LogFileInfo &a, const LogFileInfo &b) {
    uint32_t x = UINT32_MAX, y = UINT32_MAX;
    archiveName(a.name, &x);
    archiveName(b.name, &y);
    return x < y;
  });
  return n;
}
void enforceRetention() {
  if (hostActive)
    return;
  LogFileInfo files[LogArchiveLimit + 8];
  int n = listUnlocked(files, LogArchiveLimit + 8), archives = 0;
  uint32_t bytes = 0;
  for (int i = 0; i < n; i++)
    if (archiveName(files[i].name, nullptr)) {
      archives++;
      bytes += files[i].size;
    }
  for (int i = 0; i < n && (archives > int(LogArchiveLimit) || bytes > LogArchiveBytes ||
                            (archives && fsFree() < LogFsReserveBytes));
       i++) {
    if (!archiveName(files[i].name, nullptr))
      continue;
    char path[48];
    snprintf(path, sizeof path, "%s/%s", Dir, files[i].name);
    LittleFS.remove(path);
    archives--;
    bytes -= files[i].size;
  }
}
// ---- tar.gz writer (bounded RAM, compressor state in PSRAM) ----
struct GzOut {
  File *file;
  bool ok;
};
mz_bool putBuffer(const void *buf, int len, void *user) {
  auto out = static_cast<GzOut *>(user);
  if (out->ok && out->file->write(static_cast<const uint8_t *>(buf), len) != size_t(len))
    out->ok = false;
  return out->ok;
}
struct Deflater {
  tdefl_compressor *d;
  GzOut out;
  uint32_t crc = 0, size = 0;
  bool feed(const void *p, size_t n, tdefl_flush flush = TDEFL_NO_FLUSH) {
    crc = mz_crc32(crc, static_cast<const uint8_t *>(p), n);
    size += n;
    auto status = tdefl_compress_buffer(d, p, n, flush);
    return out.ok && (status == TDEFL_STATUS_OKAY || status == TDEFL_STATUS_DONE);
  }
};
void octal(char *field, size_t width, uint32_t value) {
  snprintf(field, width, "%0*lo", int(width - 1), (unsigned long)value);
}
bool compress(const char *source, uint32_t seq, File &dest) {
  File in = LittleFS.open(source, "r");
  if (!in) {
    lastError = "open source";
    return false;
  }
  uint32_t length = in.size(), mtime = time(nullptr) > 1600000000 ? uint32_t(time(nullptr)) : 0;
  auto d = (tdefl_compressor *)heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM);
  auto buffer = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  bool ok = d && buffer;
  if (!ok)
    lastError = "alloc";
  Deflater z{d, {&dest, true}};
  if (ok) {
    const uint8_t gzip[10] = {0x1f, 0x8b, 8, 0, uint8_t(mtime), uint8_t(mtime >> 8),
                              uint8_t(mtime >> 16), uint8_t(mtime >> 24), 0, 3};
    ok = dest.write(gzip, sizeof gzip) == sizeof gzip &&
         tdefl_init(d, putBuffer, &z.out, 128) == TDEFL_STATUS_OKAY;
    if (!ok)
      lastError = "init";
  }
  if (ok) {
    char header[512] = {};
    snprintf(header, 100, "inkos-%06lu.log", (unsigned long)seq);
    octal(header + 100, 8, 0644);
    octal(header + 108, 8, 0);
    octal(header + 116, 8, 0);
    octal(header + 124, 12, length);
    octal(header + 136, 12, mtime);
    memset(header + 148, ' ', 8);
    header[156] = '0';
    memcpy(header + 257, "ustar", 6);
    memcpy(header + 263, "00", 2);
    strlcpy(header + 265, "inkos", 32);
    strlcpy(header + 297, "inkos", 32);
    unsigned sum = 0;
    for (char c : header)
      sum += uint8_t(c);
    snprintf(header + 148, 8, "%06o", sum);
    header[155] = ' ';
    ok = z.feed(header, sizeof header);
    if (!ok)
      lastError = "header";
  }
  uint32_t copied = 0;
  while (ok && copied < length) {
    size_t n = in.read(buffer, std::min<size_t>(4096, length - copied));
    if (!n) {
      lastError = "read";
      ok = false;
      break;
    }
    ok = z.feed(buffer, n);
    if (!ok)
      lastError = "deflate";
    copied += n;
    vTaskDelay(1); // Stay polite on core 0.
  }
  if (ok) {
    static const uint8_t zeros[512] = {};
    size_t pad = (512 - length % 512) % 512;
    ok = (!pad || z.feed(zeros, pad)) && z.feed(zeros, 512) && z.feed(zeros, 512, TDEFL_FINISH);
    if (!ok)
      lastError = "finish";
  }
  if (ok) {
    uint8_t trailer[8];
    for (int i = 0; i < 4; i++) {
      trailer[i] = uint8_t(z.crc >> (8 * i));
      trailer[4 + i] = uint8_t(z.size >> (8 * i));
    }
    ok = dest.write(trailer, 8) == 8;
    if (!ok)
      lastError = "trailer";
  }
  in.close();
  free(buffer);
  free(d);
  return ok;
}
std::atomic<bool> rotateRequested{false};
void rotate() {
  if (hostActive)
    return;
  bool forced = rotateRequested.exchange(false);
  File cur = LittleFS.open(Current, "r");
  if (!cur)
    return;
  uint32_t size = cur.size();
  cur.close();
  if (size < (forced ? 1u : LogRotateBytes))
    return;
  enforceRetention();
  if (fsFree() < LogFsReserveBytes + size) {
    lastError = "space";
    return;
  }
  uint32_t started = millis();
  File out = LittleFS.open(Temp, "w");
  if (!out)
    lastError = "open temp";
  bool ok = out && compress(Current, nextSeq, out);
  uint32_t packed = out ? out.position() : 0;
  if (out)
    out.close();
  char path[48];
  snprintf(path, sizeof path, "%s/inkos-%06lu.tar.gz", Dir, (unsigned long)nextSeq);
  bool renamed = ok && LittleFS.rename(Temp, path);
  if (ok && !renamed)
    lastError = "rename";
  if (renamed) {
    LittleFS.remove(Current);
    lastError = "";
    logf("INKOS log rotated %s bytes=%lu packed=%lu ms=%lu\n", path + 6, (unsigned long)size,
         (unsigned long)packed, (unsigned long)(millis() - started));
    nextSeq++;
    rotations++;
  } else {
    logf("INKOS log rotation failed stage=%s bytes=%lu\n", lastError.load(), (unsigned long)size);
    LittleFS.remove(Temp);
    // Never let a failing rotation grow current.log without bound.
    if (size > 2 * LogRotateBytes)
      LittleFS.remove(Current);
  }
  enforceRetention();
}
void flush(char *batch) {
  if (!pendingLogBytes(LogReader::File))
    return;
  File out;
  for (;;) {
    size_t n = takeLogLines(LogReader::File, batch, BatchBytes);
    if (!n)
      break;
    if (fsFree() < LogFsReserveBytes) {
      enforceRetention();
      if (fsFree() < LogFsReserveBytes) {
        discarded++;
        continue; // Never fill the shared filesystem with logs.
      }
    }
    if (!out)
      out = LittleFS.open(Current, "a");
    if (!out || out.write(reinterpret_cast<uint8_t *>(batch), n) != n) {
      discarded++;
      break;
    }
  }
  if (out) {
    out.close();
    flushes++;
  }
}
void clearFiles() {
  LogFileInfo files[LogArchiveLimit + 8];
  int n = listUnlocked(files, LogArchiveLimit + 8);
  for (int i = 0; i < n; i++) {
    char path[48];
    snprintf(path, sizeof path, "%s/%s", Dir, files[i].name);
    LittleFS.remove(path);
  }
  LittleFS.remove(Temp);
}
void writerTask(void *) {
  auto batch = (char *)heap_caps_malloc(BatchBytes, MALLOC_CAP_SPIRAM);
  if (!batch) {
    vTaskDelete(nullptr);
    return;
  }
  for (;;) {
    // Disabled: no periodic wakeup and no flash writes until re-enabled.
    uint32_t wait = loggingEnabled() ? pdMS_TO_TICKS(FlushMs) : portMAX_DELAY;
    ulTaskNotifyTake(pdTRUE, wait);
    Lock g;
    if (clearRequested.exchange(false))
      clearFiles();
    flush(batch);
    rotate();
  }
}
} // namespace
void startLogStore() {
  if (writer || !storageHealthy())
    return;
  fsLock = xSemaphoreCreateMutex();
  if (!LittleFS.exists(Dir))
    LittleFS.mkdir(Dir);
  LittleFS.remove(Temp);
  LogFileInfo files[LogArchiveLimit + 8];
  int n = listUnlocked(files, LogArchiveLimit + 8);
  for (int i = 0; i < n; i++) {
    uint32_t seq;
    if (archiveName(files[i].name, &seq))
      nextSeq = std::max(nextSeq, seq + 1);
  }
  setLogFileNotify(notifyWriter);
  ready = true;
  xTaskCreatePinnedToCore(writerTask, "log-store", 8192, nullptr, 1, &writer, 0);
}
void applyLoggingEnabled(bool on) {
  if (!on && loggingEnabled())
    logf("INKOS logging disabled\n");
  setLoggingEnabled(on);
  if (on)
    logf("INKOS logging enabled\n");
  notifyWriter(); // Flushes pending lines; blocks indefinitely when disabled.
}
int listLogFiles(LogFileInfo *out, int max) {
  if (!ready)
    return 0;
  Lock g;
  return listUnlocked(out, max);
}
bool readLogFile(const char *name, uint32_t offset, uint8_t *out, size_t n) {
  if (!ready || !validName(name))
    return false;
  char path[48];
  snprintf(path, sizeof path, "%s/%s", Dir, name);
  File f = LittleFS.open(path, "r");
  if (!f || offset > f.size() || !f.seek(offset))
    return false;
  size_t got = f.read(out, n);
  f.close();
  if (got < n)
    memset(out + got, 0, n - got);
  return true;
}
void setLogHostActive(bool active) {
  hostActive = active;
  if (!active)
    notifyWriter(); // Catch up on rotation/retention deferred while mounted.
}
static cJSON *reply(bool ok, const char *message = "") {
  auto out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "inkos", true);
  cJSON_AddBoolToObject(out, "ok", ok);
  if (*message)
    cJSON_AddStringToObject(out, "message", message);
  return out;
}
static void addBase64(cJSON *out, const uint8_t *data, size_t n) {
  size_t encoded = 0;
  std::vector<unsigned char> text(4 * ((n + 2) / 3) + 1);
  mbedtls_base64_encode(text.data(), text.size(), &encoded, data, n);
  text[encoded] = 0;
  cJSON_AddStringToObject(out, "data", (const char *)text.data());
}
cJSON *logCommand(const char *cmd, cJSON *j) {
  if (strncmp(cmd, "log_", 4))
    return nullptr;
  if (!strcmp(cmd, "log_list")) {
    auto out = reply(true);
    cJSON_AddBoolToObject(out, "enabled", loggingEnabled());
    cJSON_AddBoolToObject(out, "storage", ready.load());
    cJSON_AddNumberToObject(out, "ring_bytes", bufferedLogBytes());
    cJSON_AddNumberToObject(out, "pending_bytes", pendingLogBytes(LogReader::File));
    cJSON_AddNumberToObject(out, "dropped", droppedLogLines());
    cJSON_AddNumberToObject(out, "file_dropped", droppedFileLogLines());
    cJSON_AddNumberToObject(out, "discarded_batches", discarded.load());
    cJSON_AddNumberToObject(out, "rotations", rotations.load());
    cJSON_AddNumberToObject(out, "rotate_bytes", LogRotateBytes);
    cJSON_AddBoolToObject(out, "host_active", hostActive.load());
    cJSON_AddStringToObject(out, "rotate_error", lastError.load());
    auto files = cJSON_AddArrayToObject(out, "files");
    LogFileInfo list[LogArchiveLimit + 8];
    int n = listLogFiles(list, LogArchiveLimit + 8);
    for (int i = 0; i < n; i++) {
      auto f = cJSON_CreateObject();
      cJSON_AddStringToObject(f, "name", list[i].name);
      cJSON_AddNumberToObject(f, "size", list[i].size);
      cJSON_AddNumberToObject(f, "mtime", list[i].mtime);
      cJSON_AddItemToArray(files, f);
    }
    if (ready) {
      cJSON_AddNumberToObject(out, "fs_free_bytes", fsFree());
    }
    return out;
  }
  if (!strcmp(cmd, "log_read")) {
    auto name = cJSON_GetObjectItem(j, "name"), offset = cJSON_GetObjectItem(j, "offset"),
         length = cJSON_GetObjectItem(j, "length");
    if (!cJSON_IsString(name) || !validName(name->valuestring) || !cJSON_IsNumber(offset) ||
        offset->valuedouble < 0)
      return reply(false, "Invalid log name or offset");
    size_t want = cJSON_IsNumber(length) && length->valueint > 0
                      ? std::min<size_t>(length->valueint, ReadChunkMax)
                      : ReadChunkMax;
    if (!ready)
      return reply(false, "Log storage unavailable");
    char path[48];
    snprintf(path, sizeof path, "%s/%s", Dir, name->valuestring);
    std::vector<uint8_t> data(want);
    size_t got = 0, size = 0;
    {
      Lock g;
      File f = LittleFS.open(path, "r");
      if (!f)
        return reply(false, "No such log file");
      size = f.size();
      uint32_t at = uint32_t(offset->valuedouble);
      if (at < size && f.seek(at))
        got = f.read(data.data(), std::min<size_t>(want, size - at));
      f.close();
    }
    auto out = reply(true);
    cJSON_AddStringToObject(out, "name", name->valuestring);
    cJSON_AddNumberToObject(out, "offset", offset->valuedouble);
    cJSON_AddNumberToObject(out, "length", got);
    cJSON_AddNumberToObject(out, "size", size);
    cJSON_AddBoolToObject(out, "eof", uint32_t(offset->valuedouble) + got >= size);
    addBase64(out, data.data(), got);
    return out;
  }
  if (!strcmp(cmd, "log_tail")) {
    auto bytes = cJSON_GetObjectItem(j, "bytes");
    size_t want = cJSON_IsNumber(bytes) && bytes->valueint > 0
                      ? std::min<size_t>(bytes->valueint, TailMax)
                      : TailMax;
    std::vector<uint8_t> data(want);
    size_t n = tailLog(reinterpret_cast<char *>(data.data()), want);
    auto out = reply(true);
    cJSON_AddBoolToObject(out, "enabled", loggingEnabled());
    cJSON_AddNumberToObject(out, "length", n);
    addBase64(out, data.data(), n);
    return out;
  }
  if (!strcmp(cmd, "log_rotate")) { // Flush now and archive current.log regardless of size.
    if (!ready)
      return reply(false, "Log storage unavailable");
    if (hostActive)
      return reply(false, "Log disk is mounted by a USB host; rotation is paused");
    rotateRequested = true;
    notifyWriter();
    return reply(true);
  }
  if (!strcmp(cmd, "log_clear")) {
    clearLog();
    auto files = cJSON_GetObjectItem(j, "files");
    bool removeFiles = !files || cJSON_IsTrue(files);
    if (removeFiles && hostActive)
      return reply(false, "Log disk is mounted by a USB host; RAM log cleared only");
    if (removeFiles && ready) {
      clearRequested = true;
      notifyWriter();
    }
    return reply(true);
  }
  return nullptr;
}
} // namespace ink
