# iCloud 私有日历

固件／Studio 0.7 起支持账号方式读取一个 iCloud 日历。无需公开共享日历，无需常驻服务器。Google、Outlook 和原有 Apple 公共 ICS 订阅仍独立存在；启用的来源会合并到主页下一事项与从今天起七天的日程页。

## 一次配置

1. 在 [Apple Account](https://account.apple.com/) 登录，开启账号双重认证。
2. 登录与安全 → App 专用密码，创建名为 InkOS 的密码。主账号密码／验证码只填官方页面；专用密码直接填本地 App，不要写到源码、日志或聊天。见 [Apple 官方说明](https://support.apple.com/en-us/102654)。
3. Studio 连接屏幕：USB 选择实际串口，或使用已配对 IP，点击 Connect / Read。
4. Calendars → iCloud / Private calendar，输入 Apple Account 和 App-specific password，点击 Load calendars。
5. 在 Calendar to display 中选择一个日历，勾选 Show selected iCloud calendar，点击 Save iCloud to screen。
6. 等待设备联网同步；主页 Up Next 与日程页会使用新的事项。关闭 App 不影响后续拉取。

Load calendars 只在这台电脑的内存中使用密码，执行 PROPFIND 发现 principal、calendar-home-set 与可读取的 VEVENT 日历。列表不会自动选择第一个日历；由用户明确选择。保存时将账户、专用密码与所选集合地址通过 USB 或配对 AES-GCM IP 协议存入设备。正常读取设置不会返回密码。

当前 App 关闭后不保存密码到电脑，也不保存发现列表到磁盘；重新打开后保留设备中的当前选择。要重读全部日历列表，需要再次填专用密码。一次会话中可在已读取的列表中切换选择而保留设备密码。

## 权限与退出

InkOS 仅实现读取日历列表与事件，没有创建、修改、删除日历或事件的接口。**Apple 发放的专用密码本身并非单日历、只读授权**；“选择一个日历”是本应用的读取范围，不会缩减凭据在 Apple 端的能力。可为 InkOS 单独生成密码，便于单独撤销。

Disconnect 仅移除设备上的 iCloud 凭据，并停止该来源。若需要撤销 Apple 端密码，去 Apple Account 操作；修改主账号密码也可能使专用密码失效。取消 Show selected iCloud calendar 并保存可暂停，不必删除凭据。

## 设备直连与缓存

- HTTPS 验证服务器证书，仅允许 `caldav.icloud.com` 与 `p数字-caldav.icloud.com` 的 443 端口；每次重定向重新验证，拒绝将密码转发到其他主机或明文 HTTP。
- 按 [RFC 4791](https://www.rfc-editor.org/rfc/rfc4791) 使用只读 calendar-query REPORT 和 VEVENT 时间过滤，查询今天起 35 天；复用既有 ICS 重复、例外、跨日、时区解析器。
- XML 响应上限 512 KiB、最多 256 个资源；命名空间与每个 calendar-data 的 200 状态均需正确。禁止 DTD／外部实体；错误响应不覆盖已发布数据。
- 合并后的日程缓存位于 U5 外部 LittleFS。缓存带来源指纹，更换所选日历时先移除旧显示，启动时拒绝把旧来源缓存当作新日历。旧 0.6 日程缓存会在首次同步时重建，订阅和账户设置不清除。
- 同步约每 15 分钟。错误或凭据失效会保留同来源的旧缓存并标记 stale，不会替用户更改账号或日历共享状态。
