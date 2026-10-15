#include "Artwork.h"
#include "Lyrics.h"
#include "Services.h"
#include <algorithm>
#include <cJSON.h>
#include <ctype.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
namespace ink {
static String accessToken;
static uint32_t credentialRevision = ~0u;
static int64_t accessExpiry = 0, nextAllowed = 0;
static Music *pending = nullptr;
static int64_t lyricRetry = 0;
static const cJSON *f(const cJSON *j, const char *k) {
  return cJSON_GetObjectItemCaseSensitive(j, k);
}
static const char *s(const cJSON *j, const char *k) {
  auto v = f(j, k);
  return cJSON_IsString(v) ? v->valuestring : "";
}
static double n(const cJSON *j, const char *k, double def = 0) {
  auto v = f(j, k);
  return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static String encode(const String &s) {
  String r;
  r.reserve(s.length() * 2);
  const char *hex = "0123456789ABCDEF";
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      r += char(c);
    else {
      r += '%';
      r += hex[c >> 4];
      r += hex[c & 15];
    }
  }
  return r;
}
static String normalized(const char *s) {
  String r;
  for (; *s; s++) {
    uint8_t c = *s;
    if (c >= 128 || isalnum(c))
      r += char(c < 128 ? tolower(c) : c);
  }
  return r;
}
static bool refreshAccess(const Credentials &cfg) {
  HttpResponse r;
  String body = "grant_type=refresh_token&client_id=" + encode(cfg.clientId) +
                "&refresh_token=" + encode(cfg.refreshToken);
  if (!request("https://accounts.spotify.com/api/token", r, 16384, "", body) || r.status != 200) {
    nextAllowed = esp_timer_get_time() + int64_t(std::max(30, r.retrySeconds)) * 1000000;
    Guard g(modelMutex);
    snprintf(model->music.error, sizeof model->music.error, "Spotify authorization HTTP %d",
             r.status);
    if (r.status == 400 || r.status == 401)
      model->music.authorized = false;
    model->music.revision++;
    invalidate();
    return false;
  }
  cJSON *j = cJSON_Parse((char *)r.bytes);
  if (!j)
    return false;
  accessToken = s(j, "access_token");
  int ttl = n(j, "expires_in", 3600);
  accessExpiry = esp_timer_get_time() + int64_t(std::max(1, ttl - 60)) * 1000000;
  const char *rotated = s(j, "refresh_token");
  if (*rotated && cfg.refreshToken != rotated)
    rotateToken(rotated);
  cJSON_Delete(j);
  return accessToken.length();
}
static void decodeCover(const char *url, Music &out) {
  String u(url);
  if (!u.startsWith("https://i.scdn.co/") && !u.startsWith("https://image-cdn-ak.spotifycdn.com/"))
    return;
  HttpResponse r;
  if (!request(u, r, 262144) || r.status != 200)
    return;
  if (decodeArtwork(r.bytes, r.length, out.art)) {
    out.artValid = true;
    out.artRevision++;
  }
}
static void fetchLyrics(Music &m, const char *leadArtist) {
  m.lineCount = 0;
  m.lyricStatus = LyricStatus::Missing;
  String u = "https://lrclib.net/api/get?track_name=" + encode(m.title) +
             "&artist_name=" + encode(leadArtist) + "&album_name=" + encode(m.album) +
             "&duration=" + String((m.duration + 500) / 1000);
  HttpResponse r;
  if (!request(u, r, 196608) || r.status != 200) {
    m.lyricStatus = r.status == 404 ? LyricStatus::Missing : LyricStatus::Unavailable;
    return;
  }
  cJSON *j = cJSON_Parse((char *)r.bytes);
  if (!j) {
    m.lyricStatus = LyricStatus::Unavailable;
    return;
  }
  bool match = normalized(s(j, "trackName")) == normalized(m.title) &&
               normalized(s(j, "artistName")) == normalized(leadArtist) &&
               fabs(n(j, "duration", -100) - m.duration / 1000.0) <= 2;
  if (match) {
    if (cJSON_IsTrue(f(j, "instrumental")))
      m.lyricStatus = LyricStatus::Instrumental;
    else if (*s(j, "syncedLyrics")) {
      parseTimedLyrics(s(j, "syncedLyrics"), m);
      m.lyricStatus = m.lineCount ? LyricStatus::Timed : LyricStatus::Missing;
    } else if (*s(j, "plainLyrics"))
      m.lyricStatus = LyricStatus::Untimed;
  }
  cJSON_Delete(j);
}
void updateSpotify() {
  auto cfg = credentials();
  if (credentialRevision != cfg.authRevision) {
    credentialRevision = cfg.authRevision;
    accessToken = "";
    accessExpiry = 0;
    nextAllowed = 0;
    lyricRetry = 0;
    Guard guard(modelMutex);
    auto &music = model->music;
    music.authorized = false;
    music.valid = false;
    music.playing = false;
    music.artValid = false;
    music.lineCount = 0;
    music.id[0] = 0;
    music.lyricStatus = LyricStatus::Waiting;
    music.revision++;
    music.artRevision++;
    invalidate();
  }
  if (cfg.clientId.isEmpty() || cfg.refreshToken.isEmpty()) {
    Guard g(modelMutex);
    if (model->music.authorized) {
      model->music.authorized = false;
      model->music.revision++;
      invalidate();
    }
    return;
  }
  int64_t now = esp_timer_get_time();
  if (now < nextAllowed)
    return;
  if (accessToken.isEmpty() || now >= accessExpiry)
    if (!refreshAccess(cfg))
      return;
  if (!pending) {
    pending = (Music *)heap_caps_malloc(sizeof(Music), MALLOC_CAP_SPIRAM);
    if (!pending)
      return;
    new (pending) Music();
  }
  {
    Guard g(modelMutex);
    memcpy(pending, &model->music, sizeof(Music));
  }
  HttpResponse r;
  int64_t began = esp_timer_get_time();
  if (!request("https://api.spotify.com/v1/me/player/currently-playing", r, 65536,
               "Bearer " + accessToken) ||
      r.status != 200) {
    if (r.status == 401) {
      accessExpiry = 0;
      nextAllowed = now + 1000000;
      return;
    }
    if (r.status == 429)
      nextAllowed = now + int64_t(std::max(5, r.retrySeconds)) * 1000000;
    Guard g(modelMutex);
    model->music.authorized = true;
    if (r.status == 204) {
      model->music.valid = false;
      model->music.playing = false;
      model->music.error[0] = 0;
    } else {
      model->music.stale = true;
      snprintf(model->music.error, sizeof model->music.error, "Spotify HTTP %d; retrying",
               r.status);
    }
    model->music.revision++;
    invalidate();
    return;
  }
  cJSON *j = cJSON_Parse((char *)r.bytes);
  if (!j)
    return;
  auto item = f(j, "item");
  if (!cJSON_IsObject(item)) {
    cJSON_Delete(j);
    return;
  }
  bool changed = strcmp(pending->id, s(item, "id")) != 0;
  bool playing = cJSON_IsTrue(f(j, "is_playing"));
  bool stateChange = playing != pending->playing || pending->stale || !pending->valid;
  uint32_t pos = n(j, "progress_ms"), duration = n(item, "duration_ms");
  bool correction = abs(int64_t(pos) - int64_t(playbackPosition(*pending, now))) > 1200;
  pending->authorized = true;
  pending->valid = true;
  pending->playing = playing;
  pending->stale = false;
  pending->error[0] = 0;
  pending->position = pos;
  pending->duration = duration;
  pending->sampleUs = began + (esp_timer_get_time() - began) / 2;
  strlcpy(pending->id, s(item, "id"), sizeof pending->id);
  strlcpy(pending->title, s(item, "name"), sizeof pending->title);
  auto album = f(item, "album");
  strlcpy(pending->album, s(album, "name"), sizeof pending->album);
  const auto artists = f(item, "artists");
  const char *lead = s(cJSON_GetArrayItem(artists, 0), "name");
  String all;
  for (int i = 0; i < cJSON_GetArraySize(artists); i++) {
    if (i)
      all += ", ";
    all += s(cJSON_GetArrayItem(artists, i), "name");
  }
  strlcpy(pending->artist, all.c_str(), sizeof pending->artist);
  if (changed) {
    pending->artValid = false;
    pending->lineCount = 0;
    pending->lyricStatus = LyricStatus::Waiting;
    pending->artRevision++;
    auto images = f(album, "images");
    const char *image = "";
    int best = 10000;
    for (int i = 0; i < cJSON_GetArraySize(images); i++) {
      auto im = cJSON_GetArrayItem(images, i);
      int width = n(im, "width");
      if (width >= ART && width < best) {
        image = s(im, "url");
        best = width;
      }
    }
    if (*image)
      decodeCover(image, *pending);
  }
  if (changed || (pending->lyricStatus != LyricStatus::Timed && now > lyricRetry)) {
    fetchLyrics(*pending, lead);
    lyricRetry = now + 300000000LL;
    stateChange = true;
  }
  if (changed || stateChange || correction)
    pending->revision++;
  cJSON_Delete(j);
  {
    Guard g(modelMutex);
    memcpy(&model->music, pending, sizeof(Music));
  }
  if (changed || stateChange || correction)
    invalidate();
}
} // namespace ink
