# 设备配置与日历订阅

## 配置流程

初始 Wi-Fi → Studio 连接 USB → 读取当前设置 → 修改并保存 → 等待屏幕联网更新。Studio 不充当运行时服务器。

首版上位机没有单独的 Wi-Fi 设置页，使用仓库附带的临时 Python helper 配网。不要把 Wi-Fi 密码写进源码。

```powershell
$env:INKOS_PORT = 'COM9'
python tools/setup.py
```

浏览器打开 `http://127.0.0.1:8767/`，只在本机输入 2.4 GHz SSID 和密码，点击 Save Wi-Fi to device。完成后按 Ctrl+C 关闭 helper，释放串口，再启动 Studio。已有 Wi-Fi 可以直接保留，不必重复配置。

`127.0.0.1` 指向运行 helper 的电脑。要换电脑设置，需要把设备 USB 接到那台电脑，在那里运行 helper 或 Studio；无需把临时页面暴露到局域网。

## Studio 五个区域

| 区域 | 配置 |
|---|---|
| Calendars | Apple、Google、Outlook 三个订阅槽；名称、URL、启用开关 |
| Place & time | 地区标签、经纬度、本地时区、两个世界时钟、天气源、高频主页更新 |
| Spotify | Client ID、官方授权、授权状态、移除授权 |
| Energy | 开关、数据源、VAT、margin、transmission、费用含税选项、可选 API 凭据 |
| Device | 上一页、下一页、主页和重新读取设置 |

选择实际 USB 串口，点击 Connect / Read。应用先检查 InkOS 0.3 协议版本，然后读取设备配置，避免拿空白默认值覆盖已有设置。修改完成再 Save to screen，等状态显示已保存。

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

在 iCloud 日历的共享设置中取得“公共日历”提供的 webcal 链接，填入 Apple 槽。固件将 `webcal://` 规范化为 HTTPS。是否公开共享由账户主人自行决定；应用只读取已经提供的订阅地址，不操作账号共享权限。

本版没有私有 CalDAV、Apple 账号登录或应用专用密码接口。如果不愿启用共享链接，可先不接入该源，后续扩展 OAuth/CalDAV adapter。

## Outlook / Microsoft

使用 Outlook 网页版的日历发布设置取得 ICS 订阅 URL，填 Outlook 槽。不要填普通 HTML 分享页面。组织可能禁止发布；本版不通过 Microsoft Graph 登录读取受保护日历。

## 日程的显示与边界

设备约每 15 分钟拉取启用的源。主页显示当前／下一个事项，Week 显示本地时区周一到周日。每天最多两条事项；额外事项显示 `+N`。跨天事件按 `[start,end)` 与每天区间求交，全天事项保留全天语义。

支持常见 RRULE、EXDATE、RDATE 和单次实例覆盖；展开窗口 35 天，最多保留最近 96 条。某些复杂、未知时区或超限日历会报错并保留旧缓存，不能保证任意 ICS 文件都被完整解释。

删除或暂停源：在 Studio 取消启用后保存；要移除链接，再清空该 URL 保存。原始 URL 仅通过 USB 配置，不由 LAN 诊断接口返回。

## 地区、时间和天气

Oulu 默认坐标 `65.0121, 25.4651`，本地时区 `Europe/Helsinki`。北京 `Asia/Shanghai`，伦敦 `Europe/London`。时区下拉框是明确支持的 28 项，不是完整 IANA 数据库；不要仅替换成任意字符串。

天气源可选 Open-Meteo 或 MET Norway（yr.no 背后的气象服务）。MET 的 User-Agent/contact 可配置；不要填含换行的值。数据源切换后等待新数据，失败时保留旧结果并标记状态。

Frequent Home updates 控制图标动画和冒号闪烁，默认关闭。即使打开，也受工厂波形耗时限制，不能保证 0.5 秒刷新。
