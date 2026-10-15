# Contributing

先阅读 [架构](docs/ARCHITECTURE.md)、[开发指南](docs/DEVELOPMENT.md) 和 [显示合同](docs/DISPLAY.md)。Issue 与 PR 应描述具体问题、设备型号、固件／Studio 版本、预期与实际行为，以及与改动相关的验证。

不要附 Wi-Fi 密码、Spotify token、API key、私人 ICS URL、NVS dump 或原始 settings_get。屏幕照片可能含私人日程，上传前自行检查。

提交前运行 Python 工具测试和仓库检查，修改固件时完成编译，修改 C# 时使用 locked restore 并构建。只改文档或低影响布局不要求反复授权外部账号。

PR 说明应区分构建通过、离线测试、设备测试和人工屏幕观察。新增第三方代码、字体或依赖时保留原始许可证和版本来源。项目原创贡献采用根目录 MIT 许可证。
