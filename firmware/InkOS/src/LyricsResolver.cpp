#include "LyricsResolver.h"
#include "Lyrics.h"
#include <algorithm>
namespace ink {
namespace {
const cJSON *field(const cJSON *j, const char *key) { return cJSON_GetObjectItemCaseSensitive(j, key); }
const char *text(const cJSON *j, const char *key) {
  auto v = field(j, key); return cJSON_IsString(v) ? v->valuestring : "";
}
double number(const cJSON *j, const char *key, double fallback = -1) {
  auto v = field(j, key); return cJSON_IsNumber(v) ? v->valuedouble : fallback;
}
struct Json {
  cJSON *value = nullptr;
  ~Json() { cJSON_Delete(value); }
};
struct Lookup {
  const LyricQuery &query;
  Music &music;
  LyricsTransport &transport;
  LyricReport report;
  int64_t deadline;
  bool failed = false, untimed = false, instrumental = false;
  bool get(const String &url, bool netease, int64_t &until, Json &out) {
    int64_t now = transport.nowMs();
    if (now < until || now + 1000 > deadline) { report.deferred = true; return false; }
    if (report.requests) transport.pause(300);
    HttpResponse response; RequestOptions options;
    options.userAgent = "InkOS/0.4.3 (https://github.com/DynamixYANG/InkOS)";
    options.timeoutMs = std::max(1000, int(std::min<int64_t>(5000, deadline - transport.nowMs())));
    if (netease) { options.headerName = "Referer"; options.headerValue = "https://music.163.com/"; }
    report.requests++;
    bool ok = transport.fetch(url, response, options);
    (netease ? report.neteaseHttp : report.lrclibHttp) = response.status;
    if (response.status == 429) {
      until = transport.nowMs() + int64_t(lyricRetrySeconds(response.retryAfter, time(nullptr))) * 1000;
      report.deferred = true; failed = true; return false;
    }
    if (!ok || response.status != 200) {
      if (response.status != 404) { until = transport.nowMs() + 30000; failed = true; }
      return false;
    }
    out.value = cJSON_Parse((const char *)response.bytes);
    if (!out.value) { failed = true; until = transport.nowMs() + 30000; }
    return out.value != nullptr;
  }
  bool timed(const char *lrc, LyricProvider provider, int score) {
    if (!*lrc) return false;
    parseTimedLyrics(lrc, music);
    int kept = 0, visible = 0;
    for (int i = 0; i < music.lineCount; i++) {
      const auto &line = music.lines[i];
      if (line.atMs > query.durationMs + 5000) continue;
      if (kept != i) music.lines[kept] = line;
      visible += music.lines[kept].text[0] != 0; kept++;
    }
    music.lineCount = visible ? kept : 0;
    if (!visible) { untimed = true; return false; }
    music.lyricStatus = LyricStatus::Timed; report.source = provider; report.score = score;
    return true;
  }
  bool lrclib(const cJSON *root, LyricProvider source) {
    if (!cJSON_IsArray(root) && (!cJSON_IsObject(root) || !cJSON_IsString(field(root, "trackName")))) {
      failed = true; return false;
    }
    const cJSON *best[3] = {}; int scores[3] = {-1, -1, -1};
    int count = cJSON_IsArray(root) ? std::min(50, cJSON_GetArraySize(root)) : 1;
    for (int i = 0; i < count; i++) {
      const cJSON *record = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, i) : root;
      if (!cJSON_IsObject(record)) continue;
      report.candidates++;
      int score = lyricMatchScore(query, text(record, "trackName"), text(record, "artistName"),
                                  text(record, "albumName"), number(record, "duration") * 1000);
      if (score < 0) continue;
      if (cJSON_IsTrue(field(record, "instrumental"))) { instrumental = true; continue; }
      if (!*text(record, "syncedLyrics")) { untimed |= *text(record, "plainLyrics") != 0; continue; }
      for (int slot = 0; slot < 3; slot++) if (score > scores[slot]) {
        for (int j = 2; j > slot; j--) { scores[j] = scores[j - 1]; best[j] = best[j - 1]; }
        scores[slot] = score; best[slot] = record; break;
      }
    }
    for (int i = 0; i < 3; i++) if (best[i] && timed(text(best[i], "syncedLyrics"), source, scores[i])) return true;
    return false;
  }
  bool netease(int64_t &until) {
    Json search;
    String keyword = lyricSearchArtist(query.artist) + " " + lyricSearchTitle(query.title);
    if (!get("https://music.163.com/api/search/get?type=1&limit=10&offset=0&s=" + lyricEncode(keyword), true, until, search)) return false;
    const cJSON *result = field(search.value, "result");
    const cJSON *songs = field(result, "songs");
    if (number(search.value, "code") != 200 || !cJSON_IsObject(result)) {
      failed = true; until = transport.nowMs() + 30000; return false;
    }
    if (!cJSON_IsArray(songs)) return false;
    const cJSON *best[2] = {}; int scores[2] = {-1, -1};
    for (int i = 0; i < std::min(10, cJSON_GetArraySize(songs)); i++) {
      const cJSON *song = cJSON_GetArrayItem(songs, i); report.candidates++;
      String artists;
      const cJSON *names = field(song, "artists"); if (!cJSON_IsArray(names)) names = field(song, "ar");
      for (int j = 0; j < std::min(12, cJSON_GetArraySize(names)); j++) {
        if (j) artists += ", "; artists += text(cJSON_GetArrayItem(names, j), "name");
      }
      const cJSON *album = field(song, "album"); if (!album) album = field(song, "al");
      double duration = number(song, "duration", number(song, "dt"));
      int score = lyricMatchScore(query, text(song, "name"), artists, text(album, "name"), duration);
      auto aliases = field(song, "alias"); if (!cJSON_IsArray(aliases)) aliases = field(song, "alia");
      for (int a = 0; lyricVersionsMatch(query.title, text(song, "name")) && a < std::min(6, cJSON_GetArraySize(aliases)); a++) {
        auto alias = cJSON_GetArrayItem(aliases, a);
        if (cJSON_IsString(alias)) score = std::max(score, lyricMatchScore(query, alias->valuestring, artists, text(album, "name"), duration));
      }
      if (score < 0 || number(song, "id") <= 0) continue;
      if (score > scores[0]) { best[1] = best[0]; scores[1] = scores[0]; best[0] = song; scores[0] = score; }
      else if (score > scores[1]) { best[1] = song; scores[1] = score; }
    }
    for (int i = 0; i < 2; i++) if (best[i]) {
      char id[32]; snprintf(id, sizeof id, "%.0f", number(best[i], "id"));
      Json lyrics;
      if (!get(String("https://music.163.com/api/song/lyric?id=") + id + "&lv=-1&kv=-1&tv=-1", true, until, lyrics)) continue;
      if (number(lyrics.value, "code") != 200) { failed = true; until = transport.nowMs() + 30000; continue; }
      if (cJSON_IsTrue(field(lyrics.value, "nolyric"))) { instrumental = true; continue; }
      const char *lrc = text(field(lyrics.value, "lrc"), "lyric");
      if (strstr(lrc, "纯音乐，请欣赏") || strstr(lrc, "純音樂，請欣賞")) { instrumental = true; continue; }
      if (timed(lrc, LyricProvider::Netease, scores[i])) return true;
    }
    return false;
  }
};
}
LyricReport LyricsResolver::resolve(const LyricQuery &q, Music &music, LyricsTransport &transport,
                                    LyricLookupMode mode) {
  music.lineCount = 0; music.lyricStatus = LyricStatus::Missing;
  Lookup lookup{q, music, transport, {}, transport.nowMs() + 25000};
  if (q.title.isEmpty() || q.artist.isEmpty() || !q.durationMs) return lookup.report;
  if (mode == LyricLookupMode::Automatic) {
    Json exact;
    String url = "https://lrclib.net/api/get?track_name=" + lyricEncode(q.title) +
      "&artist_name=" + lyricEncode(q.artist) + "&album_name=" + lyricEncode(q.album) +
      "&duration=" + String((q.durationMs + 500) / 1000);
    if (lookup.get(url, false, lrclibUntil, exact) && lookup.lrclib(exact.value, LyricProvider::LrclibExact)) return lookup.report;
  }
  if (mode != LyricLookupMode::NeteaseOnly) {
    String title = q.title, artist = q.artist;
    for (int attempt = 0; attempt < 2; attempt++) {
      Json search;
      String url = "https://lrclib.net/api/search?track_name=" + lyricEncode(title) + "&artist_name=" + lyricEncode(artist);
      if (lookup.get(url, false, lrclibUntil, search) && lookup.lrclib(search.value, LyricProvider::LrclibSearch)) return lookup.report;
      String nextTitle = lyricSearchTitle(q.title), nextArtist = lyricSearchArtist(q.artist);
      if (nextTitle == title && nextArtist == artist) break;
      title = nextTitle; artist = nextArtist;
    }
  }
  if (mode != LyricLookupMode::LrclibSearchOnly && lookup.netease(neteaseUntil)) return lookup.report;
  music.lineCount = 0;
  music.lyricStatus = lookup.untimed ? LyricStatus::Untimed : lookup.instrumental ? LyricStatus::Instrumental :
                      lookup.failed || lookup.report.deferred ? LyricStatus::Unavailable : LyricStatus::Missing;
  return lookup.report;
}
}
