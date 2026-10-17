#pragma once
#include <stdint.h>

namespace ink {
// Display-task-owned state. Requests arrive independently while the panel is busy.
// Input quiet time overlaps the panel waveform. Keep the final title visible
// for at least 100 ms so the following full refresh cannot hide it immediately.
class PageNavigation {
  int committedPage = 0, selectedPage = 0, visibleTitle = 0;
  uint32_t revision = 0;
  int64_t visibleSince = -1, titleShownAt = 0, requestedAt = 0;
  bool waiting = false;
public:
  void observe(int page, uint32_t serial, int64_t requestUs) {
    if (serial == revision) return;
    revision = serial;
    if (!waiting && page == committedPage) return;
    selectedPage = page;
    requestedAt = requestUs;
    waiting = true;
    visibleSince = visibleTitle == page ? (requestUs > titleShownAt ? requestUs : titleShownAt) : -1;
  }
  bool pending() const { return waiting; }
  bool needsPreview() const { return waiting && visibleTitle != selectedPage; }
  // Do not delay the first title. Busy-panel requests are already coalesced by
  // the latest target in the model; an extra debounce here only adds latency.
  bool previewReady(int64_t) const { return needsPreview(); }
  void previewed(int page, int64_t completedUs) {
    visibleTitle = page;
    titleShownAt = completedUs;
    visibleSince = page == selectedPage ? completedUs : -1;
  }
  bool ready(int64_t nowUs, uint32_t delayMs) const {
    return waiting && !needsPreview() && visibleSince >= 0 &&
           nowUs - requestedAt >= int64_t(delayMs) * 1000 &&
           nowUs - titleShownAt >= 100000;
  }
  void committed(int page) {
    committedPage = selectedPage = visibleTitle = page;
    waiting = false;
    visibleSince = -1;
  }
};
} // namespace ink
