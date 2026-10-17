# Firmware 0.7.6 — Faster navigation refresh (experimental)

- Log and report per-phase refresh timing (power-on, setup/SPI transfer, 0x12 waveform, power-off, total) on the `INKOS display` line, in `status` (`display_phase_us`) and `/display`.
- Keep panel drive power on across a navigation sequence; the settled full refresh or next title reuses it, any other refresh powers off first, and an idle timeout (default 3000 ms) forces power-off.
- Use the fast OTP full waveform (E0=0x02, E5=0x5A) only for the full refresh that commits a navigation selection. Boot, recovery and maintenance fulls stay standard; every 8th consecutive settled full and the 30-minute maintenance timer use the standard waveform.
- Send full frames in one SPI burst, drop the redundant post-refresh DTM1 rewrite (N2OCP is set), and replace fixed 2 ms command delays with an edge-driven BUSY wait. SPI stays at 10 MHz.
- Fix a pre-existing (0.7.x) sticky slowdown: once the 4 KB USB TX buffer filled while USB stayed plugged but no program read the port, every display/network diagnostic `Serial.printf` blocked up to 2 s (HWCDC backpressure), delaying titles and settled pages by many seconds until reboot. Task diagnostics now use a non-blocking `logf()` that drops lines when the buffer is full (`status.log_dropped`); command responses are unchanged.
- New `display_tuning_get` / `display_tuning_set` commands (`hold_power`, `fast_full`, `idle_off_ms`) in separate NVS keys; Studio 0.7.2 remains compatible. Ghosting, low-temperature behaviour and long-held drive power require physical validation.

# Firmware 0.7.5 — Thin rounded weather symbols

- Replace the weather symbol family with pinned Lucide outlines, adapted to thin native 16/24/40/64 px monochrome masks with rounded caps and joins.
- Use matching thermometer, droplet and wind symbols beside Home and Weather metrics. Preserve page layout, existing text fonts and the accepted 0.5-second navigation waveform.
- Keep original SVGs, source hashes, generator and upstream ISC / inherited MIT notices for reproducible asset updates.

# Firmware 0.7.4 — Experimental 500 ms title erasure

- User rejected the 0.7.3 trial because old title glyphs remained visible.
- Extend both differential drive phases from one to six frames, targeting roughly 0.5 seconds of actual presentation and stronger old-glyph erasure.
- Keep title-window isolation, factory body/full refresh, voltages/PLL, selection quiet interval and encoder lock unchanged. Optical acceptance remains a physical test.

# Firmware 0.7.3 — Experimental 300 ms navigation titles

- Trial a short balanced register LUT only for guarded title-only navigation windows; retain OTP for body partials and full refresh.
- Fully clear every LUT table, keep existing voltages/PLL, border Hi-Z, gate-window scan and VCOM float/power-off. Keep settled-page cleanup and 2-second encoder lock.
- Preserve the 0.7.2 OTA image for fallback. Optical clarity and ghosting require physical user feedback.
- Studio 0.7.2 remains compatible; no app setting or account migration is needed.

# Firmware / Studio 0.7.2 — Navigation-first scheduling

- Remove the extra 25–80 ms title debounce. Validated detents now wake the input task directly through an ISR notification instead of polling every 2 ms.
- Read a small navigation/render identity before copying page content. Idle loops and selection waits no longer copy the 99 KB model; render a candidate page once while waiting and discard it when superseded.
- Start selection quiet time at the last input, overlapping the unchanged factory title waveform. Keep the final title visible for at least 100 ms before full refresh; Studio describes this timing explicitly.
- Coalesce background redraws for 40 ms (150 ms cap), always yielding to navigation before non-cancellable panel work. Add dispatch/title/acceptance timing and snapshot counters for verification.
- Preserve the existing configurable encoder lock, fonts, flash layout, network/calendar/Spotify credentials and proven refresh waveforms.

# Firmware / Studio 0.7.1 — Settled-page encoder lock and responsive navigation

- Add an encoder-only lock when a settled selection is accepted for full refresh: default 2000 ms, configurable 0–10000 ms (0 disables) in Studio over USB or paired IP.
- Drop turns captured during the lock, clear pending/partial detents, and recheck the gate atomically with navigation. Home and other navigation controls remain available. The lock starts before full refresh, not after completion.
- Isolate input from blocking web/USB requests in a dedicated task. Coalesce rapid title updates for 25 ms (80 ms maximum); skip superseded targets before sending a partial update.
- Keep the verified factory partial waveform, page quiet delay, font archive, external filesystem and existing accounts.

# Firmware / Studio 0.7.0 — Rotary input, expanded layouts and private iCloud calendars

