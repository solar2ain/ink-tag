# InkTag

中文 | [English](README.en.md)

InkTag 是面向多种主控电子价签的低功耗显示固件和管理系统。它将电子纸变成日历、时钟或图片展示屏，适用于桌面信息牌、照片展示和日常提醒。通过 InkTag WebUI 或 Python CLI，可配置屏幕、管理图片和升级固件；设备完成配置后无需持续连接电脑。

墨水屏利用环境光呈现接近纸张的显示效果，不依靠背光持续发光，在合适照明下具有较舒适的阅读体验。

## 主要功能（Free / Pro）

| 功能 | 基础功能（Free） | 激活后（Pro） | 说明 |
| --- | :---: | :---: | --- |
| 图片、日历、时钟、二维码与设备信息 | ✓ | ✓ | 支持范围以设备实际能力为准 |
| 图片存储 | ✓ | ✓ | Free：可视区长边小于 300 像素最多 10 张，其余最多 4 张；Pro 不设数量配额，以图槽总空间为限。实际数量还受图片大小和存储容量限制 |
| 图片轮播 | ✓ | ✓ | 保存图片后可按间隔自主切换 |
| 屏幕与引脚配置 | ✓ | ✓ | 屏幕型号、方向及已支持的屏幕／LED 引脚设置 |
| 画板与天气站本机预览 | ✓ | ✓ | 编辑控件、图片抖动、原稿导出、主机渲染预览 |
| 灯效、按键、蜂鸣器 | ✓ | ✓ | 按键、蜂鸣器功能暂未实现；配置入口不代表功能已开放 |
| 更新与 Recovery 维护 | ✓ | ✓ | 通过兼容客户端更新已安装的 InkTag |
| NFC 碰一碰连接 | ✓ | ✓ | 被动设备链接，需要主板配有 NFC；浏览器首次连接仍需授权 |
| 隐藏 InkTag 标识 | — | ✓ | Free 显示固定标识；Pro 可隐藏或调整 |
| 防丢器（利用苹果 Find My 网络） | — | ✓ | 需安装兼容固件并配置身份；不是 GPS 实时定位，也不是 Apple 官方客户端 |
| NFC 切换页面 | — | ✓ | 需对应主板及固件支持 |
| NFC 传图 | — | 后续待支持 | 尚未开放 |
| 自定义动态布局（设备离线运行） | — | 后续待支持 | 尚未开放；主机天气站由电脑渲染普通图片，不在设备执行布局 |
| 改广播名、设置密码等更多功能 | — | ✓ | 依设备能力开放 |
| 主机动态天气自动推送 | — | ✓ | CLI 定时取数与推送；主机需在线，具体目标仍需实板验证 |

nRF52811、DA14585 的独立项目采用 Free-only 功能范围，图片容量不套用上述商业配额；其固件不在本期提供。激活不能增加缺失的硬件。Pro 详情见 [Pro 能力](docs/zh-CN/pro.md)；捐赠获取激活码的渠道与规则尚未开放，请以项目公告为准。

## 墨水屏的特点

- **类似纸张的显示**：依靠环境光阅读，暗处需要外部照明。“护眼”不代表医疗保证，仍需注意照明、距离和休息。
- **刷新较慢并伴随闪烁**：全刷通常需要几秒到十几秒，多色屏或低温下可能更久。请保持供电并等待完成。
- **静态保图无需持续供电**：屏幕在刷新后保持画面不需要持续耗电，断电后通常仍能保图；主控、蓝牙和计时并非零功耗。
- **适合低频更新**：刷新不频繁、设备保持低功耗待机时，纽扣电池也能使用很长时间。续航取决于电池、温度、刷新与无线活动。

