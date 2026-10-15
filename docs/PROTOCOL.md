# USB 与设备本地调试协议

## USB framing

115200 baud、UTF-8、每条命令一个 JSON 对象加 LF。Studio 设置 DTR=true、RTS=false。启动日志和协议响应可能混排；客户端只解析 JSON 对象并匹配 inkos=true 与预期响应字段，不假设每行都是 JSON，也不把数字行当对象。

单条输入上限 4095 bytes，溢出会拒绝该命令。写操作一般返回 `{"inkos":true,"saved":true}`，失败时 saved=false 并可能有 message。不要记录原始包含设置的响应。

| cmd | 关键字段 | 结果 |
|---|---|---|
| status | 无 | version、page、wifi、spotify、time、heap 等 |
| settings_get | 无 | settings、feeds、energy；feeds 含私人 URL |
| settings_set | settings 对象 | 更新地区、天气与两个世界时钟 |
| feed_set | index=0..2、name、provider、url、enabled | 保存一个只读日历源 |
| energy_set | energy 对象 | 更新成本参数与来源，凭据按 key_mode 处理 |
| spotify | client_id、refresh_token | 存入设备；不是 Client Secret |
| spotify_disconnect | 无 | 移除设备授权和相关播放内容 |
| wifi | ssid、password | 保存 Wi-Fi，网络任务使用新值 |
| page | page=0..6 | 绝对切页 |
| navigate | action=prev/next/home | 相对切页 |
| icon_test | code | 临时图标检查，约 15 秒恢复 |

## 普通设置示例

```json
{"cmd":"settings_set","settings":{"location":"Oulu","zone":"Europe/Helsinki","latitude":65.0121,"longitude":25.4651,"weather_source":0,"animations":false,"met_contact":"","worlds":[{"label":"Beijing","zone":"Asia/Shanghai"},{"label":"London","zone":"Europe/London"}]}}
```

weather_source：0 Open-Meteo，1 MET Norway。worlds 必须是两个有效时区与标签。时区只接受 Zones.cpp 的目录；坐标和来源有界。设置变化递增 revision 并触发全刷。

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
