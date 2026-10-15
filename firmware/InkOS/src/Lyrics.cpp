#include "Lyrics.h"
#include <algorithm>
#include <ctype.h>
namespace ink {
void parseTimedLyrics(const char *raw, Music &m) {
  m.lineCount = 0;
  const char *line = raw;
  while (*line && m.lineCount < MAX_LYRICS) {
    const char *end = strchr(line, '\n');
    if (!end)
      end = line + strlen(line);
    const char *text = line;
    uint32_t times[12];
    int count = 0;
    while (text < end && *text == '[') {
      int mm = 0, ss = 0, used = 0;
      const char *close = strchr(text, ']');
      if (!close || close > end)
        break;
      if (sscanf(text, "[%d:%d%n", &mm, &ss, &used) != 2 || mm < 0 || mm > 999 || ss < 0 || ss > 59)
        break;
      const char *p = text + used;
      int ms = 0;
      if (*p == '.' || *p == ':') {
        p++;
        int digits = 0;
        while (p < close && isdigit(*p) && digits < 3) {
          ms = ms * 10 + (*p++ - '0');
          digits++;
        }
        while (digits++ < 3)
          ms *= 10;
      }
      if (p != close)
        break;
      if (count < 12)
        times[count++] = (mm * 60 + ss) * 1000 + ms;
      text = close + 1;
    }
    while (text < end && (*text == ' ' || *text == '\r'))
      text++;
    for (int t = 0; t < count && m.lineCount < MAX_LYRICS; t++) {
      auto &q = m.lines[m.lineCount++];
      q.atMs = times[t];
      size_t len = std::min<size_t>(end - text, sizeof(q.text) - 1);
      while (len && ((text[len] & 192) == 128))
        len--;
      memcpy(q.text, text, len);
      q.text[len] = 0;
      while (len && (q.text[len - 1] == '\r' || q.text[len - 1] == ' '))
        q.text[--len] = 0;
    }
    line = *end ? end + 1 : end;
  }
  std::sort(m.lines, m.lines + m.lineCount,
            [](const Lyric &a, const Lyric &b) { return a.atMs < b.atMs; });
}
} // namespace ink
