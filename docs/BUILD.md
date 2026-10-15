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

生成固件不需要重建字体。生成的 `Fonts.cpp` 和合成 JPEG 自检样本已经包含在源码中。

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
python tools/build.py
```

精确板型参数：

```text
esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc
```

脚本显式设置 `upload.maximum_size=12582912`、使用仓库中的分区表及 vendored `libraries/`。需要自定义安装路径时：

```powershell
python tools/build.py --cli C:/Tools/arduino-cli.exe --config-file C:/Tools/arduino-cli.yaml
```

也可设置 `ARDUINO_CLI`、`ARDUINO_CLI_CONFIG_FILE`。中间文件默认在 `.build/firmware/`；产物和日志在 `out/firmware/`。所有目录都被 Git 忽略。SDK 自动生成的 merged 镜像会被删除。

SDK 也可能生成一个被忽略的 `firmware/InkOS/build/` 导出目录。发布构建使用 GCC prefix-map 和 C# PathMap 重映射本机路径，避免在断言／符号元数据中带出开发者的 Windows 用户目录。

## 确认身份并备份烧录

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

`--self-test`、`--render-preview`、`--device-smoke` 是应用开发检查模式；最后一个会访问真实串口并写回设置，只在明确准备好设备时使用。普通构建不启动 GUI，也不连接设备。

## 产物打包与截图

```powershell
python tools/capture_pages.py --url http://192.168.1.100
python tools/package.py --firmware out/firmware --studio out/studio
```

地址替换为设备的实际 DHCP IP；本版未实现 inkos.local 的 mDNS 名称。截图保存在 `out/captures/`，工具最后返回主页。打包工具使用允许清单，只收集固件三件套或 Studio 发布目录，并生成 SHA256SUMS；不自动上传串口日志、设备配置和备份。

Release 二进制的 SHA 应以 Release 的 SHA256SUMS 为准，不假定不同工具链、构建时间和路径下都能产生逐字节相同的文件。
