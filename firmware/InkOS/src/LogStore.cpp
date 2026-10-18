#include "LogStore.h"
#include "CjkAssetsData.h"
#include "Log.h"
#include "Storage.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <vector>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <mbedtls/base64.h>
#include <miniz.h>
#include <time.h>
namespace ink {
namespace {
constexpr char Dir[] = "/logs", Current[] = "/logs/current.log", Temp[] = "/logs/.rotate.tmp";
constexpr size_t BatchBytes = 16384, ReadChunkMax = 2048, TailMax = 3072;
constexpr int ListMax = LogArchiveMaxFiles + 16, DeleteMax = 64;
constexpr uint32_t FlushMs = 15000, DaySeconds = 86400, BlockBytes = 4096;
TaskHandle_t writer = nullptr;
std::atomic<bool> clearRequested{false}, rotateRequested{false}, pruneRequested{true}, ready{false};
std::atomic<uint32_t> discarded{0}, rotations{0}, flushes{0}, pruned{0};
std::atomic<uint16_t> retention{LogRetentionDefaultDays};
std::atomic<const char *> lastError{""}; // Last rotation failure stage, for log_list.
SemaphoreHandle_t fsLock = nullptr; // Serialises writer vs. clear/delete/list/read/fs_info.
uint32_t nextSeq = 1, lastAgePrune = 0;
struct Lock {
  Lock() { xSemaphoreTake(fsLock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(fsLock); }
};
void notifyWriter() {
  if (writer)
    xTaskNotifyGive(writer);
}
uint32_t nowUnix() {
  time_t t = time(nullptr);
  return t >= time_t(ClockValidAfter) ? uint32_t(t) : 0;
}
uint32_t fsFree() {
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}
uint32_t blocks(uint32_t bytes) { return (bytes + BlockBytes - 1) / BlockBytes * BlockBytes; }
// Listing buffer in PSRAM: up to ~216 entries would not fit on task stacks.
struct Listing {
  LogFileInfo *items = (LogFileInfo *)heap_caps_malloc(sizeof(LogFileInfo) * ListMax, MALLOC_CAP_SPIRAM);
  int n = 0;
  ~Listing() { free(items); }
};
int listUnlocked(LogFileInfo *out, int max) {
  if (!out)
    return 0;
  File dir = LittleFS.open(Dir);
  if (!dir || !dir.isDirectory())
    return 0;
  int n = 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    const char *name = f.name();
    if (!f.isDirectory() && validLogName(name)) {
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
    logArchiveSeq(a.name, &x);
    logArchiveSeq(b.name, &y);
    return x < y;
  });
  return n;
}
// Rotation time: the file's mtime, else the gzip header MTIME written at rotation.
uint32_t archiveEnd(const LogFileInfo &f) {
  if (f.mtime >= ClockValidAfter)
    return f.mtime;
  char path[48];
  snprintf(path, sizeof path, "%s/%s", Dir, f.name);
  File in = LittleFS.open(path, "r");
  uint8_t h[8] = {};
  bool ok = in && in.read(h, sizeof h) == sizeof h && h[0] == 0x1f && h[1] == 0x8b;
  in.close();
  uint32_t t = ok ? uint32_t(h[4]) | uint32_t(h[5]) << 8 | uint32_t(h[6]) << 16 | uint32_t(h[7]) << 24 : 0;
  return t >= ClockValidAfter ? t : 0;
}
struct Budget {
  uint64_t total, used, logBytes;
  uint32_t budget;
};
Budget budgetFor(const LogFileInfo *files, int n) {
  Budget b{LittleFS.totalBytes(), LittleFS.usedBytes(), 0, 0};
  for (int i = 0; i < n; i++)
    b.logBytes += blocks(files[i].size);
  uint64_t nonLog = b.used > b.logBytes ? b.used - b.logBytes : 0;
  b.budget = logArchiveBudget(b.total, nonLog, LogFsReserveBytes);
  return b;
}
int pruneUnlocked() {
  Listing list;
  if (!list.items)
    return 0;
  list.n = listUnlocked(list.items, ListMax);
  struct Work {
    LogArchiveEntry entries[ListMax];
    int map[ListMax];
    bool remove[ListMax];
  };
  auto w = (Work *)heap_caps_malloc(sizeof(Work), MALLOC_CAP_SPIRAM);
  if (!w)
    return 0;
  auto entries = w->entries;
  auto map = w->map;
  auto remove = w->remove;
  int n = 0;
  for (int i = 0; i < list.n; i++) {
    uint32_t seq;
    if (!logArchiveSeq(list.items[i].name, &seq))
      continue;
    entries[n] = {seq, blocks(list.items[i].size), archiveEnd(list.items[i])};
    map[n++] = i;
  }
  Budget b = budgetFor(list.items, list.n);
  LogPruneLimits limits{nowUnix(), retention.load(), LogArchiveMaxFiles, b.budget, int64_t(fsFree()),
                        LogFsReserveBytes};
  int count = planLogPrune(entries, n, limits, remove);
  for (int i = 0; i < n; i++)
    if (remove[i]) {
      char path[48];
      snprintf(path, sizeof path, "%s/%s", Dir, list.items[map[i]].name);
      LittleFS.remove(path);
    }
  free(w);
  pruned += count;
  return count;
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
void rotate() {
  bool forced = rotateRequested.exchange(false);
  File cur = LittleFS.open(Current, "r");
  if (!cur)
    return;
  uint32_t size = cur.size();
  cur.close();
  if (size < (forced ? 1u : LogRotateBytes))
    return;
  pruneUnlocked();
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
  pruneUnlocked();
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
      pruneUnlocked();
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
  Listing list;
  list.n = listUnlocked(list.items, ListMax);
  for (int i = 0; i < list.n; i++) {
    char path[48];
    snprintf(path, sizeof path, "%s/%s", Dir, list.items[i].name);
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
    {
      Lock g;
      if (clearRequested.exchange(false))
        clearFiles();
      flush(batch);
      rotate();
      uint32_t now = nowUnix();
      // Boot / setting change: prune now. Daily age prune only while enabled.
      bool daily = loggingEnabled() && now && (!lastAgePrune || now - lastAgePrune >= DaySeconds);
      if (pruneRequested.exchange(false) || daily) {
        pruneUnlocked();
        if (now)
          lastAgePrune = now;
      }
    }
    // Disabled: no periodic wakeup and no flash writes until notified.
    uint32_t wait = loggingEnabled() ? pdMS_TO_TICKS(FlushMs) : portMAX_DELAY;
    ulTaskNotifyTake(pdTRUE, wait);
  }
}
void addBase64(cJSON *out, const uint8_t *data, size_t n) {
  size_t encoded = 0;
  std::vector<unsigned char> text(4 * ((n + 2) / 3) + 1);
  mbedtls_base64_encode(text.data(), text.size(), &encoded, data, n);
  text[encoded] = 0;
  cJSON_AddStringToObject(out, "data", (const char *)text.data());
}
cJSON *reply(bool ok, const char *message = "") {
  auto out = cJSON_CreateObject();
  cJSON_AddBoolToObject(out, "inkos", true);
  cJSON_AddBoolToObject(out, "ok", ok);
  if (*message)
    cJSON_AddStringToObject(out, "message", message);
  return out;
}
struct Usage {
  uint32_t bytes = 0, files = 0;
  void add(uint32_t b) { bytes += b, files++; }
};
void walk(File &dir, Usage &u, int depth) {
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) {
      if (depth < 3)
        walk(f, u, depth + 1);
    } else
      u.add(f.size());
    f.close();
  }
}
void addUsage(cJSON *parent, const char *key, const Usage &u) {
  auto o = cJSON_AddObjectToObject(parent, key);
  cJSON_AddNumberToObject(o, "bytes", u.bytes);
  cJSON_AddNumberToObject(o, "files", u.files);
}
cJSON *fsInfo() {
  auto out = reply(true);
  Usage logs, calendars, energy, other, root;
  auto dirs = cJSON_CreateArray();
  uint64_t total = 0, used = 0;
  Listing list;
  {
    Lock g;
    total = LittleFS.totalBytes();
    used = LittleFS.usedBytes();
    File top = LittleFS.open("/");
    for (File f = top ? top.openNextFile() : File(); f; f = top.openNextFile()) {
      const char *name = f.name();
      if (f.isDirectory()) {
        Usage u;
        walk(f, u, 1);
        auto d = cJSON_CreateObject();
        char path[40];
        snprintf(path, sizeof path, "/%s", name);
        cJSON_AddStringToObject(d, "path", path);
        cJSON_AddNumberToObject(d, "bytes", u.bytes);
        cJSON_AddNumberToObject(d, "files", u.files);
        cJSON_AddItemToArray(dirs, d);
        Usage &cat = !strcmp(name, "logs") ? logs : other;
        cat.bytes += u.bytes;
        cat.files += u.files;
      } else {
        uint32_t size = f.size();
        root.add(size);
        if (!strncmp(name, "feed", 4) || !strncmp(name, "agenda", 6))
          calendars.add(size);
        else if (!strncmp(name, "energy", 6))
          energy.add(size);
        else
          other.add(size);
      }
      f.close();
    }
    if (list.items)
      list.n = listUnlocked(list.items, ListMax);
  }
  auto rootDir = cJSON_CreateObject();
  cJSON_AddStringToObject(rootDir, "path", "/");
  cJSON_AddNumberToObject(rootDir, "bytes", root.bytes);
  cJSON_AddNumberToObject(rootDir, "files", root.files);
  cJSON_InsertItemInArray(dirs, 0, rootDir);
  auto fs = cJSON_AddObjectToObject(out, "littlefs");
  cJSON_AddNumberToObject(fs, "total_bytes", total);
  cJSON_AddNumberToObject(fs, "used_bytes", used);
  cJSON_AddNumberToObject(fs, "free_bytes", total > used ? total - used : 0);
  cJSON_AddNumberToObject(fs, "block_bytes", BlockBytes);
  cJSON_AddNumberToObject(fs, "reserve_bytes", LogFsReserveBytes);
  cJSON_AddStringToObject(fs, "device", "external SPI flash 4 MiB (extstore)");
  auto cats = cJSON_AddObjectToObject(out, "categories");
  addUsage(cats, "logs", logs);
  addUsage(cats, "calendars", calendars);
  addUsage(cats, "energy", energy);
  addUsage(cats, "other", other);
  cJSON_AddItemToObject(out, "dirs", dirs);
  Budget b = budgetFor(list.items, list.n);
  uint32_t current = 0, archives = 0, count = 0, oldest = 0, newest = 0;
  for (int i = 0; i < list.n; i++) {
    if (!logArchiveSeq(list.items[i].name, nullptr)) {
      current += list.items[i].size;
      continue;
    }
    archives += list.items[i].size;
    count++;
    uint32_t t = list.items[i].mtime >= ClockValidAfter ? list.items[i].mtime : 0;
    if (t && (!oldest || t < oldest))
      oldest = t;
    if (t > newest)
      newest = t;
  }
  auto lg = cJSON_AddObjectToObject(out, "log");
  cJSON_AddNumberToObject(lg, "current_bytes", current);
  cJSON_AddNumberToObject(lg, "archive_bytes", archives);
  cJSON_AddNumberToObject(lg, "archive_count", count);
  cJSON_AddNumberToObject(lg, "oldest_archive_time", oldest);
  cJSON_AddNumberToObject(lg, "newest_archive_time", newest);
  cJSON_AddNumberToObject(lg, "archive_budget_bytes", b.budget);
  cJSON_AddNumberToObject(lg, "archive_max_files", LogArchiveMaxFiles);
  cJSON_AddNumberToObject(lg, "budget_percent", LogBudgetPercent);
  cJSON_AddNumberToObject(lg, "rotate_bytes", LogRotateBytes);
  cJSON_AddNumberToObject(lg, "retention_days", retention.load());
  cJSON_AddBoolToObject(lg, "clock_valid", nowUnix() != 0);
  cJSON_AddNumberToObject(lg, "pruned", pruned.load());
  auto fonts = cJSON_AddObjectToObject(out, "fonts");
  auto p = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, "fonts");
  cJSON_AddStringToObject(fonts, "device", "internal flash partition 'fonts' (not LittleFS)");
  cJSON_AddNumberToObject(fonts, "partition_bytes", p ? p->size : 0);
  cJSON_AddNumberToObject(fonts, "used_bytes", FONT_ARCHIVE_BYTES);
  return out;
}
cJSON *deleteLogs(cJSON *j) {
  auto one = cJSON_GetObjectItem(j, "name"), many = cJSON_GetObjectItem(j, "names");
  std::vector<const char *> requested;
  if (cJSON_IsString(one))
    requested.push_back(one->valuestring);
  if (cJSON_IsArray(many)) {
    cJSON *it;
    cJSON_ArrayForEach(it, many) {
      if (!cJSON_IsString(it))
        return reply(false, "names must be strings");
      requested.push_back(it->valuestring);
    }
  }
  if (requested.empty() || requested.size() > DeleteMax)
    return reply(false, "Give name or names (1-64 log files)");
  // Validate everything before deleting anything.
  std::vector<std::array<char, 32>> names(requested.size());
  for (size_t i = 0; i < requested.size(); i++)
    if (!normalizeLogName(requested[i], names[i].data(), names[i].size()))
      return reply(false, "Only /logs/current.log and /logs/inkos-NNNNNN.tar.gz can be deleted");
  auto out = reply(true);
  auto deleted = cJSON_AddArrayToObject(out, "deleted"), missing = cJSON_AddArrayToObject(out, "missing");
  {
    Lock g; // Writer is idle while we hold this: current.log can be removed safely;
            // the next flush re-creates it with the lines still pending in RAM.
    for (auto &name : names) {
      char path[48];
      snprintf(path, sizeof path, "%s/%s", Dir, name.data());
      bool existed = LittleFS.exists(path);
      if (existed && LittleFS.remove(path))
        cJSON_AddItemToArray(deleted, cJSON_CreateString(name.data()));
      else
        cJSON_AddItemToArray(missing, cJSON_CreateString(name.data()));
    }
  }
  return out;
}
} // namespace
void startLogStore() {
  if (writer || !storageHealthy())
    return;
  fsLock = xSemaphoreCreateMutex();
  if (!LittleFS.exists(Dir))
    LittleFS.mkdir(Dir);
  LittleFS.remove(Temp);
  Listing list;
  list.n = listUnlocked(list.items, ListMax);
  for (int i = 0; i < list.n; i++) {
    uint32_t seq;
    if (logArchiveSeq(list.items[i].name, &seq))
      nextSeq = std::max(nextSeq, seq + 1);
  }
  setLogFileNotify(notifyWriter);
  ready = true;
  pruneRequested = true; // Boot prune (size caps; age too if the clock is already valid).
  xTaskCreatePinnedToCore(writerTask, "log-store", 12288, nullptr, 1, &writer, 0);
}
void applyLoggingEnabled(bool on) {
  if (!on && loggingEnabled())
    logf("INKOS logging disabled\n");
  setLoggingEnabled(on);
  if (on)
    logf("INKOS logging enabled\n");
  pruneRequested = true;
  notifyWriter(); // Flushes pending lines; blocks indefinitely when disabled.
}
void setLogRetentionDays(uint16_t days) {
  if (!validRetentionDays(days))
    return;
  uint16_t was = retention.exchange(days);
  if (was != days && ready) {
    logf("INKOS log retention days=%u\n", unsigned(days));
    pruneRequested = true;
    notifyWriter();
  }
}
uint16_t logRetentionDays() { return retention.load(); }
cJSON *logCommand(const char *cmd, cJSON *j) {
  if (!strcmp(cmd, "fs_info"))
    return ready ? fsInfo() : reply(false, "Log storage unavailable");
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
    cJSON_AddNumberToObject(out, "retention_days", retention.load());
    cJSON_AddNumberToObject(out, "pruned", pruned.load());
    cJSON_AddStringToObject(out, "rotate_error", lastError.load());
    auto files = cJSON_AddArrayToObject(out, "files");
    if (ready) {
      Listing list;
      {
        Lock g;
        list.n = listUnlocked(list.items, ListMax);
      }
      for (int i = 0; i < list.n; i++) {
        auto f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "name", list.items[i].name);
        cJSON_AddNumberToObject(f, "size", list.items[i].size);
        cJSON_AddNumberToObject(f, "mtime", list.items[i].mtime);
        cJSON_AddItemToArray(files, f);
      }
      cJSON_AddNumberToObject(out, "fs_free_bytes", fsFree());
    }
    return out;
  }
  if (!strcmp(cmd, "log_read")) {
    auto name = cJSON_GetObjectItem(j, "name"), offset = cJSON_GetObjectItem(j, "offset"),
         length = cJSON_GetObjectItem(j, "length");
    char bare[32];
    if (!cJSON_IsString(name) || !normalizeLogName(name->valuestring, bare, sizeof bare) ||
        !cJSON_IsNumber(offset) || offset->valuedouble < 0)
      return reply(false, "Invalid log name or offset");
    size_t want = cJSON_IsNumber(length) && length->valueint > 0
                      ? std::min<size_t>(length->valueint, ReadChunkMax)
                      : ReadChunkMax;
    if (!ready)
      return reply(false, "Log storage unavailable");
    char path[48];
    snprintf(path, sizeof path, "%s/%s", Dir, bare);
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
    cJSON_AddStringToObject(out, "name", bare);
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
    rotateRequested = true;
    notifyWriter();
    return reply(true);
  }
  if (!strcmp(cmd, "log_delete")) {
    if (!ready)
      return reply(false, "Log storage unavailable");
    return deleteLogs(j);
  }
  if (!strcmp(cmd, "log_clear")) {
    clearLog();
    auto files = cJSON_GetObjectItem(j, "files");
    bool removeFiles = !files || cJSON_IsTrue(files);
    if (removeFiles && ready) {
      clearRequested = true;
      notifyWriter();
    }
    return reply(true);
  }
  return nullptr;
}
} // namespace ink
