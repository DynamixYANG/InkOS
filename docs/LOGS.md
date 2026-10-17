# Device logs (firmware 0.7.8, Studio 0.7.4)

InkOS keeps its `INKOS ...` diagnostic lines (display timing, navigation, Wi-Fi, weather,
calendar, energy, OTA) on the device so they can be read later over USB or paired IP.
The USB mode stays Hardware CDC; there is no USB mass-storage drive.

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
- **Retention** (pure rules in `LogPolicy.cpp`, host-tested), oldest archive first:
  1. archives whose rotation time is older than `log_retention_days` (7/21/35/90/180, default
     **35**) — only once NTP has set the clock; archives rotated before the clock was set have no
     time and are kept by age;
  2. at most **300** archives and an archive byte budget of **50 %** of
     (LittleFS size − 512 KiB reserve − non-log data), at least 256 KiB;
  3. at least **512 KiB** always stays free (calendar/energy caches). If space is short, the
     oldest archives go first, then new batches are discarded (`discarded_batches`).
  Pruning runs at boot, at every rotation, when a log setting changes and once a day while
  logging is enabled. With logging disabled there is no periodic wake: only boot and setting
  changes prune. The archive time is the file's mtime, falling back to the gzip header MTIME.
  A rotation that keeps failing deletes `current.log` beyond 128 KiB.

Archives open with any tar: `tar -xzf inkos-000002.tar.gz` (Windows 10+ includes `tar.exe`).

## Storage (measured on the device, 0.7.8)

- LittleFS on the external 4 MiB SPI flash: 4 194 304 bytes, 4 KiB blocks. Typical use 84 KiB:
  calendars ~38 KB, energy ~1.6 KB, logs, plus block rounding/metadata.
- Fonts/assets are **not** on LittleFS: they occupy 4 185 866 of the 4 194 304-byte internal
  `fonts` partition.
- Archive budget with today's data: 1 804 288 bytes (1.72 MiB).
- Idle log rate ≈ 4 bytes/s raw (≈ 350 KB/day: minute clock refreshes, weather, calendar);
  rotated archives compress to ~10 %, so ≈ 36 KB/day and ~6.8 KB per 64 KB archive (8 KiB in
  4 KiB blocks). The budget then holds ≈ 220 archives ≈ 41 days (the 300-file cap is not reached). 1, 3 and 5 weeks fit; 3 and 6
  months do not at this rate — heavy navigation logs more. The space limit removes older
  archives first; Studio warns when the estimate is below the chosen retention.

`fs_info` reports `littlefs` (total/used/free/block/reserve), `categories` (logs, calendars,
energy, other: bytes and files), `dirs` (per top-level directory), `log` (current/archive bytes,
archive count, oldest/newest archive time, budget, max files, retention, clock valid, pruned)
and `fonts` (partition size, used). It walks the small directory tree once under the log lock.

## Settings

`log_retention_days` (NVS `log_ret`, default 35): `logging_set {"retention_days":N}` applies it
immediately (prune), `settings_get`/`settings_set` carry it optionally, `status` reports it.

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
| `log_delete` | `name` or `names[]` (≤ 64): `current.log`, `inkos-NNNNNN.tar.gz`, optionally prefixed `/logs/` | `deleted[]`, `missing[]`; any other path (`..`, other directories, other files) rejects the whole request |
| `log_clear` | `files` (default true) | empties the RAM ring; deletes all log files |
| `fs_info` | — | storage breakdown (above) |

Deleting `current.log` happens while holding the writer's lock, so it never races a flush or
rotation; the next flush re-creates it with lines still pending in RAM.

`mtime` is Unix seconds (UTC) once the clock is set by NTP, otherwise 0.

## Studio Logs view

Studio 0.7.4 **07 Logs**: the device logging toggle, a retention dropdown (applied immediately),
LittleFS storage bar with logs/calendars/energy/other/free/reserve, the fonts partition, the
capacity estimate and the space-limit warning, a multi-select file list with **Delete
selected** (inline confirmation) and **Clear all**, the file list (size, modified time),
Open (downloads with `log_read`, decompresses `.tar.gz` with `System.Formats.Tar` +
`GZipStream`), a case-insensitive line filter, live tail (polls `log_tail` every 2 s and merges
overlaps), Save to disk (the file exactly as stored; tail saves as text) and Clear with an
inline confirmation. `--logs-smoke [--port COM9]` runs the same code against a real screen:
toggles logging off/on and checks the ring does not grow while off, tails, sets retention and
restores it, creates three archives and checks `fs_info`, downloads and decompresses one,
checks path validation is enforced by the device, deletes two selected archives and
`current.log`, clears all, and checks the numbers at each step. It never sends `settings_set`.
