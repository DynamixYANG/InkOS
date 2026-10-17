#pragma once
namespace ink {
// Read-only USB mass-storage view of /logs (volume INKOS_LOGS), composite with CDC.
// Needs the USB-OTG (TinyUSB) build; in the default Hardware-CDC build this is a no-op.
void startUsbLogDisk();
bool usbLogDiskAvailable();
} // namespace ink
