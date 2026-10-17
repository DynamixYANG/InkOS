#include "LyricsResolver.h"
#include "Text.h"
#include <algorithm>
#include <ctype.h>
namespace ink {
uint32_t foldHan(uint32_t);
static void append(String &s, uint32_t cp) {
  if (cp < 128) s += char(cp);
  else if (cp < 2048) { s += char(0xC0 | cp >> 6); s += char(0x80 | (cp & 63)); }
  else if (cp < 65536) { s += char(0xE0 | cp >> 12); s += char(0x80 | (cp >> 6 & 63)); s += char(0x80 | (cp & 63)); }
  else { s += char(0xF0 | cp >> 18); s += char(0x80 | (cp >> 12 & 63)); s += char(0x80 | (cp >> 6 & 63)); s += char(0x80 | (cp & 63)); }
}
String lyricEncode(const String &s) {
  String out; const char *hex = "0123456789ABCDEF";
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if ((c < 128 && isalnum(c)) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}
String lyricFold(const String &input, bool compact) {
  String out; const char *p = input.c_str();
  while (*p) {
    uint32_t cp = foldHan(decodeUtf8(p));
    if (cp >= 0xFF01 && cp <= 0xFF5E) cp -= 0xFEE0;
    if (compact) {
      if (cp < 128) { if (isalnum(cp)) out += char(tolower(cp)); continue; }
      if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) || cp == 0xFFFD) continue;
    }
    append(out, cp);
  }
  return out;
}
static String baseTitle(const String &title) {
  int end = title.length();
  for (const char *mark : {" (", "(", "（", " [", " - ", "【"}) {
    int pos = title.indexOf(mark); if (pos > 0) end = std::min(end, pos);
  }
  String result = title.substring(0, end); result.trim(); return result;
}
String lyricSearchTitle(const String &title) { return lyricFold(baseTitle(title), false); }
String lyricSearchArtist(const String &name) {
  const String key = lyricFold(name);
  static const char *aliases[][2] = {
    {"jaychou", "周杰伦"}, {"mayday", "五月天"}, {"jjlin", "林俊杰"}, {"easonchan", "陈奕迅"},
    {"gem", "邓紫棋"}, {"jolintsai", "蔡依林"}, {"stefaniesun", "孙燕姿"}, {"fishleong", "梁静茹"},
    {"hebetien", "田馥甄"}, {"fayewong", "王菲"}, {"jackycheung", "张学友"}, {"andylau", "刘德华"}
  };
  for (const auto &a : aliases) if (key == a[0]) return a[1];
  return lyricFold(name, false);
}
static bool artistMatches(const String &expected, const String &candidate) {
  String key = lyricFold(lyricSearchArtist(expected));
  if (key.isEmpty()) return false;
  if (key == lyricFold(lyricSearchArtist(candidate))) return true;
  String split = lyricFold(candidate, false); split.toLowerCase();
  for (const char *mark : {" & ", " feat. ", " feat ", " featuring ", "/", ";", "、"}) split.replace(mark, ",");
  for (int start = 0; start < int(split.length());) {
    int end = split.indexOf(',', start); if (end < 0) end = split.length();
    String part = split.substring(start, end); part.trim();
    if (key == lyricFold(lyricSearchArtist(part))) return true;
    start = end + 1;
  }
  return false;
}
static unsigned version(const String &title) {
  String t = lyricFold(title, false); t.toLowerCase(); unsigned result = 0;
  static const char *words[][6] = {
    {"live", "现场", "演唱会", nullptr}, {"remix", "混音", nullptr},
    {"acoustic", "unplugged", "不插电", nullptr}, {"instrumental", "karaoke", "伴奏", "纯音乐", nullptr},
    {"demo", "小样", nullptr}, {"cover", "翻唱", nullptr},
    {"cantonese", "粤语", nullptr}, {"mandarin", "国语", nullptr},
    {"sped up", "spedup", "speed up", "加速", nullptr}, {"slowed", "减速", nullptr}
  };
  for (unsigned i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    for (const char *word : words[i]) {
      if (!word) break;
      for (int at = t.indexOf(word); at >= 0; at = t.indexOf(word, at + 1)) {
        int end = at + strlen(word);
        bool latin = uint8_t(word[0]) < 128;
        auto letter = [](char c) { return uint8_t(c) < 128 && isalnum(uint8_t(c)); };
        if (!latin || ((!at || !letter(t[at - 1])) && (end == int(t.length()) || !letter(t[end])))) result |= 1u << i;
      }
    }
  return result;
}
bool lyricVersionsMatch(const String &a, const String &b) { return version(a) == version(b); }
int lyricMatchScore(const LyricQuery &q, const String &title, const String &artist,
                    const String &album, double duration) {
  if (!q.durationMs || !isfinite(duration) || duration <= 0 || fabs(duration - q.durationMs) > 2000 ||
      version(q.title) != version(title) || !artistMatches(q.artist, artist)) return -1;
  String requested = lyricFold(q.title), actual = lyricFold(title);
  int score = 0;
  if (requested.length() && requested == actual) score = 100;
  else if (lyricFold(baseTitle(q.title)).length() && lyricFold(baseTitle(q.title)) == lyricFold(baseTitle(title))) score = 94;
  else return -1;
  if (q.album.length() && lyricFold(q.album) == lyricFold(album)) score += 10;
  return score + 40 + int(10 - fabs(duration - q.durationMs) / 200);
}
int lyricRetrySeconds(const String &value, time_t now) {
  bool digits = value.length() > 0;
  for (unsigned i = 0; i < value.length(); i++) digits &= isdigit(uint8_t(value[i]));
  if (digits) {
    int seconds = 0;
    for (unsigned i = 0; i < value.length(); i++) seconds = std::min(86400, seconds * 10 + value[i] - '0');
    return std::max(1, seconds);
  }
  tm parsed = {};
  if (strptime(value.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &parsed) && now > 1700000000)
    return std::max<int64_t>(1, std::min<int64_t>(86400, mktime(&parsed) - now));
  return 60;
}
const char *lyricProviderName(LyricProvider p) {
  switch (p) {
  case LyricProvider::LrclibExact: return "lrclib_exact";
  case LyricProvider::LrclibSearch: return "lrclib_search";
  case LyricProvider::Netease: return "netease";
  default: return "none";
  }
}
}
