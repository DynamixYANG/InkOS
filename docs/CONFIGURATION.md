# 设备配置与日历订阅

## 配置流程

初始 Wi-Fi → Studio 连接 USB → 读取当前设置 → 修改并保存 → 等待屏幕联网更新。Studio 不充当运行时服务器。

Studio 0.4 的 Network 页面直接配置 Wi-Fi：USB Connect / Read → Network → 网络名称和密码动作 → Save Wi-Fi。首次 USB 配对后还可通过 IP 使用所有配置页；详见 [Wi-Fi 与 IP 配置](NETWORK.md)。原临时 Python helper 仍用于兼容旧流程，但正常配网不再需要它。

`127.0.0.1` 指向运行 helper 的电脑。旧 helper 换电脑时需要把 USB 接过去；新版 Studio 可在 USB 配对后直接通过 IP 设置，无需把临时页面暴露到局域网。

## Studio 六个区域

| 区域 | 配置 |
|---|---|
| Calendars | iCloud 私有日历列表与单个选择；Apple／Google／Outlook 公共或私密 ICS 订阅槽 |
| Place & time | 地区标签、经纬度、本地时区、两个世界时钟、天气源、高频主页更新 |
| Spotify | Client ID、官方授权、授权状态、移除授权 |
| Energy | 开关、数据源、VAT、margin、transmission、费用含税选项、可选 API 凭据 |
| Device | 上一页、下一页、主页、切页停按等待时间（默认 0.5 秒）和重新读取设置 |
| Network | Wi-Fi、设备 IP、USB 配对密钥、启用／撤销 IP 配置 |

选择实际 USB 串口，点击 Connect / Read。IP 模式选择 IP / Network，输入设备 IP 和配对密钥。应用先检查 InkOS 0.3–0.7 协议版本，然后读取设备配置，避免拿空白默认值覆盖已有设置。修改完成再 Save to screen，等状态显示已保存。

固件 0.6.1 起，Device → Page selection delay / seconds 设置切页等待时间，范围 0.1–5 秒，默认 0.5。按键先局刷左上角页名，停止选择并经过等待后再全刷；等待时间从标题显示完成开始计算。详见 [刷新策略](DISPLAY.md)。

## Google Calendar：只订阅日历，不读取邮件

使用你希望显示日程的 Google 账号进入 [Google Calendar 设置](https://calendar.google.com/calendar/u/0/r/settings)。若有多个账号，先核对网页右上角头像和账号，确保选对。

1. 在左侧“我的日历设置”中选择要显示的具体日历。
2. 打开“整合日历 / Integrate calendar”。
3. 找到“iCal 格式的私密网址 / Secret address in iCal format”，复制完整链接。
4. Studio → Calendars → Google Calendar，填名称和链接，开启该订阅。
5. Save to screen，等待设备获取。主页 Up Next 和 Week 共用这一份日程。

不需要将整个日历改为公开，也不需要提供 Gmail 密码。私密链接本身可以读取日历，应当像密码一样保存；不要发到聊天、Issue、截图或公开仓库。某些受管理账号没有私密地址，需按组织策略处理。步骤与权限说明见 [Google 官方帮助](https://support.google.com/calendar/answer/37648?hl=en)。

支持订阅多个不同账号的日历，但当前只有三个源槽，UI 按 Apple／Google／Outlook 分组。当前不是 Google OAuth 登录流程，也不会替你合并或修改线上日历。

## Apple / iCloud

固件／Studio 0.7 起使用 [iCloud 私有日历设置](ICLOUD.md)：在 App 输入 Apple Account 和专用密码，Load calendars 后选择一个日历并 Save iCloud to screen。无需公开日历，也无需常驻电脑；设备直接读取选中的 CalDAV 集合。

原有公共 webcal/ICS 链接仍作为可选订阅方式保留。当前私有 iCloud 选择与另外三个订阅槽独立；启用多个来源会合并显示。该配置不读取邮件，也不修改线上事件。

## Outlook / Microsoft

使用 Outlook 网页版的日历发布设置取得 ICS 订阅 URL，填 Outlook 槽。不要填普通 HTML 分享页面。组织可能禁止发布；本版不通过 Microsoft Graph 登录读取受保护日历。

## 日程的显示与边界

设备约每 15 分钟拉取启用的源。主页显示当前／下一个事项，Agenda 显示本地时区今天起连续七天的时间网格：日期在上、事件在下，全天横条最多两行，重叠的定时事件最多并排两列；额外条块在日期表头显示 `+N`。详见 [周网格布局](CALENDAR.md)。跨天事件按 `[start,end)` 与每天区间求交，全天事项保留全天语义。

支持常见 RRULE、EXDATE、RDATE 和单次实例覆盖；展开窗口 35 天，最多保留最近 96 条。某些复杂、未知时区或超限日历会报错并保留旧缓存，不能保证任意 ICS 文件都被完整解释。

删除或暂停源：在 Studio 取消启用后保存；要移除链接，再清空该 URL 保存。原始 URL 仅通过 USB 配置，不由 LAN 诊断接口返回。

## 地区、时间和天气

Oulu 默认坐标 `65.0121, 25.4651`，本地时区 `Europe/Helsinki`。北京 `Asia/Shanghai`，伦敦 `Europe/London`。时区下拉框是明确支持的 28 项，不是完整 IANA 数据库；不要仅替换成任意字符串。

天气源可选 Open-Meteo 或 MET Norway（yr.no 背后的气象服务）。MET 的 User-Agent/contact 可配置；不要填含换行的值。数据源切换后等待新数据，失败时保留旧结果并标记状态。

Frequent Home updates 控制图标动画和冒号闪烁，默认关闭。即使打开，也受工厂波形耗时限制，不能保证 0.5 秒刷新。

### Encoder lock (Studio 0.7.1 / firmware 0.7.1+)

In **Device → ENCODER LOCK / SECONDS**, set 0–10 seconds (default 2); 0 disables. Click **Save to screen** over USB or paired IP. This is separate from **PAGE SELECTION DELAY** (default 0.5 seconds after the preview becomes visible). When selection settles, the lock begins and physical rotation during the interval is discarded. Home remains available. An ongoing full refresh must finish even if the lock expires first.

### Page quiet interval in 0.7.2

The selection delay is measured from the last turn/press, overlapping the title refresh. After that delay has elapsed and the final title has been visible for at least 0.1 seconds, full refresh begins. Existing stored values are retained (default 0.5 seconds). The separately configured encoder lock still starts when the full refresh is accepted.
