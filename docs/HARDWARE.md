# 硬件与连接

## 已验证配置

| 项目 | 规格 |
|---|---|
| 套件 | Seeed TRMNL 7.5 OG DIY |
| 主控 | Seeed XIAO ESP32-S3 Plus |
| Flash / PSRAM | 16 MiB / 8 MiB OPI |
| 面板 | 7.5 英寸黑白，800×480，UC8179 |
| 系统 | ESP32 Arduino 3.3.12 内置 FreeRTOS |
| 连接 | USB 数据线，用于供电、烧录、串口设置 |

产品资料：[Seeed 套件 PDF](https://files.seeedstudio.com/Bazaar/product_pdf/104991005.pdf)。尺寸相同的屏幕不一定兼容；移植前核对控制器、排线定义、BUSY 极性和电压。

## 固件 GPIO

以下数字均为 ESP32 GPIO，不是排线脚位，也不是所有 XIAO 板上的 D 编号。

| 信号 | GPIO | 说明 |
|---|---:|---|
| SPI SCK | 7 | 时钟 |
| SPI MOSI | 9 | 数据，屏幕没有使用 MISO |
| CS | 44 | 片选 |
| DC | 10 | 命令／数据 |
| RST | 38 | 面板复位 |
| BUSY | 4 | 低电平表示忙 |
| EN | 43 | 驱动电源使能，高有效 |
| KEY1 | 2 | 上一页，INPUT_PULLUP，按下为低 |
| KEY2 | 3 | 下一页 |
| KEY3 | 5 | 主页 |

映射源于 `Panel.cpp` 和 `Runtime.cpp`。SPI 为 Mode 0、MSB first，10 MHz。连线改变时先单独确认驱动，再验证上层页面。

## 首次连接与下载模式

1. 断电检查排线和锁扣，按该驱动板丝印／手册方向连接；不要根据其他型号的“触点朝上”经验猜测。
2. 接入 USB 数据线，确认系统出现 ESP32-S3 串口。
3. 运行 `python tools/flash.py --port COM9 --inspect` 获取目标芯片身份；这里 COM9 只是命令示例，应换成实际串口。
4. 若自动进入下载模式失败：按住主控 BOOT，短按 RESET，再松开 BOOT，重新检查串口号。
5. 烧录完成后，只短按 RESET，不按 BOOT。启动先白屏全刷，再绘制当前页，可能需要约 8 秒。

套件曾经出现插错连接导致 BUSY 一直不就绪；应先检查连接，不要直接更换波形试图修复接线故障。

## 分区与备份

| 分区 | 起始地址 | 大小 |
|---|---|---|
| nvs | `0x9000` | `0x6000`，24 KiB |
| otadata | `0x10000` | `0x2000` |
| factory app | `0x20000` | `0xC00000`，12 MiB |
| storage | `0xC20000` | `0x3E0000` |

这是当前应用合同，不是 Arduino 默认分区。`otadata` 名称存在不代表具备 OTA：本版只有一个 factory app，没有 OTA 更新槽。

烧录仅写 bootloader `0x0`、partition table `0x8000`、application `0x20000`。不写 `0xe000` 的默认 boot_app0，不上传 merged.bin，不运行 erase-flash。第一次适配其他固件前还应另存完整 Flash，以便恢复；工具自动保存的前 `0x20000` 字节只包含启动区及 NVS，并不是完整固件备份。

原始备份包含 Wi-Fi 等私人配置，保存在 `.local/backups/` 或仓库外，不进入 Git 与 Release。
