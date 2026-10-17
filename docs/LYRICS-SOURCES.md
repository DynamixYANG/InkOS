# 歌词来源（固件 0.4.3）

设备直接查询 LRCLIB 与网易云 HTTPS，不依赖常驻电脑、代理服务器或新的平台账号登录。Spotify 授权继续只负责当前播放信息；歌词获取与字体绘制独立。

## 查询流程

1. LRCLIB `/api/get`：携带歌名、主歌手、专辑与四舍五入的时长。
2. 未取得可用时间轴时调用 `/api/search`：先原始歌名／歌手，再尝试标题主体、简繁折叠和常见歌手别名；结果仍需本地验证。
3. 网易云 `/api/search/get?type=1`：用规范化的主歌手与歌名搜索最多 10 个候选。
4. 从候选中选最多两首匹配的曲目，通过 `/api/song/lyric` 获取逐行 LRC。

匹配要求歌名或标题主体一致、主歌手匹配、时长差不超过 2 秒。Live、现场、Remix、不插电、伴奏、翻唱、语言和变速标签分别校验；不能借助候选别名绕过版本约束。同等候选优先专辑一致、时长更近的记录。所取时间轴剔除超过曲目结束 5 秒的条目，只有至少一条有效非空歌词才标为 Timed。

`LyricMatching.cpp` 包含 12 个常见英文／中文歌手别名，可独立扩展。OpenCC 字符表只用于搜索比较，采用字符级简化，不是完整词语转换器；不会改变屏幕显示的歌曲名称或歌词原文。拼音、其他艺名、任意错别字和未标注的录音版本不能保证匹配。

## 调度与退避

Spotify 在查歌词之前先发布新的播放快照；歌词到达后再发布结果。请求串行执行，后续请求之间至少停留 300 ms；LRCLIB 限流响应同时支持数字和 HTTP 日期的 Retry-After，并保留 provider 退避状态。失败源不妨碍另一个来源尝试。

每次连接／读取超时目标为 5 秒；resolver 有 25 秒的发起请求预算，单次慢响应可能超过该预算。没有歌词时保持 Missing／Untimed／Instrumental／Unavailable 语义，并沿用约 5 分钟的重试间隔。没有新增持久歌词库，当前曲目的歌词仍保留在 RAM 中。

网易云接入是非官方接口，接口、可达性和曲库可能变化；服务错误或格式变化会回退到不可用状态，不假造歌词或时间轴。只有无时间轴文字时，不将其伪装成同步歌词。

## 模块边界

| 模块 | 职责 |
|---|---|
| LyricMatching / LyricHan | 纯文本、歌手别名、简繁折叠、版本及候选评分 |
| LyricsResolver | 来源顺序、候选选择、状态和退避；依赖可替换 transport |
| LyricsService | ESP32 HTTPS 与串行测试队列；联网不持有显示 model 锁 |
| Lyrics | LRC 转换到现有逐行时间轴 |
| Spotify | 官方播放数据和封面；触发歌词查询、发布播放快照 |
| Canvas / Fonts | 中文与英文 1-bit 绘制，不参与联网 |

## 可复现验证

启动检查使用原创合成 LRC 与可替换的假 HTTP transport，覆盖简繁／别名、版本误匹配、时长差、候选排序、精确／搜索／网易云回退、坏 JSON、超出曲长的时间轴、纯音乐、无时间轴及 Retry-After 退避。

实际设备联网检查：

```powershell
python tools/probe_lyrics.py --port COM9
```

0 是 LRCLIB 搜索，1 是网易云（繁体歌名＋英文歌手），2 是正常自动流程。它们只查询固定测试曲目，不修改用户的播放、日历、配置或实体屏幕。命令只导出来源、状态、时间轴行数和 HTTP 诊断，不导出歌词正文或账号资料。结果写入 Git 忽略的 `out/lyrics-probes.json`。

## 后续扩展

本地 LRC 导入、持久缓存、上位机来源开关／优先级、候选手选及时间偏移仍未实现。QQ／酷狗与 Musixmatch 是后续可选来源；它们分别涉及平台接口、格式／解密或 API 权限。

## 一手资料与来源

- [LRCLIB API](https://lrclib.net/docs)
- [网易云音乐](https://music.163.com/)：实际读取搜索与逐行歌词响应，客户端协议适配在本项目中独立实现。
- [OpenCC](https://github.com/BYVoid/OpenCC)：[固定字典与许可](../assets/opencc/SOURCE.md)。
- [LDDC](https://github.com/chenmozhijin/LDDC)：此前方案评估参考，网易云 provider 未复制其 GPL 代码，也不需要其运行环境。
- [Spotify 当前播放接口](https://developer.spotify.com/documentation/web-api/reference/get-the-users-currently-playing-track)
