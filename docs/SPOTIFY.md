# Spotify 接入与同步歌词

## 运行方式

音乐在手机、电脑或其他 Spotify 播放设备上播放。ESP32 不播放音频，它直接查询官方 Web API 的当前播放信息，下载封面，并按歌曲、歌手和时长匹配 LRCLIB 同步歌词。

Studio 只完成一次性的 PKCE 浏览器授权和 USB 传输。之后 refresh token 存在设备 NVS，ESP32 直接续期，不需要常驻电脑服务器。

## 创建个人开发者应用

1. 用准备使用的 Spotify 账号进入 [Developer Dashboard](https://developer.spotify.com/dashboard)。
2. 创建使用 Web API 的应用，填写自己的应用名和描述。
3. 在 Redirect URIs 中加入下列完全相同的地址并保存：

```text
http://127.0.0.1:8768/callback
```

4. 复制 **Client ID**。不要复制 Client Secret，本项目不使用它。
5. 使用其他测试账号时，在应用的 Users Management 中加入允许访问的账号。

截至 2026-10-15，Spotify 官方 [quota modes](https://developer.spotify.com/documentation/web-api/concepts/quota-modes) 说明开发模式应用所有者需 Premium，开发模式最多 5 个已授权用户，测试用户还需加入 allowlist。规则和配额可能变化，授权失败时核对官方文档。

## 在 Studio 授权

1. 把设备 USB 连接到运行 Studio 的电脑，选择串口并 Connect / Read。
2. 打开 Spotify 区域，粘贴 32 字符 Client ID。
3. 点击 Authorize Spotify，浏览器进入官方账号页面；核对账号后同意权限。
4. 浏览器回到本机 `127.0.0.1:8768/callback`，Studio 接收授权码、交换 token，并通过 USB 保存到屏幕。
5. 应用显示授权已保存后，在手机或音箱上开始播放，切换屏幕到 Music 页。

浏览器和 Studio 的 callback 应运行在同一台电脑上。若换另一台电脑完成设置，需要把设备接过去并在那台电脑运行 Studio。只把 localhost 地址发到另一台电脑不能完成当前实例的回调。

Studio 回调端口是 **8768**；旧 Python helper 的 Spotify 回调是 **8767**。两种流程选择其一，并在 Dashboard 注册对应 URI，不要混用。

## 访问权限与刷新

请求 scopes 为 `user-read-currently-playing` 和 `user-read-playback-state`，只读取播放状态。应用不会控制播放、改歌单或读取邮件。PKCE verifier/state 只在这次授权流程使用。

Music 页约每 5 秒查询播放状态，其他页约每 30 秒。两次响应之间按设备单调时钟估算播放进度，再用新样本校正暂停、跳转或换曲。数据陈旧时显示状态，不假装有新的实时信息。

歌词更新按时间戳选择当前行，但墨水屏刷新、网络采样和歌词来源都有延迟；不是音频采样级同步。当前只对匹配到的 timed lyrics 做行同步，没有完整同步歌词时会显示对应提示。

## 检查与排错

| 现象 | 检查 |
|---|---|
| Redirect URI 错误 | scheme、127.0.0.1、8768、/callback、尾斜线是否逐字匹配 |
| 浏览器能登录但 API 403 | 应用模式、Premium、测试账号 allowlist、账号是否一致 |
| NO ACTIVE PLAYBACK | 账号当前是否真的有活动播放；等待一个采样周期 |
| 有歌名但无歌词 | LRCLIB 是否有相同版本、歌手和时长；并非所有曲目都有同步歌词 |
| 中文变成问号 | 当前字库没有 CJK；后续需要字体扩展 |
| 授权过期／撤销 | 在 Studio 重新授权，不向仓库提交 token |

Disconnect 仅移除设备上的相关授权数据，不删除 Spotify 账号。真实个人账号授权仍需用户完成；仓库中的合成自检和静态页面不代表账号已连通。
