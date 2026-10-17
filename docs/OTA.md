# OTA, external LittleFS and shared font assets

InkOS 0.6 uses native ESP-IDF A/B OTA with **two 5.9375 MiB application slots**.
The entire LittleFS filesystem lives on U5 external Flash. Chinese glyphs use
Source Han Sans SC Light 2.005 in a shared internal read-only partition.
The ESP32 silicon ROM is unchanged. The second-stage Espressif bootloader
supports native rollback; no eFuses, secure boot or encryption settings change.

## Current physical storage

| Chip / region | Offset | Size | Purpose |
|---|---:|---:|---|
| Internal bootloader | `0x000000` | 32 KiB reserved | Image validation, slot selection |
| Internal partition table | `0x008000` | 4 KiB | This fixed memory map |
| Internal NVS | `0x009000` | 24 KiB | Existing Wi-Fi, account/settings, pairing key |
| Internal OTA data | `0x010000` | 8 KiB | Two-sector boot selection and rollback state |
| Internal ota_0 | `0x020000` | 5.9375 MiB | Application A |
| Internal ota_1 | `0x610000` | 5.9375 MiB | Application B |
| Internal fonts | `0xC00000` | 4 MiB | Shared immutable compressed glyph archive |
| External extstore | `0x000000` | 4 MiB | LittleFS, registered dynamically at boot |

The internal area before ota_0 totals 128 KiB, including the regions above and
60 KiB of alignment/reserved gaps. There is no internal filesystem partition.
Layout identity is `inkos-ab594-extfs-v2`; 0.5 packages use a different layout
and are rejected. A regular OTA package never changes this table.

U5 on this measured EE04 unit uses **CLK11, MOSI13, MISO12, CS40**, with GPIO43
enabling its 3.3 V rail. GPIO39 stays deselected. These CLK/MOSI assignments are
opposite to the supplied schematic labels. The panel uses separate SPI3/HSPI;
U5 uses native ESP-IDF SPI2 at 10 MHz. See [hardware measurements](EXTERNAL-FLASH.md).
External Flash cannot be used as a stock bootloader application slot.

## Filesystem and fonts

`Storage` owns the external device, strict 5E4016/4 MiB identity check, partition
registration and LittleFS mount. Calendars and electricity services depend on
that module rather than choosing a physical partition themselves. A failed
mount is never automatically formatted. This avoids destroying data after a
power, wiring or corruption fault. The filesystem is required for boot-health
confirmation; failures remain visible and pending updates can roll back.

Source Han Light provides 28,430 glyphs at each of 14/18/24/28 px. Its raw
7,555,344-byte archive is stored in 29 independently compressed DEFLATE blocks,
totalling 4,185,866 bytes. Runtime validates the compressed SHA and decoded raw
SHA, then uses three 256 KiB PSRAM cache buffers. Compression preserves every
native one-bit source glyph pixel; Latin/Finnish fonts remain unchanged.
Both applications share these immutable assets. A font change requires USB
maintenance of both apps and their shared archive, rather than changing assets
under a possible rollback target.

## One-time USB migration

The supported source is InkOS with an internal `storage` partition at
`0xC20000`, size `0x3E0000`. Keep the previous compatible application/font images
and a verified full 16 MiB internal backup. Backups contain credentials and
private calendar data; they must remain outside public packages.

1. Build the new application, font archive and `tools/storage_bridge` sketch.
2. The maintenance tool validates device MAC, image/asset identity, target table
   and previous recovery images. It saves boot/NVS and the old filesystem.
3. It temporarily loads a USB-only bridge while retaining the source table.
4. Before any external erase, it backs up **all 4 MiB U5** and records SHA-256.
   It then saves an exact current internal filesystem image while writers are stopped.
5. It compares source/target hashes, clones the old filesystem, verifies every
   copied byte, mounts LittleFS and grows it from 3.875 MiB to the full 4 MiB.
