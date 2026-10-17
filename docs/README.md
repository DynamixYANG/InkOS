# InkOS 文档导航

文档对应固件 **0.7.0**、Studio **0.7.0**。功能状态以当前源码和 [验证记录](VALIDATION.md) 为准；规划不代表已经实现。

| 文档 | 解决的问题 |
|---|---|
| [HARDWARE](HARDWARE.md) | 已验证硬件、GPIO、排线、供电、进入下载模式、分区 |
| [BUILD](BUILD.md) | 新电脑准备环境，编译固件／上位机，备份和烧录，构建产物 |
| [CONFIGURATION](CONFIGURATION.md) | 初始 Wi-Fi、地区与世界时钟、Google/Apple/Outlook 日历 |
| [SPOTIFY](SPOTIFY.md) | 创建个人开发者应用、Redirect URI、PKCE、实际播放检查 |
| [ENERGY](ENERGY.md) | 数据源、单位、VAT 公式、15 分钟归一化、缓存、居中时间窗口 |
| [ARCHITECTURE](ARCHITECTURE.md) | FreeRTOS 任务、状态归属、模块边界、网络与绘图解耦 |
| [PROTOCOL](PROTOCOL.md) | USB NDJSON 命令、字段与设备本地 HTTP 调试接口 |
| [DISPLAY](DISPLAY.md) | 清晰度策略、全刷／局刷、1-bit 字体、布局规则 |
| [DEVELOPMENT](DEVELOPMENT.md) | 加页面、加数据源、改字体和改存储时的具体步骤 |
| [VALIDATION](VALIDATION.md) | 当前与历史验证、尚未覆盖的场景、可复现检查 |
| [TROUBLESHOOTING](TROUBLESHOOTING.md) | 烧录、屏幕、联网、日历、Spotify 和电价问题 |
| [THIRD_PARTY](THIRD_PARTY.md) | 许可证、字体、服务归属与数据来源 |
| [ROADMAP](ROADMAP.md) | 已实现边界与后续工作 |

从零使用建议顺序：**HARDWARE → BUILD → CONFIGURATION → SPOTIFY**。修改代码建议先读 **ARCHITECTURE → DEVELOPMENT → DISPLAY → VALIDATION**。

`images/screen/` 是设备提供的原始 800×480 单色画面转换得到的 PNG。`images/studio/` 是未登录、未配置私人数据的应用示例；不是用户账户截图。`validation/` 只保存可公开的摘要，不包含原始 NVS、串口设置响应或用户订阅。

- [Wi-Fi 与加密 IP 配置](NETWORK.md)：首次配网、USB 配对、另一台电脑设置、协议和重连。

- [月历与周时间网格](CALENDAR.md)：ISO 周序号、事件块、全天横条、当前时间线和预览。

- [OTA、A/B 启动、外部字库和 USB 迁移](OTA.md)

- [U5 外部 Flash 手册核对、实板引脚与速率测试](EXTERNAL-FLASH.md)

- [Flash 分区饼图、实际占用与容量余量](STORAGE.md)

- [iCloud 私有日历选择与设备直连](ICLOUD.md)
