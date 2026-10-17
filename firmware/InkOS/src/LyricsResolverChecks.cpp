#include "LyricsResolver.h"
#include <esp_heap_caps.h>
#include <new>
namespace ink {
namespace {
class FixtureTransport : public LyricsTransport {
public:
  String exact = "{}", search = "[]", neSearch = "{\"code\":200,\"result\":{\"songs\":[]}}";
  String neLyrics = "{\"code\":200,\"lrc\":{\"lyric\":\"[00:01.00]Chinese fixture\\n[00:03.00]Second line\"}}";
  int exactHttp = 404, searchHttp = 200, neHttp = 200;
  int searches = 0, neteaseSearches = 0, neteaseLyrics = 0;
  bool normalizedQuery = false;
  int64_t clock = 100000;
  bool fetch(const String &url, HttpResponse &r, const RequestOptions &) override {
    String body;
    clock += 50;
    if (url.startsWith("https://lrclib.net/api/get?")) { r.status = exactHttp; body = exact; }
    else if (url.startsWith("https://lrclib.net/api/search?")) { searches++; r.status = searchHttp; body = search; }
    else if (url.startsWith("https://music.163.com/api/search/get?")) {
      neteaseSearches++; r.status = neHttp; body = neSearch;
      normalizedQuery = url.indexOf(lyricEncode("五月天 倔强")) >= 0;
    } else { neteaseLyrics++; r.status = 200; body = neLyrics; }
    if (r.status == 429) r.retryAfter = "120";
    r.length = body.length(); r.bytes = (uint8_t *)malloc(r.length + 1);
    if (!r.bytes) return false;
    memcpy(r.bytes, body.c_str(), r.length + 1); return true;
  }
  int64_t nowMs() const override { return clock; }
  void pause(unsigned ms) override { clock += ms; }
};
}
void lyricsResolverChecks(int &passed, int &total) {
  auto check = [&](bool ok) { total++; if (ok) passed++; else Serial.printf("INKOS selftest failed_case=%d\n", total); };
  LyricQuery cn{"倔強", "Mayday", "神的孩子都在跳舞", 261618};
  check(lyricFold("繁體，中文 ＡＢＣ") == "繁体中文abc");
  check(lyricSearchArtist("JAY CHOU") == "周杰伦");
  check(lyricSearchTitle("倔強 (Live)") == "倔强");
  check(lyricEncode("中 &") == "%E4%B8%AD%20%26");
  check(lyricMatchScore(cn, "倔强", "五月天", cn.album, 261618) > 140);
  check(lyricMatchScore(cn, "倔强", "其他歌手", cn.album, 261618) < 0);
  check(lyricMatchScore(cn, "倔强", "五月天", cn.album, 264000) < 0);
  check(lyricMatchScore(cn, "倔强 (Live)", "五月天", cn.album, 261618) < 0);
  check(lyricMatchScore(cn, "同名也不同", "五月天", cn.album, 261618) < 0);
  check(lyricMatchScore(cn, "倔强", "五月天 Feat. Guest", cn.album, 261618) >= 140);
  LyricQuery live{"倔強 (Live)", "Mayday", "", 261618};
  check(lyricMatchScore(live, "倔强（现场版）", "五月天", "", 261618) >= 140);
  LyricQuery english{"Alive", "Artist", "Album", 120000};
  check(lyricMatchScore(english, "Alive - 2011 Remaster", "Artist", "", 120000) >= 140);
  check(lyricMatchScore(english, "Alive", "Artist", "", NAN) < 0);
  check(lyricRetrySeconds("120", Clock::epoch(2026, 10, 17)) == 120);
  check(lyricRetrySeconds("Sat, 17 Oct 2026 00:02:00 GMT", Clock::epoch(2026, 10, 17)) == 120);
  check(lyricRetrySeconds("unknown", Clock::epoch(2026, 10, 17)) == 60);
  check(lyricRetrySeconds("999999999999", Clock::epoch(2026, 10, 17)) == 86400);
  auto music = (Music *)heap_caps_malloc(sizeof(Music), MALLOC_CAP_SPIRAM);
  check(music != nullptr); if (!music) return; new (music) Music();
  const char *good = "{\"trackName\":\"Alive\",\"artistName\":\"Artist\",\"albumName\":\"Album\",\"duration\":120,\"syncedLyrics\":\"[00:01.00]Correct fixture\\n[00:03.00]Next\"}";
  {
    FixtureTransport http; http.exactHttp = 200; http.exact = good; LyricsResolver resolver;
    auto r = resolver.resolve(english, *music, http);
    check(r.source == LyricProvider::LrclibExact && r.requests == 1 && music->lineCount == 2);
  }
  {
    FixtureTransport http;
    http.search = String("[{\"trackName\":\"Alive (Live)\",\"artistName\":\"Artist\",\"duration\":120,\"syncedLyrics\":\"[00:01.00]Wrong\"},") + good + "]";
    LyricsResolver resolver; auto r = resolver.resolve(english, *music, http);
    check(r.source == LyricProvider::LrclibSearch && r.requests == 2 && !strcmp(music->lines[0].text, "Correct fixture"));
  }
  const char *song = "{\"code\":200,\"result\":{\"songs\":[{\"id\":386175,\"name\":\"倔强\",\"duration\":261618,\"artists\":[{\"name\":\"五月天\"}],\"album\":{\"name\":\"神的孩子都在跳舞\"}}]}}";
  {
    FixtureTransport http; http.neSearch = song; LyricsResolver resolver;
    auto r = resolver.resolve(cn, *music, http);
    check(r.source == LyricProvider::Netease && music->lyricStatus == LyricStatus::Timed && http.normalizedQuery);
    check(http.searches == 2 && http.neteaseSearches == 1 && http.neteaseLyrics == 1);
  }
  {
    FixtureTransport http; http.neSearch = "{\"code\":200,\"result\":{\"songs\":[{\"id\":1,\"name\":\"倔强 (Live)\",\"alias\":[\"倔强\"],\"duration\":261618,\"artists\":[{\"name\":\"五月天\"}]}]}}";
    LyricsResolver resolver; auto r = resolver.resolve(cn, *music, http, LyricLookupMode::NeteaseOnly);
    check(r.source == LyricProvider::None && http.neteaseLyrics == 0 && music->lineCount == 0);
  }
  {
    FixtureTransport http; http.searchHttp = 429; LyricsResolver resolver;
    auto first = resolver.resolve(english, *music, http, LyricLookupMode::LrclibSearchOnly);
    auto second = resolver.resolve(english, *music, http, LyricLookupMode::LrclibSearchOnly);
    check(first.requests == 1 && first.deferred && second.requests == 0 && second.deferred);
    http.clock += 120001; http.searchHttp = 200;
    auto third = resolver.resolve(english, *music, http, LyricLookupMode::LrclibSearchOnly);
    check(third.requests == 1 && !third.deferred);
  }
  {
    FixtureTransport http; http.search = "{broken"; LyricsResolver resolver;
    auto r = resolver.resolve(english, *music, http, LyricLookupMode::LrclibSearchOnly);
    check(music->lyricStatus == LyricStatus::Unavailable && music->lineCount == 0 && r.source == LyricProvider::None);
  }
  {
    FixtureTransport http; http.search = String("[") + good + "]"; LyricsResolver resolver;
    http.search.replace("00:01.00", "99:01.00"); http.search.replace("00:03.00", "99:03.00");
    auto r = resolver.resolve(english, *music, http, LyricLookupMode::LrclibSearchOnly);
    check(music->lyricStatus != LyricStatus::Timed && music->lineCount == 0);
  }
  {
    FixtureTransport http; http.exactHttp = 200;
    http.exact = "{\"trackName\":\"Alive\",\"artistName\":\"Artist\",\"duration\":120,\"plainLyrics\":\"Original fixture\"}";
    LyricsResolver resolver; resolver.resolve(english, *music, http);
    check(music->lyricStatus == LyricStatus::Untimed && music->lineCount == 0);
  }
  {
    FixtureTransport http; http.exactHttp = 200;
    http.exact = "{\"trackName\":\"Alive\",\"artistName\":\"Artist\",\"duration\":120,\"instrumental\":true}";
    LyricsResolver resolver; resolver.resolve(english, *music, http);
    check(music->lyricStatus == LyricStatus::Instrumental && music->lineCount == 0);
  }
  free(music);
}
}
