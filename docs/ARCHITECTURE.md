# 系统架构

## 设计目标与边界

InkOS 是 FreeRTOS 上的 ESP32 应用，不是 Linux。主控承担时间换算、日历展开、太阳与月亮计算、天气／电价／Spotify 拉取，以及完整 1-bit 渲染。外部服务提供原始数据，不提供页面截图。Studio 负责配置与一次性账号授权。

显示驱动、业务数据、页面布局和桌面设置各自独立；新增页面不应侵入网络协议，修改天气源不应要求修改绘图代码。

```mermaid
flowchart LR
  P[Weather / Spotify / LRCLIB / ICS / FI prices] --> N[Network task · core 0]
  T[SNTP + local astronomy] --> M[Typed Model / PSRAM]
  N --> M
  U[Buttons / USB configuration / local HTTP] --> M
  M --> Q[Coalescing redraw queue]
  Q --> D[Display task · core 1]
  D --> R[Pure page renderers]
  R --> C[Canvas / 1-bit framebuffer]
  C --> E[Panel / UC8179 SPI]
  S[Studio · C# / ShadUI] -->|USB NDJSON| U
```

## 任务、内存和所有权

| 任务 | 核心 / 优先级 / 栈 | 责任 |
|---|---|---|
| display | core 1 / 2 / 12288 bytes | 获取快照，绘制页面，唯一调用 Panel 的任务 |
| network | core 0 / 1 / 16384 bytes | 串行执行外部 HTTP、同步和重试，发布数据 |
| control | core 1 / 1 / 12288 bytes | 物理按键、USB 命令和设备本地 HTTP |

Model、绘图快照和大缓冲区位于 PSRAM；内部 RAM 仍承担任务栈和部分 TLS 开销。大型 Model/Music 不放在任务栈上。现有 Model 约 99.4 KB，单张 framebuffer 为 48,000 bytes。

`modelMutex` 保护短时快照与发布；网络请求不能在持有它时进行。`frameMutex` 保护屏幕帧和调试抓取。Credentials 有单独的配置锁，不把密码、token 或 API key 拷贝进渲染 Model。

重绘队列容量为 1：快速到来的更新合并为“按最新状态重绘”，不会积累过时帧。Panel 仍有实际忙状态；动画目标频率不等于可保证的物理刷新频率。

## 模块地图

| 模块 | 责任 |
|---|---|
| Domain | 页面枚举、Settings、Weather、Agenda、Music、Energy 与基础时间／节假日模型 |
| Configuration / Zones | USB 配置验证、NVS、支持的时区规则、世界时钟 |
| Http / Certificates | 验证 HTTPS、响应限额、状态码、Retry-After、有限重定向与解压 |
| Weather | Open-Meteo、MET Norway 转换成统一天气模型 |
| ICalendar / Calendars | ICS 字段解析、uICAL 重复展开、例外与覆盖、多源合并、缓存 |
| Spotify / Lyrics / Artwork | token 刷新、播放状态、LRC 时间戳、JPEG 到黑白封面 |
| Astronomy | 无网络的太阳／月亮近似计算和相邻事件查找 |
| Energy | 来源 adapter、单位和 15 分钟区间归一化、成本模型、价格缓存与重试 |
| Canvas / Fonts | 像素、线、图形和原生字模；没有设备联网操作 |
| Pages / WeekPage / AstronomyPages / EnergyPage | 用快照绘制页面，不写设置、不取网络数据 |
| Layout | 共用页边距、标题线、底栏、主页栏目边界 |
| Panel | 独占 SPI，工厂波形，全刷／局刷，old/new frame 和清理预算 |
| Runtime | 任务装配、调度、按键、USB 和本地调试端点 |
| desktop/InkOS.Studio | 独立 View、ViewModel、DTO、USB client、临时 Spotify PKCE callback |

## 一次数据更新如何到达屏幕

1. Network 读取当前配置副本，按到期时间发起 HTTPS。
2. Adapter 做单位与字段转换，缺失值保持 NAN／unknown，不能补造数据。
3. 短时间持锁发布 typed model，增加对应 revision，发送合并重绘通知。
4. Display 获取 Model 快照，并在锁外完成绘图。
5. Panel 比较 old/new 单色位图，仅传输变化的字节对齐区域；页面／设置／封面变化可强制全刷。
6. 每次完成后断开面板驱动电源，保留画面及所需 RAM；达到预算时进行维护全刷。

## 数据调度与失效

天气和日历通常每 15 分钟更新。天气失败会较快重试，并保留上次有效数据。Spotify 在 Music 页约 5 秒，其余页约 30 秒；单调播放时间线在采样间插值。电价有单独的公布时段、长间隔和失败退避，不每帧拉取。

用户切换来源时更新配置 revision。进行中的旧源请求完成后必须核对配置版本，不能把旧结果重新覆盖新设置。缓存也绑定来源 key。

时区显示不改变进程全局 TZ；系统保持 UTC，按显式 ZoneRule 做换算。电价以 UTC 小时为索引；日历以本地日界转换区间。这样跨午夜、夏令时跳时和重复小时的语义分开处理。

## 持久化合同

| 内容 | 位置／键 |
|---|---|
| Wi-Fi | NVS namespace wifi，ssid / pass |
| 常规设置 | inkos/settings2，原始 Settings 结构字节 |
| 日历配置 | inkos/feed0..feed2 |
| 电价配置 | inkos/energy1，独立 JSON |
| Spotify | inkos 的 client_id / refresh |
| 日历与电价缓存 | storage 分区上的 LittleFS |

Settings 原始字节保存方式对结构体布局敏感。增加字段前必须设计 schema/version 与迁移，不能直接改变 sizeof(Settings) 后假设旧配置可读。电价配置独立存放，正是为了避免破坏既有 settings2。

## 桌面程序边界

MainWindow 只承载侧栏与页面选择；Views 是五个独立设置区。MainViewModel 持有编辑模型、校验和命令状态；DeviceClient 串行发送请求并匹配响应。SpotifyAuthorization 独占临时浏览器授权状态。

Studio 不代设备抓天气、渲染屏幕或定时抓日历。关闭上位机不影响已配置设备的数据更新。对外 HTTP 是 ESP32 自己托管的调试服务，不是上位机 server。
