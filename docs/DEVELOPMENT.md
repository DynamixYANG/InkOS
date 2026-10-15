# 开发与扩展

## 原则

页面只接收 typed snapshot；provider 只产出领域模型；Panel 只接收像素帧。UI 控件不直接发底层串口字节，统一通过 DeviceClient。这样改页面、换服务和换显示驱动能够分别验证。

固件源码原有行为保持为 0.3.1。整理仓库主要修改工具可移植性、项目构建配置、文档与发布结构，不扩展网络功能，也不在页面代码中放设置密码。

## 修改一个页面

1. 定位对应 renderer：Pages.cpp、WeekPage.cpp、AstronomyPages.cpp 或 EnergyPage.cpp。
2. 复用 Layout.h 的公共边界，按原生字模尺寸安排文字和留白。
3. 使用 fit 限制长文本，评估负数、四位温度、未知值、长地点和两条事项。
4. 编译后在设备切页，再用 capture_pages.py 获取实际帧。
5. 检查标题、底栏、分隔线、最后一行和缺数据状态；涉及波形时另做实物观察。

只改坐标时不必重测全部外部账号或写回设置，但必须完成构建和相关页面检查。

## 添加页面

需要同时维护 Domain.h 的 Page/Count、pageAfter 导航、自检、Runtime 的 renderer 分派、Chrome 的名称和页码、本地 web 按钮，以及 Studio 的导航说明。不可只把页面加到枚举却遗漏循环边界。

新页面明确选择全刷与内容局刷策略。不能在 renderer 发 HTTP、等待 SPI 或写 NVS；网络数据应该预先准备在 Model 中。

## 添加数据来源

1. 定义或复用领域模型的单位与缺值规则。
2. 在独立 adapter 中验证 HTTPS 响应、大小与字段，保持处理有界。
3. 用固定 fixture 验证单位、空值、失败和时间戳，再做适量真实查询。
4. 只有有效结果才覆盖已有模型；增加 revision 并通知重绘。
5. 配置变更期间核对 source/config revision，不能发布过期来源的结果。
6. 同步维护 C# DTO、表单校验、USB 命令和文档。

Weather 的 MET 与 Open-Meteo 共用同一页面；新增源应沿用这个模式。Energy 的 net spot 单位必须保持 ct/kWh，不能让 renderer 猜测来源单位。

## 修改配置或存储

settings2 保存原始 Settings 字节，改变字段顺序、类型或 sizeof 都可能使旧值不可读。先定义迁移，再修改结构。优先为可独立版本化的新功能使用单独 JSON/key，像 energy1 一样。

不得在 schema 迁移中静默清除 Wi-Fi、Spotify 和日历。开发前先私下备份 NVS；公开测试夹具只放虚构 URL 和 token。原始 settings_get 响应含日历链接，不能上传。

## 修改字体或图标

先修改字体生成器或 Canvas 图形实现，保持 1-bit、整数坐标。运行生成器后审查生成资产的变化，并保留源字体许可证。图标动画应使用局部区域；不要为追求 FPS 重新启用未经验证的短波形。

ArtworkFixture.h 是独立合成灰度样本；真实封面受各自权利和服务条款约束，不用用户封面充当公开单元测试夹具。

## 桌面应用

五个 View 独立，ViewModel 的 Commands 负责 busy/cancel/validation，串口请求必须经同一个 gate 串行执行。新增设置同时验证输入合法性、USB DTO、固件边界和保存语义。

密码型文本要双向绑定，但不能为调试打印敏感值。Energy 的空 key 表示保留，而不是用空字符串抹掉；修改此行为要连同 UI 提示和固件一并设计。

NuGet 已锁定，项目声明 win-x64 runtime，避免只用本机历史 restore 状态才能编译。修改依赖时有意更新 lock file，并记录来源与许可证。

## 检查、提交与发布

```powershell
python -m unittest discover -s tests -v
python tools/check_repo.py
python tools/build.py
dotnet restore desktop/InkOS.Studio/InkOS.Studio.csproj --locked-mode
dotnet build desktop/InkOS.Studio/InkOS.Studio.csproj -c Release --no-restore
```

编译通过不代表真实账号、实体屏幕对比度或全部日历规则已验证。验证报告明确区分离线、构建、硬件和人工观察。

构建产物在 out，私有文件在 .local 或仓库外。发布前用允许清单打包，检查 Git 暂存列表与 ZIP 内容。Release 只附三件套固件、完整 Studio 目录 ZIP 与 SHA256SUMS，不附原始 NVS 或串口配置响应。
