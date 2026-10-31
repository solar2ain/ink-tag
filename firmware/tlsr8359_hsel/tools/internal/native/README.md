# InkTag tlsr8359_hsel 完整烧录说明

中文 | [English](README.en.md)

工具版本 **0.2.0.8**。适用于 Windows x64、Windows PowerShell **5.1**（也支持 PowerShell 7）。工具与固件独立提供，不需要安装 InkTag CLI。

## 开始前

1. 保留完整工具目录，在**本 README 所在目录**打开终端。下方每个代码块都是可单独复制的一行命令，不使用 `&`。
2. 下载对应主控的 **完整固件**，保留文件原名，放在本目录的**上一级目录**。不要将 OTA 包交给完整烧录工具。
3. 核对设备型号、主板丝印与实际屏幕，再选择下方配置。同一型号可能存在不同主板；同一主板也可能换装不同屏幕。
4. 接好稳定电源、共地和烧录线。保持默认确认和完整回读；首次烧录不要跳过回读。

本版示例使用的完整固件文件：`InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip`。如果移动或重命名文件，只需替换 `-Firmware` 路径；工具不会绑定、自动搜索或下载这一版固件。

除必需的 `-Firmware`、`-Config` 外，烧录命令默认只加 `-RetryDetect`。它只重试最初的设备检测，不会重放擦写或结果未知的操作。速度、超时和确认方式均使用工具默认值。

**TLSR 例外：**完整烧录必须指定串口。下方使用 `-Port COM3`，请改成设备管理器里烧录器的实际串口；其他参数保持默认。使用 CH340/SWS 接线，不能用 J-Link 的 SWD 接线替代。

## 准备工具

安装 Python **3.11 或更新版本**和 CH340 驱动，再运行依赖安装与检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\setup.ps1"
```

## 按设备和主板选择配置

### Hanshow Nowa-154R-N — HS_EL5101_5M_66_05.toml

- 主板丝印：**HS_EL5101_5M_66_05**。
- 屏幕：152×152，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HS_EL5101_5M_66_05.toml`。152×152 三色原屏，不用于 200×200 主板。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HS_EL5101_5M_66_05.toml" -RetryDetect -Port COM3
```

### Hanshow Stellar-S3TN@（型号待铭牌确认） — HS_EL5101_5M_60_04.toml

- 主板丝印：**HS_EL5101_5M_60_04**。
- 屏幕：200×200，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HS_EL5101_5M_60_04.toml`。P154030 200×200 三色装配；E0154A07-A1 屏兼容性尚未确认。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HS_EL5101_5M_60_04.toml" -RetryDetect -Port COM3
```

### Hanshow Nowa-213R-N — HS_EL5102_1M_67_33.toml

- 主板丝印：**HS_EL5102_1M_67_33**。
- 屏幕：128×250，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HS_EL5102_1M_67_33.toml`。2.13 寸三色原屏。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HS_EL5102_1M_67_33.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-213R-N — HSEL4Q_02_13M_30.toml

- 主板丝印：**HSEL4Q_02_13M_30**。
- 屏幕：128×250，黑白红黄。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_02_13M_30.toml`。2.13 寸四色原屏。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_02_13M_30.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-420R-N — HSEL4Q_04_20M_33.toml

- 主板丝印：**HSEL4Q_04_20M_33**。
- 屏幕：400×300，黑白红黄。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_04_20M_33.toml`。JD79668AA 四色装配。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_04_20M_33.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-420R-N — HSEL4Q_04_20M_33-SSD1683.toml

- 主板丝印：**HSEL4Q_04_20M_33**。
- 屏幕：400×300，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_04_20M_33-SSD1683.toml`。SSD1683 三色装配，与四色配置分开。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_04_20M_33-SSD1683.toml" -RetryDetect -Port COM3
```

### Hanshow Nebular-420R-N — HSEL4Q_04_20M_33-YW2427.toml

- 主板丝印：**HSEL4Q_04_20M_33**。
- 屏幕：400×300，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_04_20M_33-YW2427.toml`。YW2427 批次，SSD1683 三色屏。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_04_20M_33-YW2427.toml" -RetryDetect -Port COM3
```

### Hanshow StellarPro-154R-N（早期版） — HSEL5_01_54M_00.toml

- 主板丝印：**HSEL5_01_54M_00**。
- 屏幕：200×200，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL5_01_54M_00.toml`。可换屏版本，JD79653A 200×200 三色屏。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL5_01_54M_00.toml" -RetryDetect -Port COM3
```

### Hanshow StellarPro-154R-N（后期版） — HSEL5_01_54M_02.toml

- 主板丝印：**HSEL5_01_54M_02**。
- 屏幕：200×200，黑白红。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL5_01_54M_02.toml`。YW2116 固定屏版本；不能和 _00 配置互换。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL5_01_54M_02.toml" -RetryDetect -Port COM3
```

### Hanshow StellarPro-154Q-N — HSEL5Q_01_54M_00.toml

- 主板丝印：**HSEL5Q_01_54M_00**。
- 屏幕：200×200，黑白红黄。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL5Q_01_54M_00.toml`。200×200 四色屏；原屏完整画面和颜色效果仍待确认，暂不推荐安装。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL5Q_01_54M_00.toml" -RetryDetect -Port COM3
```

### Hanshow NebularPro-154Q-N — HSEL4Q_01_54M_31.toml

- 主板丝印：**HSEL4Q_01_54M_31**。
- 屏幕：200×200，黑白红黄。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL4Q_01_54M_31.toml`。200×200 四色屏；完整烧录验证仍待补齐。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL4Q_01_54M_31.toml" -RetryDetect -Port COM3
```

### Hanshow PolarisPro-260Q-N — HSEL3_02_66M_36.toml

- 主板丝印：**HSEL3_02_66M_36**。
- 屏幕：152×296，黑白红黄。
- 配置：`firmware/ports/telink/tlsr8359/tools/configs/HSEL3_02_66M_36.toml`。2.66 寸 152×296 四色屏；NFC 和待机电流仍待确认。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Firmware "..\InkTag-tlsr8359_hsel-full-v0.2.0.0-d4366a076b61.zip" -Config ".\firmware\ports\telink\tlsr8359\tools\configs\HSEL3_02_66M_36.toml" -RetryDetect -Port COM3
```

## 烧录后的数据与检查

完整烧录会重置设备设置、密码、设备端 Find 信息、用户字库、旧图片状态与崩溃记录。旧图片可能只是失效，并非安全擦除；无法清理的独立外置 Flash 保留。主机上的 Apple 会话、Find 身份资料和轨迹不由本工具删除。

主板必需的出厂身份、蓝牙地址及校准按工具规则保留。只保留本次同机读取且验证有效的原有 Pro 记录，不从其他设备或旧备份复制授权。原厂或无有效授权的设备初始化为 Free。

看到确认提示后再输入 `y`。最终成功以全部要求的回读与验证通过为准；单个阶段达到 100% 不代表完成。遇到错误先检查接线、目标、固件和配置，不盲目重复擦写。

## 探测、备份和帮助

`probe.ps1` 用于探测，不需要固件；已有 `backup.ps1` 时可单独保存原厂内容。它们位于上面的同一工具子目录。先查看入口帮助和输出位置，不在走时或功耗测试期间探测。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\firmware\ports\telink\tlsr8359\tools\flash.ps1" -Help
```

工具不包含密钥、厂商 SDK、固件源码或用户备份。保留整个目录及随附 LICENSE、NOTICE、字体／图标及依赖许可文件；不要只复制单个脚本。
