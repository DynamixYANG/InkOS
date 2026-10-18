// NOAA SWPC planetary K-index forecast for the Home "tonight" Kp slot. Runs in the network task.
#include "Aurora.h"
#include "Log.h"
#include "Services.h"
#include <algorithm>
#include <esp_heap_caps.h>
#include <new>
namespace ink {
// ~7 KB JSON (81 three-hour bins: observed, estimated, predicted); buffered whole, gzip accepted.
static const char KpUrl[] = "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json";
constexpr uint32_t KpRefreshMs = 2 * 3600000u; // NOAA revises the forecast a few times per day
void updateAurora() {
  static uint32_t due = 0;
  static bool first = true;
  static String etag;
  if (!first && int32_t(millis() - due) < 0) return;
  first = false;
  auto a = (Aurora *)heap_caps_malloc(sizeof(Aurora), MALLOC_CAP_SPIRAM);
  if (!a) { due = millis() + 300000; return; }
  new (a) Aurora();
  {
    Guard g(modelMutex);
    *a = model->aurora;
  }
  HttpResponse response;
  RequestOptions opts;
  opts.userAgent = "InkOS/0.7 personal e-paper display (aurora Kp)";
  if (a->count) opts.etag = etag;
  bool fetched = request(KpUrl, response, 65536, "", "", opts);
  bool success = false, notModified = fetched && response.status == 304 && a->count;
  if (fetched && response.status == 200) {
    static Aurora parsed; // parse aside; keep the previous bins when the new body is malformed
    if (parseKpForecast((const char *)response.bytes, response.length, parsed) > 0) {
      memcpy(a->bins, parsed.bins, sizeof parsed.bins);
      a->count = parsed.count;
      success = true;
      etag = response.etag;
    }
  }
  if (success || notModified) {
    a->fetched = time(nullptr);
    a->revision++;
    due = millis() + KpRefreshMs;
    Guard g(modelMutex);
    model->aurora = *a;
  } else {
    due = millis() + uint32_t(std::max(300, std::min(3600, std::max(0, response.retrySeconds)))) * 1000;
  }
  logf("INKOS aurora status=%d bins=%d ok=%d\n", response.status, a->count, success || notModified);
  free(a);
  if (success || notModified) invalidate();
}
} // namespace ink
