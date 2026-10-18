# 构建、烧录与发布产物

## 工具版本

| 工具 | 版本／用途 |
|---|---|
| Python | 3.11+；工具、配置和截图 |
| Arduino CLI | 可执行文件在 PATH，或设置 ARDUINO_CLI |
| Espressif ESP32 core | **3.3.12** |
| esptool | 推荐 **5.3.1**，命令行为 v5 格式 |
| .NET SDK | **10.x**；桌面程序目标 net10.0-windows |
| NuGet | 以 desktop/InkOS.Studio/packages.lock.json 为准 |

拉丁字体 `Fonts.cpp` 和合成 JPEG 自检样本已包含在源码中。0.5 构建前先运行 `python tools/generate_cjk_fonts.py` 生成共享中文字库归档和校验头。

## Python 环境

在仓库根目录执行：

```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
python -m pip install -r tools/requirements.txt
python -m pip install esptool==5.3.1
python -m unittest discover -s tests -v
python tools/check_repo.py
```

Linux/macOS 使用 `source .venv/bin/activate`，其余 Python 命令相同。Studio 的正式打包目标目前是 Windows x64。

## 安装 ESP32 core

```powershell
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
python tools/generate_cjk_fonts.py
python tools/build.py
```

精确板型参数：

```text
esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc
```

脚本显式设置 `upload.maximum_size=4194304`、使用仓库中的分区表及 vendored `libraries/`。需要自定义安装路径时：

```powershell
python tools/build.py --cli C:/Tools/arduino-cli.exe --config-file C:/Tools/arduino-cli.yaml
```

也可设置 `ARDUINO_CLI`、`ARDUINO_CLI_CONFIG_FILE`。中间文件默认在 `.build/firmware/`；产物和日志在 `out/firmware/`。所有目录都被 Git 忽略。SDK 自动生成的 merged 镜像会被删除。

SDK 也可能生成一个被忽略的 `firmware/InkOS/build/` 导出目录。发布构建使用 GCC prefix-map 和 C# PathMap 重映射本机路径，避免在断言／符号元数据中带出开发者的 Windows 用户目录。

## 0.6：A/B 与外部 Flash 迁移

当前构建使用两个 5.9375 MiB 应用槽、内部 4 MiB 字库和外部 4 MiB LittleFS。按 [OTA 完整说明](OTA.md) 完成一次 USB 迁移，以后在 Studio 的 Network → Firmware 经已配对 IP 上传应用包。迁移保留 NVS 原偏移，先备份外部原始内容、克隆校验并挂载文件系统，再写内部新布局。

## 历史 0.4：确认身份并备份烧录

以下 `flash.py` 只支持历史 factory 分区，不能用于当前 A/B 构建。

先关闭占用串口的 Studio 或串口监视器。以下 MAC 是占位示例，不是设备身份：

```powershell
python tools/flash.py --port COM9 --inspect
python tools/flash.py --port COM9 --expected-mac 00:11:22:33:44:55 --backup .local/backups/my-device-first.bin --dry-run
python tools/flash.py --port COM9 --expected-mac 00:11:22:33:44:55 --backup .local/backups/my-device-first.bin
```

将 MAC 替换为 inspect 输出。`--dry-run` 不访问 USB。真正烧录时工具再次核对身份，读取前 `0x20000` 字节到新备份文件，确认长度，再写三个区域。已存在的备份不会覆盖。重复烧录需另用新备份文件名。

可以用 `--images PATH` 指向解压的 Release 固件目录；其中应包含三个 `InkOS.ino.*.bin`。可用 `--esptool PATH` 指定独立 esptool 可执行文件。工具不执行擦除 NVS 的整片擦除操作。

首次从其他系统迁移时，还应使用 esptool 单独保存完整 Flash；操作前核对 [分区文档](HARDWARE.md)。

## Studio 构建

```powershell
dotnet restore desktop/InkOS.Studio/InkOS.Studio.csproj --locked-mode
dotnet build desktop/InkOS.Studio/InkOS.Studio.csproj -c Release --no-restore
dotnet publish desktop/InkOS.Studio/InkOS.Studio.csproj -c Release -r win-x64 --self-contained true -o out/studio
```

运行 `out/studio/InkOS Studio.exe`。不要只复制 EXE，需保持 DLL、runtime、字体和许可证一起。发布包本身不要求目标电脑另装 .NET。

`--self-test`、`--render-preview`、`--device-smoke`、`--network-smoke` 是应用开发检查模式；后两个会访问真实串口并写回设置；network-smoke 临时启用 USB 配对，经真实 IP 验证读写、重放拒绝和 Wi-Fi 保留，结束时撤销配对。它们，只在明确准备好设备时使用。普通构建不启动 GUI，也不连接设备。

## 产物打包与截图

```powershell
python tools/capture_pages.py --url http://192.168.1.100
python tools/package.py --firmware out/firmware --studio out/studio --bridge out/storage-bridge/storage_bridge.ino.bin
```

地址替换为设备的实际 DHCP IP；本版未实现 inkos.local 的 mDNS 名称。截图保存在 `out/captures/`，工具最后返回主页。打包前需生成 `out/assets` 。打包工具使用允许清单，版本号默认取自 `Ota.cpp` 与 `InkOS.Studio.csproj`，输出到 `out/release-<固件版本>/`；分别输出仅含应用的 OTA 包、带共享字库的 USB 迁移包和 Studio 发布包，并生成 SHA256SUMS；不自动上传串口日志、设备配置和备份。

Release 二进制的 SHA 应以 Release 的 SHA256SUMS 为准，不假定不同工具链、构建时间和路径下都能产生逐字节相同的文件。

## InkOS 0.6 A/B layout

Current sources build a 5.9375 MiB-per-slot application with internal immutable compressed fonts and external 4 MiB LittleFS. Run `python tools/generate_cjk_fonts.py` before the first build, and compile `tools/storage_bridge` for USB migration. **Do not use legacy `tools/flash.py` for the new layout.** Follow [OTA and one-time USB migration](OTA.md). Subsequent updates use the app-only OTA ZIP. Optional pie-chart reports require `pip install -r tools/requirements-report.txt` and `python tools/storage_report.py --ip DEVICE_IP --output out/flash-layout.png`; pairing input is hidden.


## Lucide 细线天气图标（0.7.5）

原始 SVG 在 assets/icons/lucide，固定 commit 与来源哈希见该目录的 upstream.json、manifest.json 和 SOURCE.md。安装 tools/requirements.txt 后运行 `python tools/generate_weather_icons.py`，生成固件 WeatherIconsData.h 与 out/lucide-thin-weather-icons.png。生成器用 fontTools 解析开放／闭合路径，离线绘制圆角细线，再一次性转为 16/24/40/64 px 单色掩码；屏幕运行时不缩放、不抗锯齿。现成生成头随源码保留，生成过程无需网络或浏览器。旧 MDI 原始资源保留作历史记录。

iCloud 的 pugixml 1.15 源码固定在 libraries/pugixml，并由 --libraries 编译；无需额外在线下载 XML 库。
