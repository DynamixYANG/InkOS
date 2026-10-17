# USB 与设备本地调试协议

## USB framing

115200 baud、UTF-8、每条命令一个 JSON 对象加 LF。Studio 设置 DTR=true、RTS=false。启动日志和协议响应可能混排；客户端只解析 JSON 对象并匹配 inkos=true 与预期响应字段，不假设每行都是 JSON，也不把数字行当对象。

单条输入上限 4095 bytes，溢出会拒绝该命令。写操作一般返回 `{"inkos":true,"saved":true}`，失败时 saved=false 并可能有 message。不要记录原始包含设置的响应。

| cmd | 关键字段 | 结果 |
|---|---|---|
| status | 无 | version、page、wifi、spotify、time、heap 等 |
| settings_get | 无 | settings、feeds、energy；feeds 含私人 URL |
| settings_set | settings 对象 | 更新地区、天气与两个世界时钟 |
| icloud_get | 无 | 0.7 起，返回 enabled、username、calendar_url/name 和 has_password；不返回密码 |
| icloud_set | enabled、username、calendar_url/name、password_mode、可选 password | 0.7 起，保存单个私有 iCloud 日历；mode=0 保留密码（同账号），1 替换；仅 Apple CalDAV HTTPS |
| icloud_disconnect | 无 | 清除设备 iCloud 凭据，不撤销 Apple 端专用密码 |
| feed_set | index=0..2、name、provider、url、enabled | 保存一个只读日历源 |
| energy_set | energy 对象 | 更新成本参数与来源，凭据按 key_mode 处理 |
| spotify | client_id、refresh_token | 存入设备；不是 Client Secret |
| spotify_disconnect | 无 | 移除设备授权和相关播放内容 |
| wifi | ssid、password、password_mode | 保存 Wi-Fi；mode 0 保留、1 更换、2 开放网络 |
| wifi_get | 无 | ssid、has_password；从不返回密码 |
| remote_pair | enabled | 仅 USB；启用时返回一次 pairing_key，禁用时撤销 |
| page | page=0..6 | 绝对切页 |
| navigate | action=prev/next/home | 相对切页 |
| icon_test | code | 临时图标检查，约 15 秒恢复 |
| lyrics_status | 无 | 仅 USB，0.4.3 起；上次正常播放查询的来源、请求数、候选数、状态与 HTTP 状态，不含歌词或账号内容 |
| lyrics_probe | 可选 fixture=0..2 | 仅 USB，0.4.3 起；有 fixture 时提交固定曲目的联网测试；省略时读取任务状态。0=LRCLIB 搜索，1=网易云，2=自动回退。返回 job/queued/running/complete/source/timed/lines，不改变真实播放或配置 |
| calendar_preview | fixture=0..8、offset | 仅 USB，0.4.1 起；返回合成预览 preview_chunk（Base64，最多 1024 字符）、offset、total，不改变实体屏幕或日程；6..8 为 0.4.2 中文 Week／Music／Home 样例 |

## 普通设置示例

```json
{"cmd":"settings_set","settings":{"location":"Oulu","zone":"Europe/Helsinki","latitude":65.0121,"longitude":25.4651,"weather_source":0,"animations":false,"met_contact":"","worlds":[{"label":"Beijing","zone":"Asia/Shanghai"},{"label":"London","zone":"Europe/London"}]}}
```

weather_source：0 Open-Meteo，1 MET Norway。worlds 必须是两个有效时区与标签。时区只接受 Zones.cpp 的目录；坐标和来源有界。设置变化递增 revision 并触发全刷。

0.6.1 新增 settings.page_switch_delay_ms：整数 100–5000，默认 500。settings_get 返回此字段；settings_set 省略时保留原值，越界或小数拒绝保存。该设置单独存入 NVS 的 page_delay，保持已有 settings2 二进制布局与账号配置兼容。page/navigate 的响应表示目标已选中，正文会在标题局刷完成并经过停按等待后更新；status.page 仍表示选择的目标页。

## 日历与电价示例

```json
{"cmd":"feed_set","index":1,"name":"Personal calendar","provider":"Google","url":"https://calendar.example.invalid/private/basic.ics","enabled":true}
```

上例为不可用占位地址。实际私密 URL 只填在本机上位机中。webcal 会转成 HTTPS；URL 上限 1500 字符。

```json
{"cmd":"energy_set","energy":{"enabled":true,"provider":0,"vat_percent":25.5,"margin_ct":0.45,"transmission_ct":6.0,"fees_include_vat":false,"endpoint":"https://sahkotin.fi/prices","api_header":"Authorization","key_mode":0,"api_key":""}}
```

