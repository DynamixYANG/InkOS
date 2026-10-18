# 第三方许可证与数据来源

原创代码由 DynamixYANG 以 [MIT](../LICENSE) 授权。以下独立组件保持原许可证；根目录 MIT 不覆盖这些组件，也不授权第三方平台内容。

| 项目 | 用途 | 版本／来源与许可证 |
|---|---|---|
| uICAL | RRULE 引擎 | commit 49262ecf6fed01b6d74f2d3af774a52714a2bc21；[MIT](../libraries/uICAL/LICENSE) |
| pugixml | iCloud CalDAV XML 解析 | 1.15；[MIT](../libraries/pugixml/LICENSE.md)、[固定来源](../libraries/pugixml/SOURCE.md)；禁用 XPath／异常，分配 XML 节点到 PSRAM |
| Lucide | 当前天气及温度／降水／风图标 | commit 1ef60af944de207e0d5176905a0759ba12e6ee8d；[官方网站](https://lucide.dev/)、[ISC 与继承 MIT 许可](../assets/icons/lucide/LICENSE)、[固定来源与处理](../assets/icons/lucide/SOURCE.md)；16/24/40/64 px 原生细线单色图 |
| Material Design Icons | 历史 0.7.0–0.7.4 天气图标；0.7.10 起首页 aurora（Kp）图标 | 7.4.47；[Pictogrammers](https://pictogrammers.com/library/mdi/)、[许可](../assets/icons/mdi/LICENSE)、[原始来源](../assets/icons/mdi/SOURCE.md) |
| SunCalc | 太阳／月亮近似公式改编 | 1.9.0；Vladimir Agafonkin；[BSD-2-Clause](../assets/SunCalc-LICENSE) |
| Terminus | 小号点阵字 | 4.49.1 normal；[OFL](../assets/fonts/terminus/OFL.TXT) |
| IBM Plex Mono | 大字与 Studio 字体 | Light / Regular；[OFL](../assets/fonts/license.txt) |
| Source Han Sans SC Light | 中文 fallback 单色字模 | 2.005；Adobe；[OFL](../assets/fonts/source-han-sans/OFL.txt)、[来源与校验和](../assets/fonts/source-han-sans/SOURCE.md) |
| OpenCC TSCharacters | 歌词匹配用简繁字形折叠 | [Apache-2.0](../assets/opencc/LICENSE)、[来源与校验和](../assets/opencc/SOURCE.md) |
| ShadUI | 上位机控件与主题 | 0.2.4；[MIT 原文](../desktop/licenses/ShadUI-LICENSE.txt) |
| Avalonia | 上位机框架 | 12.1.0；MIT；完整 notices 在 desktop/licenses |
| SkiaSharp / HarfBuzzSharp / .NET | 桌面绘制与 runtime | 各自 notices，见 [完整依赖表](../desktop/licenses/DEPENDENCIES.md) |
| ESP32 Arduino / ESP-IDF | 固件工具链与系统 | 通过 Espressif core 3.3.12 安装，ESP-IDF 5.5.5；LGPL-2.1+ / Apache-2.0 等，保留各组件许可证 |

ShadUI NuGet 元数据没有直接包含许可正文；仓库补入其固定源码 commit badba276bf3bde9f50299a1718dcb44efc9cd6e4 的 LICENSE。字体源与 uICAL 源码随仓库保留，不把第三方作者移除。

## 外部服务

- [Open-Meteo](https://open-meteo.com/)：天气预报 adapter，对应使用条款和归属见该服务。
- [MET Norway](https://api.met.no/)：Locationforecast，yr.no 背后的气象服务；遵守 User-Agent、缓存与数据使用要求。
- [Sähkötin](https://sahkotin.fi/api)：Finnish spot 数据接口，Pakastin Oy 提供、Nord Pool 数据；当前免费 API 非商业用途。
- [Porssisahko](https://www.porssisahko.net/api-v1)：备用 Finnish price adapter。
- [Spotify Web API](https://developer.spotify.com/documentation/web-api)：播放信息与封面；用户自己的开发者应用和授权。
- [LRCLIB](https://lrclib.net/docs)：精确查询、候选搜索与同步歌词来源。
- [网易云音乐](https://music.163.com/)：0.4.3 的补充歌词来源，直接使用其 HTTPS 搜索及逐行歌词接口；非官方稳定性承诺，仍受其数据与服务条款约束。

Weather 页面移除了来源文字以减小屏幕信息量；项目文档仍明确记录来源。示例图是该项目自己绘制的页面，布局借鉴 TRMNL 的信息层级和 Weathergraph 的时间线思路，未复制它们的模板或品牌图标。

根目录 MIT 是代码许可，不是电价数据、专辑封面、歌词、账号 API 或商标的转授权。发布衍生产品时分别核对相关来源条款。

固件工具链许可正文另存于 [LGPL-2.1](../assets/licenses/LGPL-2.1.txt)、[Apache-2.0](../assets/licenses/Apache-2.0.txt) 和 [libb64](../assets/licenses/libb64-LICENSE.txt)。完整核心源码可按 [BUILD](BUILD.md) 安装 Espressif core 3.3.12 获取，也可从 [固定版本源码](https://github.com/espressif/arduino-esp32/tree/3.3.12) 取得并重新构建／链接。
