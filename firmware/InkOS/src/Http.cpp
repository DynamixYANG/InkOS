#include "Services.h"
#include "CalDav.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <miniz.h>
namespace ink {
HttpResponse::~HttpResponse() {
  if (bytes)
    free(bytes);
}
class Sink : public Stream {
  HttpResponse &r;

public:
  explicit Sink(HttpResponse &v) : r(v) {}
  int available() override {
    return 0;
  }
  int read() override {
    return -1;
  }
  int peek() override {
    return -1;
  }
  void flush() override {}
  size_t write(uint8_t c) override {
    return write(&c, 1);
  }
  size_t write(const uint8_t *p, size_t n) override {
    size_t take = std::min(n, r.capacity - r.length);
    if (take)
      memcpy(r.bytes + r.length, p, take);
    r.length += take;
    return take;
  }
};
static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (int b = 0; b < 8; b++)
      c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
  }
  return ~c;
}
static bool unpack(HttpResponse &r, const String &encoding, size_t maximum) {
  if (encoding.isEmpty() || encoding == "identity")
    return true;
  size_t begin = 0, end = r.length;
  int flags = 0;
  bool gzip = encoding == "gzip";
  if (gzip) {
    if (end < 18 || r.bytes[0] != 31 || r.bytes[1] != 139 || r.bytes[2] != 8 || (r.bytes[3] & 0xe0))
      return false;
    uint8_t f = r.bytes[3];
    begin = 10;
    if (f & 4) {
      if (begin + 2 > end - 8)
        return false;
      size_t extra = r.bytes[begin] | r.bytes[begin + 1] << 8;
      begin += 2 + extra;
    }
    if (begin > end - 8)
      return false;
    for (int field = 8; field <= 16; field *= 2)
      if (f & field) {
        while (begin < end - 8 && r.bytes[begin])
          begin++;
        if (begin >= end - 8)
          return false;
        begin++;
      }
    if (f & 2)
      begin += 2;
    if (begin > end - 8)
      return false;
    end -= 8;
  } else if (encoding == "deflate")
    flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
  else
    return false;
  uint8_t *out = (uint8_t *)heap_caps_malloc(maximum + 1, MALLOC_CAP_SPIRAM);
  if (!out)
    return false;
  size_t n = tinfl_decompress_mem_to_mem(out, maximum, r.bytes + begin, end - begin, flags);
  bool good = n != TINFL_DECOMPRESS_MEM_TO_MEM_FAILED && n <= maximum;
  if (good && gzip) {
    uint32_t expected = 0, actualSize = 0;
    for (int i = 0; i < 4; i++) {
      expected |= uint32_t(r.bytes[end + i]) << (8 * i);
      actualSize |= uint32_t(r.bytes[end + 4 + i]) << (8 * i);
    }
    good = actualSize == n && expected == crc32(out, n);
  }
  if (!good) {
    free(out);
    return false;
  }
  out[n] = 0;
  free(r.bytes);
  r.bytes = out;
  r.length = n;
  return true;
}
bool request(const String &url, HttpResponse &r, size_t maximum, const String &auth,
             const String &body, const RequestOptions &options) {
  if (!url.startsWith("https://"))
    return false;
  r.bytes = (uint8_t *)heap_caps_malloc(maximum + 1, MALLOC_CAP_SPIRAM);
  if (!r.bytes)
    return false;
  r.capacity = maximum;
  String current = url;
  for (int redirect = 0; redirect < 4; redirect++) {
    WiFiClientSecure tls;
    tls.useBuiltinCACertBundle();
    HTTPClient http;
    http.setConnectTimeout(options.timeoutMs);
    http.setTimeout(options.timeoutMs);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!current.startsWith("https://") || (options.icloudOnly && !validICloudUrl(current)) || !http.begin(tls, current)) {
      r.status = -1;
      return false;
    }
    const char *names[] = {"Retry-After", "ETag",     "Last-Modified",
                           "Expires",     "Location", "Content-Encoding"};
    http.collectHeaders(names, 6);
    http.addHeader("User-Agent", options.userAgent.length()
                                     ? options.userAgent
                                     : "InkOS/0.2 (ESP32 personal e-paper display)");
    http.addHeader("Accept-Encoding", "gzip, deflate");
    if (auth.length())
      http.addHeader("Authorization", auth);
    if (options.headerName.length() && options.headerValue.length())
      http.addHeader(options.headerName, options.headerValue);
    if (options.etag.length())
      http.addHeader("If-None-Match", options.etag);
    if (options.lastModified.length())
      http.addHeader("If-Modified-Since", options.lastModified);
    if (options.method.length()) {
      if (options.depth.length()) http.addHeader("Depth", options.depth);
      http.addHeader("Content-Type", options.contentType);
      r.status = http.sendRequest(options.method.c_str(), body);
    } else if (body.length()) {
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      r.status = http.POST(body);
    } else
      r.status = http.GET();
    if (options.redirects && (r.status == 301 || r.status == 302 || r.status == 303 ||
                              r.status == 307 || r.status == 308)) {
      String next = http.header("Location");
      http.end();
      if (next.startsWith("/")) {
        int slash = current.indexOf('/', 8);
        next = current.substring(0, slash < 0 ? current.length() : slash) + next;
      }
      if (!next.startsWith("https://")) {
        r.status = -3;
        return false;
      }
      current = next;
      continue;
    }
    r.retryAfter = http.header("Retry-After");
    r.retrySeconds = r.retryAfter.toInt();
    r.etag = http.header("ETag");
    r.lastModified = http.header("Last-Modified");
    r.expires = http.header("Expires");
    bool complete = r.status > 0;
    r.length = 0;
    String encoding = http.header("Content-Encoding");
    if (complete && r.status != 204 && r.status != 304) {
      if (http.getSize() > int(maximum))
        complete = false;
      else {
        Sink sink(r);
        int n = http.writeToStream(&sink);
        complete = n >= 0 && r.length <= maximum;
      }
    }
    r.bytes[r.length] = 0;
    http.end();
    if (complete && r.length)
      complete = unpack(r, encoding, maximum);
    if (!complete && r.status > 0)
      r.status = -2;
    return complete;
  }
  r.status = -3;
  return false;
}
} // namespace ink
