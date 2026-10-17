#include "UsbLogDisk.h"
#include <sdkconfig.h>
#if CONFIG_TINYUSB_MSC_ENABLED && !ARDUINO_USB_MODE
#include "LogStore.h"
#include "VirtualFat.h"
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <new>
namespace ink {
namespace {
// Constructed statically so the MSC interface joins the descriptor before USB.begin().
USBMSC msc;
VirtualFat *disk = nullptr;
SemaphoreHandle_t diskLock = nullptr;
bool started = false;
bool readFile(const char *name, uint32_t offset, uint8_t *out, size_t n) {
  return readLogFile(name, offset, out, n);
}
void snapshot() {
  LogFileInfo list[VirtualFat::MaxFiles];
  int n = listLogFiles(list, VirtualFat::MaxFiles);
  VirtualFatFile files[VirtualFat::MaxFiles] = {};
  for (int i = 0; i < n; i++) {
    strlcpy(files[i].name, list[i].name, sizeof files[i].name);
    files[i].size = list[i].size;
    files[i].mtime = list[i].mtime;
  }
  xSemaphoreTake(diskLock, portMAX_DELAY);
  disk->load(files, n, uint32_t(esp_random()));
  xSemaphoreGive(diskLock);
}
void hostConnected() {
  setLogHostActive(true); // Freeze rotation/deletion so the snapshot stays valid.
  snapshot();
}
void hostGone() { setLogHostActive(false); }
int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  auto out = static_cast<uint8_t *>(buffer);
  uint8_t sector[VirtualFat::SectorBytes];
  xSemaphoreTake(diskLock, portMAX_DELAY);
  for (uint32_t done = 0; done < size;) {
    uint32_t at = offset + done, l = lba + at / VirtualFat::SectorBytes,
             within = at % VirtualFat::SectorBytes;
    uint32_t n = std::min<uint32_t>(VirtualFat::SectorBytes - within, size - done);
    disk->sector(l, sector, readFile);
    memcpy(out + done, sector + within, n);
    done += n;
  }
  xSemaphoreGive(diskLock);
  return int32_t(size);
}
int32_t onWrite(uint32_t, uint32_t, uint8_t *, uint32_t) { return -1; } // Write-protected.
bool onStartStop(uint8_t, bool start, bool eject) {
  if (eject && !start)
    hostGone();
  else if (eject && start)
    hostConnected();
  return true;
}
void onUsbEvent(void *, esp_event_base_t base, int32_t id, void *) {
  if (base != ARDUINO_USB_EVENTS)
    return;
  if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT)
    hostConnected();
  else if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT)
    hostGone();
}
} // namespace
void startUsbLogDisk() {
  if (started)
    return;
  void *mem = heap_caps_malloc(sizeof(VirtualFat), MALLOC_CAP_SPIRAM);
  diskLock = xSemaphoreCreateMutex();
  if (!mem || !diskLock)
    return;
  disk = new (mem) VirtualFat();
  snapshot();
  msc.vendorID("InkOS");
  msc.productID("INKOS_LOGS");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.isWritable(false);
  msc.mediaPresent(true);
  started = msc.begin(VirtualFat::TotalSectors, VirtualFat::SectorBytes);
  USB.onEvent(onUsbEvent);
  if (USB) // Already enumerated before we attached: treat as a connected host.
    hostConnected();
}
bool usbLogDiskAvailable() { return started; }
} // namespace ink
#else
namespace ink {
void startUsbLogDisk() {}
bool usbLogDiskAvailable() { return false; }
} // namespace ink
#endif
