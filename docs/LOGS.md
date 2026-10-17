# Device logs (firmware 0.7.7, Studio 0.7.3)

InkOS keeps its `INKOS ...` diagnostic lines (display timing, navigation, Wi-Fi, weather,
calendar, energy, OTA) on the device so they can be read later over USB, over paired IP,
or (experimental build only) as a read-only USB drive.

## Pipeline

```
logf()  ──►  16 KiB PSRAM ring of whole lines (never blocks, evicts oldest)
              ├─ USB reader: control task writes to the serial port only while a host is reading
              └─ file reader: "log-store" task (core 0, prio 1) ─► /logs/current.log
                                                                 └─ at 64 KiB ─► /logs/inkos-NNNNNN.tar.gz
```

- **Writers never block.** `logf` formats at most 320 bytes into a stack buffer and copies it
  into the ring under a spinlock. When the ring is full the oldest whole lines are evicted.
  The two readers keep independent positions; each counts lines evicted before it read them
  (`dropped` for USB, `file_dropped` for flash).
- **USB drain** runs in the control task. With the Hardware-CDC build it writes only while the
  4 KiB TX buffer is nearly empty (a host is reading), so a plugged-in but unread port no longer
  stalls anything. Command replies and the boot banner are written by the same task and are
  never interleaved with log lines.
- **Flash writer** (`log-store`, 8 KiB stack, never takes the frame mutex) wakes every 15 s or
  when 8 KiB are pending, appends one batch (up to 16 KiB, PSRAM) to `/logs/current.log` on
  the external LittleFS, then rotates if needed. The display task never touches flash for logs.
- **Rotation** at 64 KiB: streams `current.log` into `/logs/.rotate.tmp` as a real ustar
  archive (one member `inkos-NNNNNN.log`, mode 0644, owner `inkos`) compressed with the ROM
  miniz deflater (`tdefl`, 128 probes, ~170 KB state in PSRAM) inside a standard gzip
  container (header, raw deflate, CRC-32 + size). The temp file is renamed to
  `inkos-NNNNNN.tar.gz` and `current.log` is removed. Measured: 66 404 → 6 864 bytes
  (10.3 %) in 297 ms on core 0, during a navigation burst, with no visible effect on titles
  or full refreshes.
- **Retention**: newest 16 archives and at most 512 KiB of archives. Logs never use the last
  512 KiB of free filesystem space; if space is short, the oldest archives are deleted first,
  then new batches are discarded (`discarded_batches`). A rotation that keeps failing deletes
  `current.log` beyond 128 KiB rather than letting it grow.

Archives open with any tar: `tar -xzf inkos-000002.tar.gz` (Windows 10+ includes `tar.exe`).

## Setting

`logging_enabled` (NVS key `log_on`), **default on**. Off: `logf` returns immediately, the
ring stays as it is, the writer sleeps with no periodic wakeup and no flash writes. Turning it
off logs `INKOS logging disabled` and flushes pending lines first; turning it on logs
`INKOS logging enabled`. It takes effect at once, without reboot.

- `{"cmd":"logging_set","enabled":false}` / `{"cmd":"logging_get"}` change/read it without a
  settings revision (no full-screen refresh). Studio's toggle uses this.
- `settings_get` reports `settings.logging_enabled`; `settings_set` accepts it optionally
  (absent = unchanged, so older Studio versions keep the device value).
- `status` reports `logging_enabled`, `log_dropped`, `log_file_dropped`, `log_buffered`, `log_disk`.

## Commands (USB and paired IP)

| Command | Arguments | Reply |
|---|---|---|
| `log_list` | — | `enabled`, `storage`, `ring_bytes`, `pending_bytes`, `dropped`, `file_dropped`, `discarded_batches`, `rotations`, `rotate_bytes`, `host_active`, `rotate_error`, `fs_free_bytes`, `files[{name,size,mtime}]` (archives by sequence, `current.log` last) |
| `log_read` | `name`, `offset`, `length` ≤ 2048 | `data` (base64), `length`, `size`, `eof` |
| `log_tail` | `bytes` ≤ 3072 | newest whole lines of the RAM ring, base64 `data` |
| `log_rotate` | — | archives `current.log` now (any size) |
| `log_clear` | `files` (default true) | empties the RAM ring; deletes all log files unless a USB host has the log disk mounted |

`mtime` is Unix seconds (UTC) once the clock is set by NTP, otherwise 0.

## Studio Logs view

Studio 0.7.3 adds **07 Logs**: the device logging toggle, the file list (size, modified time),
Open (downloads with `log_read`, decompresses `.tar.gz` with `System.Formats.Tar` +
`GZipStream`), a case-insensitive line filter, live tail (polls `log_tail` every 2 s and merges
overlaps), Save to disk (the file exactly as stored; tail saves as text) and Clear with an
inline confirmation. `--logs-smoke [--port COM9]` runs the same code against a real screen:
toggles logging off/on and checks the ring does not grow while off, tails, rotates, downloads
and decompresses the new archive, checks every line is a whole `INKOS` line, and saves the
archive next to the executable.

## USB drive `INKOS_LOGS` (experimental, not in the default image)

`UsbLogDisk.cpp` exposes `/logs` as a write-protected USB mass-storage disk composite with the
serial port: a synthesized 4 MiB FAT12 volume (label `INKOS_LOGS`, 2 KiB clusters, long file
names, read-only attribute) built from a snapshot of the file list taken when the host
connects. Sectors are generated on demand; file data is read from LittleFS. Writes are
rejected. While a host is connected, rotation and deletion pause so the snapshot stays valid
(appending to `current.log` continues; the disk shows the size from the snapshot).

It needs the TinyUSB (USB-OTG) stack, so it is compiled only by
`python tools/build.py --usb-otg` (`USBMode=default`). The default build uses Hardware CDC
(`USBMode=hwcdc`), which cannot add a mass-storage interface, so there `log_disk` is false.

**Why the OTG build is not the default:**

- The device enumerates as a different USB device (Seeed 2886:0063 composite instead of
  Espressif USB-Serial/JTAG 303A:1001), so Windows assigns a new COM port. Studio, `tools/flash.py`
  and scripts that assume the old port must be pointed at the new one.
- `esptool --before default-reset` cannot reset the chip over TinyUSB CDC. Serial flashing needs
  the 1200-baud touch or holding BOOT while plugging in, after which the ROM shows up as the old
  USB-Serial/JTAG port again. A firmware that crashes before USB starts has no USB serial at
  all until BOOT is held. A/B OTA over USB or IP is unchanged.
- Panic output and early boot messages are no longer on the USB port.

Host caveats: Windows caches the directory listing. A file that rotates while mounted shows up
after the drive is ejected and plugged back in. Copy files out instead of opening them in place.
The FAT image layout is checked on a host by `tests/test_host_firmware.py` (pyfatfs, when
installed).
