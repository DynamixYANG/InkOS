# 硬件与连接

当前 0.5 的分区、U5 外部 Flash 引脚与首次迁移请以 [OTA 文档](OTA.md) 为准；本文 factory 分区和 `flash.py` 操作保留作历史 0.4 恢复参考。

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
| 编码器 A | 41 | INPUT_PULLUP，A/B 均以 CHANGE 中断采集 |
| 编码器 B | 42 | A 先变化对应下一页，反向对应上一页 |
| BAT_ADC | 1 | 电池 1:2 分压，使用校准 ADC 毫伏读数 |
| ADC_EN | 6 | 高有效，采样后拉低关闭分压电路 |

映射源于 `Panel.cpp` 和 `Runtime.cpp`。SPI 为 Mode 0、MSB first，10 MHz。连线改变时先单独确认驱动，再验证上层页面。

0.7 起编码器两相输入解码一个完整 Gray-code 周期后换一页，拒绝不合法跳变、往返抖动和未完成周期。按键接原 KEY3（GPIO5）与 GND，按下回 Home；原 KEY1/2 继续可用，但屏幕不再显示按键栏。A/B 与按钮仅接 3.3 V 逻辑并共地，不要向 GPIO 输入 5 V。旋转方向取决于实物接线，交换 A/B 会反转方向。

电池采样参考 [Seeed 官方 Arduino cookbook](https://wiki.seeedstudio.com/ogdiy_kit_works_with_arduino/#battery-voltage-monitoring)：GPIO6 拉高、等待 10 ms、平均 30 次 GPIO1 读数再关闭；本固件使用 eFuse 校准毫伏值乘 2，而非固定 3.6 V 标尺，也不叠加示例针对原始 ADC 的 0.968 经验修正。每分钟采样一次，按单节锂电池近似放电曲线估算百分比。充电、负载和温度会影响估值；这不是库仑计，也没有独立充电／电池存在检测引脚。未连接电池／异常电压显示电池加斜线，不显示百分比，不伪装成精确剩余容量。

外部 U5 ZB25VQ32 与屏幕使用不同的 SPI 总线。这台实板经 GPIO 电平辨识与 `5E4016` ID 读取验证：**CLK=GPIO11、MOSI=GPIO13、MISO=GPIO12、CS=GPIO40**，其中 CLK/MOSI 与提供的原理图标注相反。电源使能仍为 GPIO43。100 kHz 至 10 MHz 六档读取均通过，详细手册核对和测试边界见 [EXTERNAL-FLASH](EXTERNAL-FLASH.md)。这是当前实板的验证结果，不应直接推定所有板卡版本都相同。

0.6 起，U5 的完整 4 MiB 用于 LittleFS。内部 16 MiB 则保留 NVS/OTA 元数据、两个 5.9375 MiB 固件槽和 4 MiB 压缩中文字库。迁移及失败恢复见 [OTA](OTA.md)。源文件系统完整备份、外部芯片原始备份及校验通过后才切换布局；运行时禁止挂载失败后自动格式化。

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

0.7.1: Encoder edges use a single GPIO input-register snapshot for A/B, avoiding mixed states from sequential GPIO reads. Full-cycle/bounce rejection is retained. Input publication runs separately from HTTP/USB handling; the configurable encoder-only lock discards turns at capture time and rechecks before page publication. Its default duration is 2000 ms from full-page acceptance.