- Add GPIO41/42 quadrature input, full-cycle bounce rejection and KEY3 Home; preserve title-preview navigation and its configurable quiet period.
- Reduce outside margins to 12 px, remove all page footers, replace page IDs with ADC battery estimates and three-level Wi-Fi status, and rebalance all seven pages without scaling fonts.
- Move world clocks beside Home's main clock; simplify moon/sun metadata and align bold ISO week numbers with date numerals.
- Use pinned MDI 7.4.47 weather SVGs, adapted to a lighter stroke with preserved rain/snow/star details and fine sun rays, as native 40/64 px monochrome masks. Label the forecast page WEATHER / location.
- Show today plus six days in the agenda, with local-date/DST boundaries, today in the first column and a matching forward fetch horizon.
- Add Studio's private iCloud CalDAV calendar discovery and explicit calendar selection; ESP32 directly fetches the selected collection using a dedicated Apple app-specific password. Keep credentials out of settings reads and preserve existing Google/Outlook/public subscriptions.
- Validate Apple-only HTTPS redirects, namespace-aware bounded XML, invalid settings, and source-specific external agenda caches. Live account configuration requires user entry in Studio.

# Firmware / Studio 0.6.1 — Preview page selection before full refresh

- Partial-refresh only the top-left page title during navigation; coalesce repeated requests and full-refresh the final page after a quiet period measured after the title is visible.
- Add a persisted 100–5000 ms page-selection delay (default 500 ms) to Studio's Device page, USB and paired IP settings. Preserve the existing settings blob, accounts, font assets and flash layout.
- Keep factory partial waveforms and defer maintenance full refreshes until selection settles. Superseded frames are discarded before hardware refresh begins.
- Fix Studio's firmware compatibility check to accept the 0.6 series.

# Firmware / Studio 0.6.0 — External filesystem and larger OTA slots

- Moved the complete LittleFS filesystem to the 4 MiB U5, preserving its files through a backed-up, readback-verified USB migration and growing the volume to fill the chip.
- Enlarged internal A/B application slots to 5.9375 MiB each; kept NVS at its original offset and shared Source Han Light assets in a 4 MiB internal partition.
- Centralized external SPI device/partition/mount ownership in Storage, disabled automatic formatting on mount failure, and exposed actual usage through paired storage_info plus reproducible pie-chart reports.
- Verified final IP upgrades in both directions, automatic reboot, pending-boot rollback, settings preservation and filesystem write/read/remove. See [validation](docs/VALIDATION.md) and [storage](docs/STORAGE.md).

# Firmware 0.4.3 / lyrics sources

- Added LRCLIB candidate search after exact lookup, followed by direct NetEase search and LRC retrieval. No always-on server or new account login is needed.
- Added character-level Chinese simplification, twelve common artist aliases and version/duration/artist checks; reject mismatched live, remix, cover, language and speed variants.
- Isolated provider resolution, text matching and ESP32 transport from Spotify playback. Publish current playback before lyric lookup; preserve original display text and existing font/refresh behavior.
- Added bounded sequential requests, provider cooldowns including Retry-After dates, defensive timestamp validation and non-invasive USB live probes.
- Details: [Lyrics sources](docs/LYRICS-SOURCES.md).

# Firmware 0.4.2 / Chinese text

- Added 28,400 Noto CJK fallback glyphs at four native monochrome sizes, covering BMP simplified/traditional Chinese and punctuation; original Latin/Finnish fonts are unchanged.
- Shared glyph measurement across text, ellipsis and wrapping; Chinese titles, locations, Spotify metadata and timed lyrics now use full-width advances instead of Latin character counts.
- Kept metadata truncation on UTF-8 boundaries and validated malformed UTF-8 safely.
- Added mixed-script rendering checks and synthetic Chinese Week, Music and Home USB previews. Refresh waveforms, settings and Studio are unchanged.
- Details: [Display and fonts](docs/DISPLAY.md).

# Firmware 0.4.1 / calendar grid

- Home month calendar adds a left ISO week-number rail with the current week underlined; handles ISO year boundaries and six-row months.
- Separated week numbers from dates with whitespace, removed the long inner divider, and kept Today as the only solid month-grid highlight.
- Moved the Home location label up 8 px and large clock/colon up 20 px; redistributed space below the clock through the date and world-clock sections.
- Replaced Week's agenda rows with seven date columns, a bounded hourly grid, two all-day span rows, parallel event blocks, per-date overflow counts and a current-time line.
- Added pure bounded calendar layout helpers, year-boundary/overlap/midnight/DST startup checks and non-invasive USB synthetic framebuffer previews. Existing font assets, refresh waveforms, settings and Studio 0.4.0 are unchanged.
- Details: [Calendar layout](docs/CALENDAR.md).

# Firmware / Studio 0.4.0

- Added a minimal Network page with Wi-Fi SSID, keep/replace/open password modes, and a separate Save Wi-Fi action. Passwords are never read back.
- Added USB / IP connection selection. USB provisions or revokes a private 256-bit key; IP settings use AES-256-GCM with one-time, expiring challenges and direction-bound responses.
- Shared firmware command dispatch and desktop DeviceClient across transports, including calendar, place/time, Spotify, energy and navigation.
- Wi-Fi uses an atomic NVS JSON item with legacy fallback, delayed application after acknowledgement, and reconnects for password changes on the same SSID. Network edits never change the flash partition or display waveform.
- Added UI binding checks and opt-in real-device network smoke coverage. See [network setup](docs/NETWORK.md).