provider 0/1/2 分别为 Sähkötin、Porssisahko、自定义 HTTPS。key_mode 0 保留、1 替换、2 删除。settings_get 只返回 has_key。现有命令不是整份配置事务：Studio 顺序保存 Settings、各 feed、Energy，失败时需要查看最后成功状态再重试。

## 简短串口控制

`?` 换行查看状态，`1` 至 `7` 换行切到对应页面，`<`／`>`／`h` 换行用于上一页／下一页／主页。JSON 页码为零起点，单字符页码为一开始，不要混淆。

## 本地 HTTP

ESP32 自己在端口 80 提供开发接口。地址来自设备 USB status 的 ip 或路由器 DHCP 列表；没有 mDNS 名称。

| 方法／路径 | 用途 |
|---|---|
| GET / | 本地按键控制页 |
| GET /status | 非敏感运行状态、页码、数据可用性 |
| GET /display | 帧数、全刷数、上次耗时和变化区域 |
| GET /frame.pbm | 800×480 原始 PBM，恰好 48011 bytes |
| GET /energy | UTC 小时、归一化价格、成本参数；没有 key/endpoint |
| GET /astronomy | 太阳与月亮计算结果 |
| POST /page | form-urlencoded，n=0..6 |
| POST /navigate | form-urlencoded，action=prev/next/home |

这不是开放互联网 API：当前没有认证、HTTPS 或访问控制。只用于可信局域网调试，不做端口映射。接口不提供设置和凭据读取，但任意能访问本地接口的客户端仍可以切页、查看显示内容。

PBM 是 1=黑，与内部 1=白相反，由导出接口转换。抓图读取的是 Panel 已提交的帧，不能用它推断实体屏幕的残影或对比度。

## 配对后的网络配置

0.4 新增 AES-256-GCM `/api/challenge` + `/api/command`。所有普通命令与 USB 共用处理器，remote_pair 仅允许 USB。格式、安全边界和 Wi-Fi 重连行为见 [网络协议](NETWORK.md)。

## Authenticated OTA (0.5)

`ota_info`, `ota_begin`, `ota_chunk`, `ota_finish`, `ota_abort`, `ota_restart` use the existing encrypted command envelope (or USB maintenance). Upload data is limited to 2560 bytes per chunk; commands remain within the 4095-byte plaintext limit. Full field semantics, memory map, health confirmation and recovery are in [OTA.md](OTA.md). `ota_tick` is internal control-loop maintenance and is not a client command.

0.6 的 `storage_info`（USB／已配对 IP）返回内部应用镜像实际大小、各分区容量与余量、外部 LittleFS 挂载状态及使用量、NVS 条目计数，不返回文件内容或凭据。USB 专用 `storage_check` 写入一个随机命名的探针文件、读回校验并删除；OTA 上传期间不执行此写测试。存储布局见 [STORAGE](STORAGE.md)。

### 0.7.1 encoder lock

`settings.encoder_lock_ms`: integer 0–10000 ms, default 2000. Omission preserves the current value; invalid type, fraction or out-of-range value rejects the command. Persisted separately as NVS `encoder_lock`, preserving `settings2` ABI. `status.encoder_locked` reports the current gate. USB and paired IP use the same setting.

The interval starts when a settled target passes the final revision check, immediately before the full refresh. It suppresses only physical A/B rotation, including capture and delivery races; turns are discarded, not queued. KEY3 Home, KEY1/2, USB/IP navigation still work. A 0 setting disables suppression. A full refresh already in progress cannot be interrupted; its physical duration can exceed the configured lock.

### 0.7.2 navigation timing and diagnostics

`page_switch_delay_ms` retains its range and stored value; its quiet interval now starts at the last accepted input and overlaps title refresh. The final title remains visible for at least 100 ms before full-page acceptance. `encoder_lock_ms` semantics are unchanged. `status` additionally returns `navigation_dispatch_us`, `navigation_title_us`, `navigation_accept_us` (last request to each phase) and `render_snapshots` (full-model copies made for actual page rendering). These are boot-local counters/measurements, not persistent settings. A queued input while the panel is physically busy includes that existing waveform in its latency.

### 0.7.3 title waveform trial

Firmware 0.7.3 trials a short differential waveform only for navigation title windows. Settings and diagnostics from 0.7.2 are unchanged. Serial `INKOS display` lines include `wave=nav300` for the guarded experiment and `wave=otp` for the original waveform. This does not change `page_switch_delay_ms` (selection quiet interval) or `encoder_lock_ms` (rotation suppression after acceptance). Studio 0.7.2 remains compatible.

0.7.4 extends the title-only drive to six-frame phases (~0.5-second target), with serial `wave=nav500`. Settings and host-app compatibility are unchanged.
