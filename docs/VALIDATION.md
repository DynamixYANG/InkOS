# 验证范围与证据

## 设备发布基线

2026-10-15，固件 0.3.1 已在目标 Seeed/XIAO/UC8179 套件烧录并通过 esptool 写后校验。启动自检 **61/61**，可用 PSRAM 为 8 MiB；应用约 1.78 MB，静态内部 RAM 48,284 bytes。

[公开摘要](validation/hardware-0.3.1.json) 只保存版本、固件 SHA、按 LF 归一化的源码 SHA、测试范围和限制。原始日志、私有 URL、芯片 MAC、IP、邮箱、NVS 与电脑路径保留在本地，不随仓库发布。

当前 Release 如使用该已部署二进制，应与摘要的 firmware_sha256 匹配。重新编译可能因时间／工具链／路径产生不同 SHA，不自动继承“已烧录并验证”的结论。构建记录与硬件记录是两种证据。

## 已检查的行为

| 范围 | 证据／结果 |
|---|---|
| 启动算法检查 | 时区／DST、节日、导航、播放与歌词、JPEG、ICS、太阳／月相、电价；61/61 |
| 设置保留 | 烧录前后读取比较，既有 Settings、feeds 和默认 Energy 参数保留 |
| 七个页面 | 设备实际 PBM 转 PNG，人工检查几何、文字和缺数据状态 |
| 当前电价计算 | 72 小时 net spot 与配置公式核对 |
| 备用价格来源 | 先前 0.3.0 对比 47 个重叠小时；0.3.1 计算和 adapter 未改 |
| 字体 | 原字模已获实物反馈：笔画均匀清晰，粗细合适 |
| 对比度策略 | 用户确认禁用快刷、全清屏后文字均匀、白底干净 |

Moon/Sun 为近似计算，不是精密历书；太阳升落曾与独立天气参考对比，月相日期使用更宽容的误差范围。这里只表明显示用途的检查，不声称导航／天文观测精度。

## 尚未完成的验证

- 用户自己的 Spotify 开发者应用和真实账号授权。
- 私人 Apple、Google、Outlook 日历；当前最终 Week 帧是未配置的空状态。
- 任意复杂 ICS、所有时区、任意第三方 7.5 英寸硬件。
- 新布局长期实物残影、不同温度和持续动画的光学指标。
- CJK 字库和中文歌词显示。

上位机曾完成五页 headless 布局与 USB 协议检查。最后扩展的隐藏 UI 启动在原工作环境被自动审批拒绝，未通过重试绕过；公开版本整理时执行构建，不把未执行的 GUI 检查记作成功。

## 当前仓库可复现检查

```powershell
python -m unittest discover -s tests -v
python tools/check_repo.py
python tools/build.py
dotnet restore desktop/InkOS.Studio/InkOS.Studio.csproj --locked-mode
dotnet build desktop/InkOS.Studio/InkOS.Studio.csproj -c Release --no-restore
```

工具测试验证配网页面 Host/CSRF/PKCE，以及烧录分区和镜像边界，均不需要 USB。CI 执行工具／文档检查和 Windows Studio 构建；这些不会替代真实设备测试。

硬件变化后的最小检查：启动无 timeout/panic、自检全通过、数据源正常、切页和按键正常、相关页面帧完整、静置后文字与背景稳定。涉及订阅或授权时使用自己的数据，但只记录去敏结果。

## 如何更新证据

新增测试报告应标明版本、构建 SHA、时间、测试类型、实际覆盖和未测场景。不要把历史测试记录改成当前 SHA 来制造一致性；只对明确未改的模块引用历史证据。

截图需标明是真实 framebuffer 还是示例。不要把用户房间照片、私有日程、专辑账号资料和原始 settings_get 当作公开 fixture。