# Firmware 0.3.1 / layout revision

- Home uses the common compact header, with only lightning and current EUR/kWh price on the title baseline. Removed the miniature price bars and secondary price caption.
- Unified outer margins, Home column gutters and section rules; moon and solar events now have a dedicated vertical divider. Retained the accepted native font assets and factory refresh policy.
- Energy now centers the current hour in a 25-hour elapsed-time window, with 12 earlier and 12 later hours. Extrema refer to that window; unavailable prices remain gaps and repeated DST hours retain a/b labels.
- Weather keeps temperature/rain graphs, seven daily icons, high/low, rain and wind. Removed the provider/update footer, UV and duplicate sunrise rows.
- Week uses a date rail and aligned time/event/place columns, two stacked events per day and a reserved overflow column. Clipped text never overwrites another field.
- Settings, source adapters, price calculation, storage layout, display driver and Studio are unchanged.

# 0.3.0

Added Moon, Sun and Energy full pages, then compact Home summaries. Added local approximate astronomy, polar/day-boundary handling, FI hourly prices, DST/negative/missing-data handling, and configurable VAT/margin/transmission with a fee-tax option. New Studio Energy section supports two presets and a compatible custom HTTPS API with a write-only optional credential. Existing configuration and conservative refresh are preserved. Details: [Energy documentation](docs/ENERGY.md).

# Studio 0.2.1 / display quality revision

- Migrated the desktop UI from WPF to real ShadUI 0.2.4 / Avalonia 12.1 / .NET 10. Preserved embedded IBM Plex Mono, USB protocol, calendars, weather, world clocks and Spotify PKCE. Four separate views, a restrained sidebar, white surfaces and thin separators. Windows x64 publish includes its runtime.
- Added command gating, coordinate parsing and masked-URL two-way binding checks. Preserved temporary browser authorization; no normal desktop server is required.
- User rejected the prior display result: sustained fast updates faded static text and polluted the background. Gate-window restriction and power-off alone were insufficient.
- Removed custom short partial LUTs from both animation and lyrics. Only factory OTP partial waveforms remain. Disabled frequent Home updates on the device and by default; minute clock updates continue. Re-enabling also enables colon blinking, with cadence limited by factory waveform duration.
- Settings changes force a full redraw. Startup clears white before drawing; partial windows gate inside only and power off afterward. Physical contrast acceptance is pending user observation.

# 0.2.0 (historical behavior; refresh policy superseded above)

- Fourth page: Monday–Sunday week agenda with event times, titles, locations, all-day/spanning events and overflow counts.
- Home Up Next section shares the normalized agenda. Finnish public holidays and flag days remain visible.
- KEY1 = Previous, KEY2 = Next, KEY3 = Home; page navigation wraps in both directions.
- Home colon is drawn at numeral mid-height and toggles every second. Two configurable world clocks default to Beijing and London.
- Weather icon animation targets a 500 ms cadence: rotating sun rays, moving overlapping clouds and falling rain. A dedicated partial waveform and periodic regional balancing limit ghosting. Panel-busy periods and maintenance refreshes can skip animation frames.
- Location, supported time zones, clock labels, weather provider and subscriptions are configurable over USB.
- Added MET Norway Locationforecast (the weather service behind yr.no) beside Open-Meteo. Daily summaries are derived from forecast samples. Missing sunrise/other fields display placeholders.
- Added C# WPF InkOS Studio, including calendar subscription setup and Spotify PKCE/account controls. No PC server is needed for routine operation.
- Added bounded ICS processing, recurrence expansion, exclusions, per-instance overrides, HTTPS redirects/compression and local calendar caching.

Calendar bounds: 512 KiB per feed, 256 active series/overrides per feed, 32 exclusion/additional dates per event, 35-day expansion window and 96 nearest retained occurrences. Week view shows two events per day plus an explicit overflow count. Unknown time zones or unsupported recurrence forms return a visible sync error and retain the previous agenda.

The initial font pack still covers Latin/Finnish rather than CJK. Account-specific calendar feeds and live Spotify require the user's own subscription links and authorization.

## 0.5.0 — Native A/B OTA and shared font assets

- Two 4 MiB application slots in internal boot Flash; native ESP-IDF rollback and delayed health confirmation.
- Immutable native CJK glyph archive compressed losslessly into the shared 4 MiB internal font partition; original pixel raster retained.
- Existing NVS and LittleFS offsets preserved. USB migration requires a full internal Flash backup before changing the layout.
- Paired AES-256-GCM IP uploads, inactive-slot-only writes, board/layout/font checks, full SHA-256 validation and automatic restart.
- Studio Network → Firmware picker/progress/boot verification; app-only OTA ZIP and CLI client.
- Chinese fallback now uses Adobe Source Han Sans SC Light 2.005 at four native 1-bit sizes; Latin/Finnish typography is unchanged.
- Corrected this EE04 unit's external Flash routing to CLK11/MOSI13/MISO12/CS40 after GPIO and RDID measurements; optional NOR remains independent of native A/B boot.
