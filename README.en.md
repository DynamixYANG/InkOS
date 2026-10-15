# InkOS

A standalone FreeRTOS e-paper dashboard for the Seeed TRMNL 7.5 OG DIY kit, with a minimal C# ShadUI configuration app.

[中文 README](README.md) · [Documentation](docs/README.md) · [Releases](https://github.com/DynamixYANG/InkOS/releases) · [MIT](LICENSE)

![Home](docs/images/screen/01-home.png)

Firmware **0.3.1**, Studio **0.3.0**. The ESP32 fetches and renders data directly. No always-on PC, Tesserae or BYOS server is needed. Studio and its temporary Spotify callback are only used during setup.

Seven pages: Home, Spotify status/artwork/lyrics, seven-day weather, weekly agenda, moon, sun, and Finnish electricity prices. Three keys provide Previous, Next and Home. Defaults are Oulu, Beijing and London, with Finnish holidays and flag days.

The Energy chart centers the current UTC hour between 12 earlier and 12 later hours. Default fees are VAT 25.5%, margin 0.45 ct/kWh and transmission 6 ct/kWh, with both fees before VAT. Home displays EUR/kWh; the detail chart uses ct/kWh.

Hardware: XIAO ESP32-S3 Plus, 16 MiB flash, 8 MiB OPI PSRAM, UC8179 800×480 monochrome panel. Typography uses native Terminus bitmap glyphs and IBM Plex Mono Light. Factory refresh waveforms prioritize contrast; frequent Home updates are off by default after an earlier experimental waveform produced faded text.

Build with Arduino ESP32 core **3.3.12**, Python **3.11+** and .NET **10 SDK**. See [build](docs/BUILD.md), [hardware](docs/HARDWARE.md), [architecture](docs/ARCHITECTURE.md) and [development](docs/DEVELOPMENT.md). The Windows Studio distribution includes its runtime; keep its complete folder together.

Calendars use read-only ICS/webcal subscriptions from Apple, Google and Outlook. Private CalDAV and calendar-account OAuth are not implemented. Spotify needs your own developer app, account authorization and a matching LRCLIB entry for timed lyrics. Live Spotify authorization and private-calendar verification remain pending. Supported glyphs are Latin/Finnish; CJK lyrics need another font pack.

Device firmware passed 61/61 startup checks. Exact framebuffer previews and a sanitized hardware validation summary are included; see [validation](docs/VALIDATION.md) for the difference between recorded tests, current source checks and physical display acceptance.

Original project code is MIT licensed. Third-party licenses and external service/data terms remain separate; see [notices](docs/THIRD_PARTY.md).
