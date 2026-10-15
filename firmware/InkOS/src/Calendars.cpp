#include "ICalendar.h"
#include "Services.h"
#include "Zones.h"
#include <LittleFS.h>
#include <esp_heap_caps.h>
namespace ink {
static bool storeReady = false;
static bool storage() {
  if (!storeReady)
    storeReady = LittleFS.begin(true, "/inkstore", 5, "storage");
  return storeReady;
}
static uint32_t key(const String &s) {
  uint32_t h = 2166136261u;
  for (unsigned i = 0; i < s.length(); i++)
    h = (h ^ uint8_t(s[i])) * 16777619u;
  return h;
}
struct CacheHeader {
  uint32_t magic, size, checksum;
};
static uint32_t bytesHash(const void *v, size_t n) {
  auto p = (const uint8_t *)v;
  uint32_t h = 2166136261u;
  while (n--)
    h = (h ^ *p++) * 16777619u;
  return h;
}
static bool saveBlob(const char *path, const void *p, size_t n) {
  String tmp = String(path) + ".next";
  File file = LittleFS.open(tmp, "w");
  if (!file)
    return false;
  bool ok = file.write((const uint8_t *)p, n) == n;
  file.flush();
  file.close();
  return ok && LittleFS.rename(tmp, path);
}
void loadAgenda() {
  auto cfg = credentials();
  bool enabled = false;
  for (auto &f : cfg.feeds)
    enabled |= f.enabled;
  if (!enabled || !storage())
    return;
  File file = LittleFS.open("/agenda.bin", "r");
  if (!file)
    return;
  CacheHeader h;
  if (file.readBytes((char *)&h, sizeof h) != sizeof h || h.magic != 0x32414749 ||
      h.size != sizeof(Agenda))
    return;
  auto a = (Agenda *)heap_caps_malloc(sizeof(Agenda), MALLOC_CAP_SPIRAM);
  if (!a)
    return;
  bool ok = file.readBytes((char *)a, sizeof(Agenda)) == sizeof(Agenda) &&
            bytesHash(a, sizeof(Agenda)) == h.checksum && a->count >= 0 && a->count <= MAX_EVENTS;
  if (ok) {
    a->stale = true;
    a->revision++;
    model->agenda = *a;
  }
  free(a);
}
static bool readCache(int i, uint32_t source, HttpResponse &r, RequestOptions &options) {
  char meta[32], body[32];
  snprintf(meta, sizeof meta, "/feed%d.meta", i);
  snprintf(body, sizeof body, "/feed%d.ics", i);
  File m = LittleFS.open(meta, "r");
  if (!m)
    return false;
  String raw = m.readString();
  m.close();
  auto j = cJSON_Parse(raw.c_str());
  if (!j)
    return false;
  auto k = cJSON_GetObjectItem(j, "key");
  bool match = cJSON_IsNumber(k) && uint32_t(k->valuedouble) == source;
  if (match) {
    auto e = cJSON_GetObjectItem(j, "etag"), l = cJSON_GetObjectItem(j, "modified");
    if (cJSON_IsString(e))
      options.etag = e->valuestring;
    if (cJSON_IsString(l))
      options.lastModified = l->valuestring;
  }
  cJSON_Delete(j);
  if (!match)
    return false;
  File f = LittleFS.open(body, "r");
  if (!f || f.size() > 524288)
    return false;
  r.capacity = f.size();
  r.bytes = (uint8_t *)heap_caps_malloc(r.capacity + 1, MALLOC_CAP_SPIRAM);
  if (!r.bytes)
    return false;
  r.length = f.read(r.bytes, r.capacity);
  r.bytes[r.length] = 0;
  return r.length == r.capacity;
}
static void writeCache(int i, uint32_t source, const HttpResponse &r) {
  char meta[32], body[32];
  snprintf(meta, sizeof meta, "/feed%d.meta", i);
  snprintf(body, sizeof body, "/feed%d.ics", i);
  if (!saveBlob(body, r.bytes, r.length))
    return;
  auto j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "key", source);
  cJSON_AddStringToObject(j, "etag", r.etag.c_str());
  cJSON_AddStringToObject(j, "modified", r.lastModified.c_str());
  char *raw = cJSON_PrintUnformatted(j);
  if (raw) {
    saveBlob(meta, raw, strlen(raw));
    free(raw);
  }
  cJSON_Delete(j);
}
void updateCalendars() {
  auto cfg = credentials();
  auto result = (Agenda *)heap_caps_calloc(1, sizeof(Agenda), MALLOC_CAP_SPIRAM);
  if (!result)
    return;
  new (result) Agenda();
  bool any = false;
  for (auto &f : cfg.feeds)
    any |= f.enabled;
  if (!any) {
    Guard g(modelMutex);
    result->revision = model->agenda.revision + 1;
    model->agenda = *result;
    free(result);
    invalidate();
    return;
  }
  result->configured = true;
  if (!storage()) {
    free(result);
    Guard g(modelMutex);
    model->agenda.stale = true;
    strlcpy(model->agenda.error, "Calendar storage unavailable", sizeof model->agenda.error);
    return;
  }
  auto local = inZone(time(nullptr), cfg.settings.localZone);
  time_t wall = Clock::epoch(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) -
                ((local.tm_wday + 6) % 7) * 86400;
  time_t begin = fromZone(wall, cfg.settings.localZone),
         end = fromZone(wall + 35 * 86400, cfg.settings.localZone);
  bool good = true;
  for (int i = 0; i < 3; i++) {
    auto &feed = cfg.feeds[i];
    if (!feed.enabled || feed.url.isEmpty())
      continue;
    RequestOptions options;
    options.redirects = true;
    HttpResponse cached, remote;
    bool has = readCache(i, key(feed.url), cached, options);
    bool online = request(feed.url, remote, 524288, "", "", options);
    const HttpResponse *body = nullptr;
    if (online && remote.status == 200)
      body = &remote;
    else if (has) {
      body = &cached;
      result->stale |= !(online && remote.status == 304);
    }
    if (!body) {
      good = false;
      snprintf(result->error, sizeof result->error, "Calendar %d HTTP %d; retrying", i + 1,
               remote.status);
      break;
    }
    if (!parseCalendar((const char *)body->bytes, cfg.settings.localZone, begin, end, *result)) {
      good = false;
      break;
    }
    if (body == &remote)
      writeCache(i, key(feed.url), remote);
  }
  if (cfg.revision != credentials().revision) {
    free(result);
    return;
  }
  if (good) {
    result->imported = time(nullptr);
    result->validUntil = end;
    {
      Guard g(modelMutex);
      result->revision = model->agenda.revision + 1;
      model->agenda = *result;
    }
    CacheHeader h = {0x32414749, sizeof(Agenda), bytesHash(result, sizeof(Agenda))};
    File f = LittleFS.open("/agenda.bin.next", "w");
    if (f) {
      bool ok = f.write((uint8_t *)&h, sizeof h) == sizeof h &&
                f.write((uint8_t *)result, sizeof(Agenda)) == sizeof(Agenda);
      f.flush();
      f.close();
      if (ok)
        LittleFS.rename("/agenda.bin.next", "/agenda.bin");
    }
    Serial.printf("INKOS calendar events=%d stale=%d truncated=%d\n", result->count, result->stale,
                  result->truncated);
  } else {
    Guard g(modelMutex);
    model->agenda.configured = true;
    model->agenda.stale = true;
    strlcpy(model->agenda.error, *result->error ? result->error : "Calendar sync failed",
            sizeof model->agenda.error);
    model->agenda.revision++;
  }
  free(result);
  invalidate();
}
} // namespace ink
