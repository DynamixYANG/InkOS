# 芬兰电价模块

## 数据来源

默认地区为芬兰 FI bidding area，Oulu 使用这个区域价格，不另有 Oulu 城市独立 spot price。

- 默认来源：[Sähkötin API](https://sahkotin.fi/api)，Pakastin Oy 提供；其 API 当前面向非商业使用，spot 数据归 Nord Pool。不要因为本项目 MIT 就假设数据也可任意再分发。
- 备用来源：[Porssisahko API](https://www.porssisahko.net/api-v1)。
- Custom：返回兼容 JSON 的自有 HTTPS endpoint，可配置 Authorization 或 X-API-Key。

默认源不需要 API key。源码只做适量正常请求与缓存；服务规则、单位及可用性以来源文档为准。

## 单位与公式

模型存储未含税 spot，单位 ct/kWh。Sähkötin 请求不带 `vat` 或 `fix`，raw EUR/MWh 除以 10 归一化。Porssisahko adapter 去掉来源内含 VAT 后再进入同一模型，避免重复加税。

默认参数为用户选择的配置，不作为所有电力合同的通用收费定义：

| 参数 | 默认 |
|---|---:|
| VAT | 25.5% |
| margin | 0.45 ct/kWh |
| transmission | 6.00 ct/kWh |
| fees_include_vat | false |

两项费用都未含 VAT 时：

```text
totalCt = (spotNetCt + marginCt + transmissionCt) × (1 + vatPercent / 100)
```

勾选“费用已含 VAT”时，两项费用使用同一个含税选项：

```text
totalCt = spotNetCt × (1 + vatPercent / 100) + marginCt + transmissionCt
```

例如 net spot 10 ct/kWh，默认总价为 20.64475 ct/kWh = 0.2064475 €/kWh。主页四舍五入显示 €/kWh 三位小数，电价页展示 ct/kWh。没有配置的月租或额外税费不会被自动补入。

## 15 分钟数据与小时显示

市场数据可以是 15 分钟区间。只有 UTC 小时内四个完整、不重叠、对齐的区间齐全时才生成该小时平均值。缺少任一区间就留空，不能当零价。

显示的是时间平均，不是按实际用电量加权的账单。负 spot 和负总价保留符号。缓存最多 96 个 UTC 小时，来源返回的实际可用长度可能更短。

## 居中图表

当前小时 `H = floor(nowUTC / 3600) × 3600`：

```text
index 0        index 12         index 24
H - 12 hours   H / NOW          H + 12 hours
```

共 25 槽，当前小时固定在正中。前后都是 12 个真实经过的小时，不以本地午夜为起点。重复的芬兰秋季本地小时标记 a/b；春季跳时不会捏造消失的小时。窗口低点／高点只统计当前窗口的有效值。

未发布的未来小时显示缺口。不能因为需要视觉对称，就将过去价格复制到未来。

## 配置与凭据

Studio → Energy 可以修改来源、开关、三项成本参数、费用含税选项、自定义 URL 和可选凭据。

Key 的保存模式：0 保留，1 替换，2 移除。输入框留空默认保留已有 key；settings_get 只返回 has_key，不返回 key 本文。自定义来源不跟随重定向，避免把凭据送给其他主机；预设源可以跟随其正常 HTTPS 重定向。

配置使用独立的 NVS energy1 JSON，渲染快照没有 URL/key。LAN /energy 返回可公开的价格与成本参数，不返回订阅和凭据。不要把私有 endpoint 或 header 放进公开示例。

## 缓存、重试与模块边界

Energy.cpp 负责 source adapter、有限 JSON、区间归一化、成本模型和来源绑定的校验缓存。EnergyPage.cpp 只绘图，不联网。缓存通过原子方式替换；网络失败保留已知数据并标记缓存状态。

正常刷新间隔较长，并考虑芬兰下一天价格公布时段；失败时尊重 Retry-After 和退避。服务未发布明日数据时，屏幕的未来槽继续留空。来源变化会使旧源缓存失效，费用变化则重算已有 net spot。

## 自定义 endpoint 约定

```json
{
  "prices": [
    {
      "startDate": "2026-01-01T00:00:00Z",
      "endDate": "2026-01-01T01:00:00Z",
      "price": 10.0
    }
  ]
}
```

上例 price 单位必须是未含税 ct/kWh。时间应使用明确时区的 ISO 8601，区间要完整、非重叠、对齐；不要把 EUR/MWh 或已经含全部费用的总价混作 spot。

设备自检包含单位、完整／缺失区间、负价格、VAT 两种模式和时间边界。实际数据源对比记录见 [VALIDATION](VALIDATION.md)。
