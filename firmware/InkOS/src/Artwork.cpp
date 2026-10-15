#include "Artwork.h"
#include <esp_heap_caps.h>
#include <jpeg_decoder.h>
namespace ink {
bool decodeArtwork(const uint8_t *jpeg, size_t length, uint8_t *output) {
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = const_cast<uint8_t *>(jpeg);
  cfg.indata_size = length;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info = {};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || !info.width || !info.height ||
      info.width > 1024 || info.height > 1024 || info.output_len > 3200000)
    return false;
  cfg.outbuf = (uint8_t *)heap_caps_malloc(info.output_len, MALLOC_CAP_SPIRAM);
  if (!cfg.outbuf)
    return false;
  cfg.outbuf_size = info.output_len;
  bool good = esp_jpeg_decode(&cfg, &info) == ESP_OK;
  if (good) {
    const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    memset(output, 255, ART * ART / 8);
    for (int y = 0; y < ART; y++)
      for (int x = 0; x < ART; x++) {
        int source = ((y * info.height / ART) * info.width + x * info.width / ART) * 3;
        int lum = (cfg.outbuf[source] * 77 + cfg.outbuf[source + 1] * 150 +
                   cfg.outbuf[source + 2] * 29) >>
                  8;
        if (lum < bayer[(y & 3) * 4 + (x & 3)] * 16 + 8)
          output[(y * ART + x) / 8] &= ~(128 >> (x & 7));
      }
  }
  free(cfg.outbuf);
  return good;
}
} // namespace ink