6. Only after a successful external mount does it write the new internal table,
   bootloader, initial A/B copies, fonts and empty OTA selection. NVS is not written.
7. A maintenance failure attempts restoration of the previous internal layout,
   both applications, compatible font archive and cache. Retain the backups even
   after the new device confirms healthy. USB recovery may be needed if power is
   lost while replacing a partition table or bootloader.

```powershell
python tools/generate_cjk_fonts.py
python tools/build.py --cli PATH_TO_ARDUINO_CLI --config-file PATH_TO_CONFIG
# Compile tools/storage_bridge with the same XIAO ESP32-S3 Plus / OPI PSRAM FQBN.
python tools/migrate_ota.py --port COM_PORT --expected-mac INSPECTED_MAC --esptool PATH_TO_ESPTOOL --images NEW_IMAGE_DIR --assets out/assets --previous-images PRIVATE_PREVIOUS_IMAGE_DIR --previous-assets PRIVATE_PREVIOUS_ASSET_DIR --bridge-image STORAGE_BRIDGE_BIN --backup NEW_PRIVATE_BACKUP_DIR --existing-internal-backup PRIVATE_FULL_16MB_BACKUP
```

Do not use the legacy `tools/flash.py`, erase-flash or a merged image for this layout.

## Routine IP upgrade

In Studio 0.6 connect with **IP + existing pairing key**, open **Network → Firmware**,
select the app-only OTA ZIP and choose **Upgrade + restart**. Studio uploads to
the inactive slot, waits for restart and reports success only after boot health
is confirmed. No resident computer server is needed.

```powershell
python tools/ota.py pack --image NEW_IMAGE_DIR/InkOS.ino.bin --assets out/assets/manifest.json --output out/release/InkOS-OTA-0.6.0.zip
python tools/ota.py install --ip DEVICE_IP --package out/release/InkOS-OTA-0.6.0.zip
```

The CLI asks for the pairing key with hidden input. The ZIP contains only
`manifest.json` and `firmware.bin`, with board/layout/font identity, size and SHA.
Ordinary OTA writes neither the filesystem, fonts, NVS, bootloader nor partition table.

## Transaction, boot confirmation and rollback

Commands use the paired challenge-bound AES-256-GCM channel. A random upload
session, sequential 2560-byte chunks, idempotent retry of the last chunk and full
SHA/image validation prevent interrupted or mixed uploads from becoming boot
candidates. A 90-second upload timeout leaves the current application selected.

After a verified upload, native OTA data selects the inactive slot and the device
restarts. InkOS overrides Arduino's weak `verifyRollbackLater()` to return true;
otherwise Arduino confirms the image before application checks. The new image
stays PENDING_VERIFY until startup self-tests, complete fonts, mounted external
filesystem, a successful screen frame and live display/network/control tasks
pass for at least 30 seconds. Wi-Fi or external service availability itself is
not required. An unconfirmed reset rolls back in the bootloader; a 90-second
health failure explicitly requests rollback. A hang needs a watchdog/reset to
reach the bootloader. Both slots and the shared assets still require USB recovery
if all valid images or shared hardware are lost.

Rollback does not undo NVS schemas or filesystem formats. Future changes must
remain compatible with the previous app or use an explicit USB maintenance plan.
Pairing authenticates access and SHA validates bytes; this is not publisher-signed
firmware, secure boot or anti-downgrade. Use a trusted LAN/VPN.

`storage_info` reports real partition usage and filesystem free space through the
paired channel. Image-slot slack is distinct from filesystem capacity. NVS uses
entry counts; it is not reported as falsely exact payload-byte free space.

## References

- [ESP-IDF OTA and rollback](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/ota.html)
- [External partition API](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/storage/partition.html)
- [LittleFS growth](https://github.com/joltwallet/esp_littlefs)
- [Recorded hardware validation](VALIDATION.md)