了解更多：[Good Display 中文介绍](https://www.good-display.cn/news/212.html) | [English](https://www.good-display.com/faq/1.html)。

## 首次使用

图文步骤见[在线使用手册](https://solar2ain.github.io/ink-tag-manual/)。设备需已安装兼容 InkTag 固件。

### 使用 WebUI

1. 准备已安装兼容 InkTag 固件的设备及稳定供电。
2. 在支持 Web Bluetooth 的 Chrome 或 Edge 中打开 WebUI，点击“连接”并选择实际蓝牙名称。
3. 在设置中核对主控和屏幕，导入图片或选择创作模板，发送后等待屏幕刷新完成。
4. 支持图槽的设备可保存图片并启用轮播；断开蓝牙后仍可工作。

原厂设备需要先安装适配固件；本期没有首次烧录文件，请勿使用不明镜像或其他主控的镜像。完整步骤见[首次使用](docs/zh-CN/getting-started.md)。

### 使用 CLI

安装 CLI 后，可扫描设备、显示日历或时钟，上传图片并开启轮播：

```console
inktag device scan
inktag --device IKT_TL_1234 device info
inktag --device IKT_TL_1234 display calendar
inktag --device IKT_TL_1234 display clock
inktag --device IKT_TL_1234 image upload photo.jpg --slot next --name landscape
inktag --device IKT_TL_1234 slot list
inktag --device IKT_TL_1234 slideshow start --interval 5m
inktag --device IKT_TL_1234 slideshow stop
```

把 `IKT_TL_1234` 替换为扫描到的实际蓝牙名称，`photo.jpg` 替换为自己的图片文件。使用 CLI 前先断开 WebUI，避免同时占用设备；等待当前屏幕刷新完成再执行下一项显示操作。更多命令见 [CLI 使用说明](docs/zh-CN/cli.md)。

## 安装与使用

| 客户端 | 安装方式 | 使用说明 |
| --- | --- | --- |
| 在线 WebUI | 正式站点开放后访问 [InkTag Web](https://solar2ain.github.io/ink-tag/)；当前优先检查随附本地包 | [WebUI 手册](docs/zh-CN/webui.md) |
| 本地 WebUI／Find 服务 | 安装 Python 3.11+，解压本地服务包；Windows 运行 `install.cmd`，macOS 运行 `install.command`，Linux 运行 `sh install.sh` | Windows Find 还需完整 7-Zip，首次会下载运行依赖；macOS 原生认证仍待真机验证，Linux Find 登录暂未验证 |
| Python CLI | 解压 CLI 包，按包内说明安装独立环境；Windows、Linux、macOS 使用同一套源码 | [CLI 手册](docs/zh-CN/cli.md) |

本地服务默认使用 8787 端口，数据保存在当前用户的 InkTag 持久目录。服务重启不需要重建标签身份；不要删除数据目录来解决登录错误。Apple 账号密码只在本机界面输入，建议使用专门注册的账号，不要使用个人常用账号。

已有 Python 虚拟环境时，也可在解压后的 CLI 包内运行 `python -m pip install .`。

## 详细手册

[在线图文使用手册](https://solar2ain.github.io/ink-tag-manual/)

[首次使用](docs/zh-CN/getting-started.md) · [WebUI](docs/zh-CN/webui.md) · [CLI](docs/zh-CN/cli.md) · [已适配设备](docs/zh-CN/supported-devices.md) · [Pro 能力](docs/zh-CN/pro.md) · [客户端更新](docs/zh-CN/updates.md) · [常见问题](docs/zh-CN/faq.md)

使用前请阅读 [安全与隐私说明](SECURITY.md)。请勿在问题反馈中公开密码、激活码、定位数据或私钥。

在线访问及本地版的区别见[在线 WebUI 与本地版](docs/zh-CN/github-pages.md)。

## 致谢

感谢 [EPD-nRF](https://github.com/tsl0922/EPD-nRF5) 提供的 nRF 电子价签固件基础，为本项目的硬件适配与电子纸显示实现提供了重要参考。

## 许可

CLI 自有源码采用 AGPL-3.0-only；WebUI 仅提供编译版，不适用 CLI 许可。详见[许可范围](LICENSING.md)及[第三方声明](THIRD_PARTY_NOTICES.md)。
