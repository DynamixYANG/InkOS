# InkOS

A standalone FreeRTOS e-paper dashboard for the Seeed TRMNL 7.5 OG DIY kit, with a minimal C# ShadUI configuration app.

[中文 README](README.md) · [Documentation](docs/README.md) · [Handover (Chinese)](docs/HANDOVER.md) · [Releases](https://github.com/DynamixYANG/InkOS/releases) · [MIT](LICENSE)

![Home](docs/images/calendar/home-weeks.png)

Synthetic example rendered by firmware 0.4.1; no private calendar data.

Firmware **0.7.8**, Studio **0.7.4**. The ESP32 fetches and renders data directly. No always-on PC, Tesserae or BYOS server is needed. Studio and its temporary Spotify callback are only used during setup. Page selection previews only the title (about 0.31 s for consecutive turns) before a fast full refresh of the selected page (about 1.7 s); configure the default 0.5-second wait in Studio. Since 0.7.6 the panel stays powered during navigation and the settled page uses the fast OTP full waveform: measured title 483 → 309 ms and full page 4172 → 1703 ms, with a standard full refresh after every 8 fast ones. Both can be turned off with `display_tuning_set`.

Seven pages: Home, Spotify status/artwork/lyrics, seven-day weather, weekly time grid, moon, sun, and Finnish electricity prices. Three keys provide Previous, Next and Home. Defaults are Oulu, Beijing and London, with Finnish holidays, flag days and ISO week numbers. Week places dates above seven time-grid columns, all-day spans above timed event blocks, and a minute-updated current-time marker.

The Energy chart centers the current UTC hour between 12 earlier and 12 later hours. Default fees are VAT 25.5%, margin 0.45 ct/kWh and transmission 6 ct/kWh, with both fees before VAT. Home displays EUR/kWh; the detail chart uses ct/kWh.

Hardware: XIAO ESP32-S3 Plus, 16 MiB flash, 8 MiB OPI PSRAM, UC8179 800×480 monochrome panel. Typography uses native Terminus bitmap glyphs and IBM Plex Mono Light. Factory refresh waveforms prioritize contrast; frequent Home updates are off by default after an earlier experimental waveform produced faded text.

Build with Arduino ESP32 core **3.3.12**, Python **3.11+** and .NET **10 SDK**. See [build](docs/BUILD.md), [hardware](docs/HARDWARE.md), [architecture](docs/ARCHITECTURE.md) and [development](docs/DEVELOPMENT.md). The Windows Studio distribution includes its runtime; keep its complete folder together. Studio configures Wi-Fi over USB or paired IP. USB provisions a private AES-256-GCM key; all settings then use the same app pages over IP. See [network setup](docs/NETWORK.md).

Device logs go to a 16 KiB non-blocking RAM ring, then every 15 s to the external LittleFS, rotating at 64 KiB into `.tar.gz` archives. Retention is selectable from 1 week to 6 months (default 35 days) with a space cap; at the measured rate about 42 days fit. Studio 0.7.4's Logs page shows storage usage and the capacity estimate, and views, filters, tails, saves, multi-deletes and clears logs. USB stays a plain serial port (no USB drive). See [device logs (Chinese)](docs/LOGS.md).

![Studio Logs](docs/images/studio/logs.png)

Architecture: input, display and control tasks run on core 1; network, log-store and ota-health on core 0. Diagrams (mermaid sources and PNGs) are in [docs/images/architecture](docs/images/architecture/); the [handover](docs/HANDOVER.md) covers design, the serial-log stall root cause, operations and pending hardware checks.

![Architecture](docs/images/architecture/1-architecture.png)

Calendars use read-only ICS/webcal subscriptions from Apple, Google and Outlook. Private CalDAV and calendar-account OAuth are not implemented. Spotify needs your own developer app, account authorization and a matching timed-lyrics record. Firmware tries LRCLIB exact lookup, candidate search and then direct NetEase LRC retrieval, with artist/duration/version checks and Chinese character folding. Live Spotify authorization and private Google Calendar fetching have been verified. Chinese song metadata, lyrics and calendar text use bundled Adobe Source Han Sans SC Light monochrome fallback glyphs with measured mixed-script layout. BMP simplified/traditional Chinese is covered; emoji and supplementary-plane ideographs are not.

Firmware 0.6.0 passed 119/119 startup checks, external filesystem migration, bidirectional IP OTA, automatic reboot and real pending-boot rollback. Settings were retained. Source Han Light previews and actual partition/usage pie charts are included; see [storage](docs/STORAGE.md) and [validation](docs/VALIDATION.md). Historical live LRCLIB/NetEase probes remain recorded separately; this release does not claim a new long-term optical ghosting test.

Original project code is MIT licensed. Third-party licenses and external service/data terms remain separate; see [notices](docs/THIRD_PARTY.md).

Firmware 0.7.6–0.7.8 were installed by USB A/B OTA and confirmed healthy; Studio self-test 14/14 and the on-device logs smoke test passed, and the logging switch and retention survive reboots. Long-term ghosting and low-temperature behaviour of the fast full refresh still need physical validation.

Remote upgrades: see [OTA, A/B partitions and external Flash](docs/OTA.md). The 0.6 partition/filesystem migration requires USB; later upgrades use Studio → Network → Firmware over paired IP.
