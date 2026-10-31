# InkTag om6626_zkel 完整烧录说明

中文 | [English](README.en.md)

工具版本 **0.2.0.7**。适用于 Windows x64、Windows PowerShell **5.1**（也支持 PowerShell 7）。工具与固件独立提供，不需要安装 InkTag CLI。

## 开始前

1. 保留完整工具目录，在**本 README 所在目录**打开终端。下方每个代码块都是可单独复制的一行命令，不使用 `&`。
2. 下载对应主控的 **完整固件**，保留文件原名，放在本目录的**上一级目录**。不要将 OTA 包交给完整烧录工具。
3. 核对设备型号、主板丝印与实际屏幕，再选择下方配置。同一型号可能存在不同主板；同一主板也可能换装不同屏幕。
4. 接好稳定电源、共地和烧录线。保持默认确认和完整回读；首次烧录不要跳过回读。

本版示例使用的完整固件文件：`InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip`。如果移动或重命名文件，只需替换 `-Firmware` 路径；工具不会绑定、自动搜索或下载这一版固件。

除必需的 `-Firmware`、`-Config` 外，烧录命令默认只加 `-RetryDetect`。它只重试最初的设备检测，不会重放擦写或结果未知的操作。速度、超时和确认方式均使用工具默认值。

## 准备工具

安装 Python **3.12.0** x64、官方 J-Link **7.88j** 与 OM6626 J-Link 支持文件，按 `DEPENDENCIES.txt` 准备并核对依赖。将该 Python 加入 PATH 后运行；软件或支持文件在其他目录时，使用 `setup.ps1 -Help` 查看路径参数。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\setup.ps1"
```


```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\check.ps1"
```

## 按设备和主板选择配置

### ZKC21V-N（OM 版） — ZK_EL_OM6626_2.13_V1.1.toml

- 主板丝印：**ZK_EL_OM6626_2.13_V1.1**。
- 屏幕：128×250，黑白红。
- 配置：`tools/configs/ZK_EL_OM6626_2.13_V1.1.toml`。原装三色屏，带 NFC。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip" -Config ".\tools\configs\ZK_EL_OM6626_2.13_V1.1.toml" -RetryDetect
```

### ZKC21B-NAE4 — ZK_EL_OM21BA_V1.1.toml

- 主板丝印：**ZK_EL_OM21BA_V1.1**。
- 屏幕：128×250，黑白红黄。
- 配置：`tools/configs/ZK_EL_OM21BA_V1.1.toml`。原装四色屏。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip" -Config ".\tools\configs\ZK_EL_OM21BA_V1.1.toml" -RetryDetect
```

### ZKC42B-NE4 — ZK_EL_OM42BA_V1.0.toml

- 主板丝印：**ZK_EL_OM42BA_V1.0**。
- 屏幕：400×300，黑白红黄。
- 配置：`tools/configs/ZK_EL_OM42BA_V1.0.toml`。原装四色屏；独立外置图片 Flash 保留。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Firmware "..\InkTag-om6626_zkel-full-v0.2.0.0-8cbf461a8efd.zip" -Config ".\tools\configs\ZK_EL_OM42BA_V1.0.toml" -RetryDetect
```

## 烧录后的数据与检查

完整烧录会重置设备设置、密码、设备端 Find 信息、用户字库、旧图片状态与崩溃记录。旧图片可能只是失效，并非安全擦除；无法清理的独立外置 Flash 保留。主机上的 Apple 会话、Find 身份资料和轨迹不由本工具删除。

主板必需的出厂身份、蓝牙地址及校准按工具规则保留。只保留本次同机读取且验证有效的原有 Pro 记录，不从其他设备或旧备份复制授权。原厂或无有效授权的设备初始化为 Free。

看到确认提示后再输入 `y`。最终成功以全部要求的回读与验证通过为准；单个阶段达到 100% 不代表完成。遇到错误先检查接线、目标、固件和配置，不盲目重复擦写。

完成后请完整断电再上电。ZKC42B 的独立外置图片 Flash 不在本次清理范围。

## 探测、备份和帮助

`probe.ps1` 用于探测，不需要固件；已有 `backup.ps1` 时可单独保存原厂内容。它们位于上面的同一工具子目录。先查看入口帮助和输出位置，不在走时或功耗测试期间探测。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ".\tools\flash.ps1" -Help
```

工具不包含密钥、厂商 SDK、固件源码或用户备份。保留整个目录及随附 LICENSE、NOTICE、字体／图标及依赖许可文件；不要只复制单个脚本。
