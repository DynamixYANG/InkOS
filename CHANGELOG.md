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
