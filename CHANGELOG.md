# 固件 0.7.9 — 首页三日预报与月出月落

- Home 天气区：体感与风速合并为一行（温度图标 + `Feels 7°`，风图标 + `4 m/s`），腾出的空间显示接下来三天：星期缩写（14 px）、原生 24 px 天气图标、`高°/低°`（14 px，过宽时省略 °，无数据显示 `--/--`）。三列宽 64 px，位于 x 12–204、y 392–458，取当地日期晚于今天的前三个 `days[]`，没有缩放。
- Home 月相区：月相图上移（中心 270,354 → 270,318，半径 33），下方两行显示月出／月落：自绘 18 px 图标（地平线 + 细线月牙 + 上／下箭头，与太阳事件图标同一风格）和 20 px 时间，基线 y 391 / 425。当天没有月出或月落时显示 `--:--`；整天在地平线上／下时显示 `ABOVE HORIZON` / `BELOW HORIZON`。
- 月出月落在设备本地计算（天气源不提供）：新 `MoonEvents` 按 Meeus《Astronomical Algorithms》第 47 章（ELP-2000/82 截断表 47.A/47.B，表格见 `MoonTerms.h`）计算月球位置，加章动主项、GMST、ΔT = 69.2 s；地平高度阈值 h0 = 0.7275·π − 0.5667°（含视差与大气折射）。在所配置地点的 Europe/Helsinki 本地日内以 10 分钟步长寻找穿越，二分到 1 s，按分钟四舍五入显示；结果按本地日与坐标缓存。
- 验证：与 USNO（aa.usno.navy.mil rstt/oneday）逐日对比奥卢（65.0121°N, 25.4651°E）2026 全年 365 天：498 个事件中 486 个完全一致、12 个相差 ±1 min，无事件有无不一致；47 个“整天在地平线上”和 57 个“整天在地平线下”的日子全部一致。主机测试 `tests/host/moon_events_test.cpp` 覆盖奥卢、朗伊尔城（78°N）和悉尼共 20 个日子（允许 ±2 min）及 Meeus 例 47.a；设备自检新增 3 项。
- 合成预览（fixture 0/3/5/8）加入 7 日天气测试数据，不含私人日历内容。

# 固件 0.7.8 / Studio 0.7.4 — 日志存储、删除与保留期

- 新命令 `fs_info`：LittleFS 总量／已用／空闲、块大小、保留余量；按类别（logs、calendars、energy、other）和顶层目录统计字节与文件数；日志预算、归档数量与时间范围；内部字库分区占用。一次遍历小目录树，在日志锁内完成。
- 新命令 `log_delete`：参数 `name` 或 `names[]`（≤64），只允许 `/logs/` 下的 `current.log` 与 `inkos-NNNNNN.tar.gz`；任一名称含 `..`、其他目录或其他文件则整批拒绝，先校验后删除，返回 `deleted[]` 与 `missing[]`。删除 `current.log` 持有写入任务的锁，不会与落盘或轮转竞争；下一次落盘自动重建。
- 新设置 `log_retention_days`（7/21/35/90/180 天，默认 **35**，NVS `log_ret`）：经 `logging_set`、`settings_get/set`（可选字段）和 `status` 读写，立即生效。早于保留期的归档按轮转时间删除；时钟未经 NTP 校准时绝不按时间删除。
- 裁剪时机：开机、每次轮转、设置变更，以及日志开启且时钟有效时每天一次；关闭日志时写入任务无周期唤醒。容量兜底依次为：最多 300 个归档、预算 = 50 % ×（LittleFS − 512 KiB 余量 − 非日志数据，至少 256 KiB）、始终保留 512 KiB 空闲；最旧的先删。规则独立为 `LogPolicy`，用伪时钟做主机测试。
- 实测：LittleFS 4,194,304 B（4 KiB 块），清空日志后已用 61,440 B；字库在内部分区，占 4,185,866 / 4,194,304 B；归档预算 1,804,288 B。空闲时约 350 KB/天原始日志，压缩后约 36 KB/天，约可保存 **42 天**；3 个月和 6 个月超出容量，此时由空间上限先删旧日志。
- 移除实验性 USB 大容量存储（`INKOS_LOGS` 盘、虚拟 FAT）和 `tools/build.py --usb-otg`；USB 保持 Hardware CDC，COM 口和 esptool 复位流程不变。
- 写入任务栈增至 12 KiB；列表与裁剪工作区放在 PSRAM。
- Studio 0.7.4：Logs 页新增 LittleFS 用量条（日志／日历／电价／其他／空闲／余量）、字库分区、容量估算和“space limit will remove older logs first”提示、保留期下拉框（立即应用）、多选 **Delete selected…**（行内确认），保留 **Clear all**。`--logs-smoke` 扩展为：设置并恢复保留期、`log_rotate` 三次后核对 `fs_info`、设备端路径校验、删除所选、删除 `current.log`、清空；全程不发送 `settings_set`。
- 验证：主机测试通过；Studio 自检 14/14；COM9 实机 `--logs-smoke` 通过；`logging_enabled` 与保留期重启两次后保持。经 USB A/B OTA 安装到 ota_1，0.7.7c 保留在 ota_0。

