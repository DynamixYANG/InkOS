#include "LyricsResolver.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
namespace ink {
namespace {
class DirectTransport : public LyricsTransport {
public:
  bool fetch(const String &url, HttpResponse &response, const RequestOptions &options) override {
    return request(url, response, 393216, "", "", options);
  }
  int64_t nowMs() const override { return esp_timer_get_time() / 1000; }
  void pause(unsigned ms) override { vTaskDelay(pdMS_TO_TICKS(ms)); }
};
LyricsResolver residentResolver;
DirectTransport transport;
LyricReport lastNormal;
LyricStatus lastStatus = LyricStatus::Waiting;
struct Probe {
  int fixture = -1, serial = 0;
  bool queued = false, running = false, complete = false;
  int lines = 0;
  LyricStatus status = LyricStatus::Waiting;
  LyricReport report;
} probe;
void addReport(cJSON *out, const LyricReport &r, LyricStatus status) {
  cJSON_AddStringToObject(out, "source", lyricProviderName(r.source));
  cJSON_AddNumberToObject(out, "lyric_status", int(status));
  cJSON_AddBoolToObject(out, "timed", status == LyricStatus::Timed);
  cJSON_AddNumberToObject(out, "requests", r.requests);
  cJSON_AddNumberToObject(out, "candidates", r.candidates);
  cJSON_AddNumberToObject(out, "match_score", r.score);
  cJSON_AddNumberToObject(out, "lrclib_http", r.lrclibHttp);
  cJSON_AddNumberToObject(out, "netease_http", r.neteaseHttp);
  cJSON_AddBoolToObject(out, "deferred", r.deferred);
}
}
void resolveTrackLyrics(Music &music, const char *lead) {
  LyricQuery query{music.title, lead, music.album, music.duration};
  auto report = residentResolver.resolve(query, music, transport);
  Guard g(modelMutex); lastNormal = report; lastStatus = music.lyricStatus;
}
cJSON *lyricsDiagnostics() {
  Guard g(modelMutex); auto out = cJSON_CreateObject();
  addReport(out, lastNormal, lastStatus); return out;
}
cJSON *lyricProbeCommand(cJSON *input) {
  Guard g(modelMutex); auto out = cJSON_CreateObject();
  auto fixture = cJSON_GetObjectItemCaseSensitive(input, "fixture");
  if (cJSON_IsNumber(fixture)) {
    bool accepted = fixture->valueint >= 0 && fixture->valueint <= 2 && !probe.queued && !probe.running;
    if (accepted) {
      int serial = probe.serial + 1; probe = Probe(); probe.serial = serial;
      probe.fixture = fixture->valueint; probe.queued = true;
    }
    cJSON_AddBoolToObject(out, "accepted", accepted);
  }
  cJSON_AddNumberToObject(out, "job", probe.serial);
  cJSON_AddNumberToObject(out, "fixture", probe.fixture);
  cJSON_AddBoolToObject(out, "queued", probe.queued);
  cJSON_AddBoolToObject(out, "running", probe.running);
  cJSON_AddBoolToObject(out, "complete", probe.complete);
  cJSON_AddNumberToObject(out, "lines", probe.lines);
  addReport(out, probe.report, probe.status); return out;
}
void serviceLyricProbe() {
  int fixture;
  { Guard g(modelMutex); if (!probe.queued) return; probe.queued = false; probe.running = true; fixture = probe.fixture; }
  auto music = (Music *)heap_caps_malloc(sizeof(Music), MALLOC_CAP_SPIRAM);
  LyricReport report; LyricStatus status = LyricStatus::Unavailable; int lines = 0;
  if (music) {
    new (music) Music();
    LyricQuery query = fixture == 0 ? LyricQuery{"Yellow", "Coldplay", "Parachutes", 267000} :
                                     LyricQuery{"倔強", "Mayday", "神的孩子都在跳舞", 261618};
    auto mode = fixture == 0 ? LyricLookupMode::LrclibSearchOnly :
                fixture == 1 ? LyricLookupMode::NeteaseOnly : LyricLookupMode::Automatic;
    report = residentResolver.resolve(query, *music, transport, mode);
    status = music->lyricStatus; lines = music->lineCount; free(music);
  }
  { Guard g(modelMutex); probe.running = false; probe.complete = true; probe.report = report; probe.status = status; probe.lines = lines; }
}
}
