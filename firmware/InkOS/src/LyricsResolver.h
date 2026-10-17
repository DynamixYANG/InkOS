#pragma once
#include "Services.h"
namespace ink {
enum class LyricProvider { None, LrclibExact, LrclibSearch, Netease };
enum class LyricLookupMode { Automatic, LrclibSearchOnly, NeteaseOnly };
struct LyricQuery {
  String title, artist, album;
  uint32_t durationMs = 0;
};
struct LyricReport {
  LyricProvider source = LyricProvider::None;
  int requests = 0, candidates = 0, score = 0, lrclibHttp = 0, neteaseHttp = 0;
  bool deferred = false;
};
class LyricsTransport {
public:
  virtual ~LyricsTransport() = default;
  virtual bool fetch(const String &url, HttpResponse &, const RequestOptions &) = 0;
  virtual int64_t nowMs() const = 0;
  virtual void pause(unsigned milliseconds) = 0;
};
class LyricsResolver {
  int64_t lrclibUntil = 0, neteaseUntil = 0;
public:
  LyricReport resolve(const LyricQuery &, Music &, LyricsTransport &,
                      LyricLookupMode = LyricLookupMode::Automatic);
};
String lyricEncode(const String &);
String lyricFold(const String &, bool compact = true);
String lyricSearchTitle(const String &);
String lyricSearchArtist(const String &);
int lyricMatchScore(const LyricQuery &, const String &title, const String &artist,
                    const String &album, double durationMs);
bool lyricVersionsMatch(const String &, const String &);
int lyricRetrySeconds(const String &, time_t now);
const char *lyricProviderName(LyricProvider);
void resolveTrackLyrics(Music &, const char *leadArtist);
cJSON *lyricsDiagnostics();
cJSON *lyricProbeCommand(cJSON *);
void serviceLyricProbe();
}