# 固件 0.7.7 / Studio 0.7.3 — 设备持久日志

- 日志环 v2：16 KiB PSRAM 整行 FIFO，写入方从不阻塞；USB 与文件两个读指针各自独立计数丢失行；新增 `log_tail` 和运行时开关。
- 新 `log-store` 任务（core 0，优先级 1）每 15 s 或积累 8 KiB 时把日志写入外部 LittleFS 的 `/logs/current.log`；达到 64 KiB 后流式压缩为标准 ustar + gzip 归档 `inkos-NNNNNN.tar.gz`（ROM miniz deflate）。实测 66,404 → 6,864 B（10.3 %），在 core 0 用时 297 ms，期间标题与全刷无可见影响。0.7.7 保留 ≤16 个归档／512 KiB，并保留 512 KiB 空闲（0.7.8 改为保留期 + 容量策略）。
- 新设置 `logging_enabled`（NVS `log_on`，默认开启）：关闭后 `logf` 立即返回，不再写 Flash；可经 `logging_set` 或 `settings_set` 立即切换，`status` 报告当前值。
- 新命令 `log_list`、`log_read`、`log_tail`、`log_rotate`、`log_clear`，USB 与已配对 IP 均可用。见 [设备日志](docs/LOGS.md)。
- 曾试验 `tools/build.py --usb-otg` 只读 USB 日志盘；因会改变 USB 设备、COM 口和 esptool 复位流程，0.7.8 已决定移除。
- Studio 0.7.3：新增 Logs 页（开关、文件列表、.tar.gz 查看、过滤、实时尾随、保存、清空）及 `--logs-smoke`。

# 固件 0.7.6 — 更快的导航刷新（实验性）

- 刷新分段计时：`INKOS display` 日志行、`status.display_phase_us` 与 `/display` 报告上电、SPI 传输、0x12 波形、断电与总时长。
- 导航期间保持面板驱动上电（`hold_power`）：后续标题和选定页全刷直接复用，其他刷新先断电，空闲 `idle_off_ms`（默认 3000 ms）后强制断电。
- 仅对“提交选页”的整页全刷使用快速 OTP 全刷波形（E0=0x02、E5=0x5A，`fast_full`）；开机、恢复和维护全刷仍为标准波形；连续 8 次快速全刷后下一次用标准波形，30 分钟维护计时以标准全刷为准。
- 全帧单次 SPI 突发传输，删除多余的刷新后 DTM1 回写（已设置 N2OCP），以 BUSY 边沿等待取代固定 2 ms 命令延时。SPI 保持 10 MHz。
- 实测（与 0.7.4 对比）：连续标题 **483 → 309 ms**（第一次标题 440 ms），选定页全刷 **4172 → 1703 ms**，第 9 次标准全刷 3976 ms，请求到提交中位数 4780 → 2266 ms；关闭两个开关时标题 480 ms、全刷 4106 ms。
- 修复 0.7.x 既有的“粘滞变慢”：USB 插着但没有程序读取串口时，4 KB HWCDC 发送缓冲写满，此后每条显示／网络诊断 `Serial.printf` 都会阻塞最多约 2 s，标题和整页延迟数秒，直到重启。任务诊断改写入 16 KiB PSRAM 整行 FIFO，满时淘汰最旧行（`status.log_dropped` 计淘汰行数，`log_buffered` 计保留字节）；control 任务仅在发送缓冲接近空时非阻塞排空，之后再打开串口可按顺序读到最新的保留行。命令回复格式不变。
- 新命令 `display_tuning_get` / `display_tuning_set`（`hold_power`、`fast_full`、`idle_off_ms`），各自独立 NVS 键；兼容 Studio 0.7.2。残影、低温和长时间保持驱动电压的影响需要实物验证。

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
