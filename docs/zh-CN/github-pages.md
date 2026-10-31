# 在线版与本地版

电脑推荐 Chrome 或 Edge；iPhone／iPad 推荐 [Bluefy](https://apps.apple.com/app/id1492822055)；Android 首选 Chrome。不要在微信等内置窗口中连接设备，请在上述浏览器中打开网页并允许蓝牙访问。

中文 | [English](../en/github-pages.md)

在线使用可直接打开 [InkTag WebUI](https://solar2ain.github.io/ink-tag/)，无需安装。建议使用支持 Web Bluetooth 的 Chrome 或 Edge；首次连接时，点击连接按钮并授权准确的设备。

| 使用方式 | 适合什么情况 | Find |
| --- | --- | --- |
| 在线 WebUI | 直接连接、传图、编辑页面和管理设备 | 可登记和控制设备广播；查询位置需本地服务 |
| standalone ZIP | 希望把网页保存在本机 | 与在线版相同；完整解压后打开 HTML，保留 assets 文件夹 |
| 静态 ZIP | 自行托管网页 | 不包含查询后台 |
| CLI ZIP（含本地 WebUI） | 需要本机数据、Find 查询或自动化任务 | 提供本地查询服务，首次使用按提示配置依赖 |

本地版需要 Python 3.11 或以上，不需要 Node.js。安装与启动步骤见 [WebUI 使用说明](webui.md)；Find 查询与设备信息迁移见 [Find 手册](find.md)。

手机访问电脑上的本地服务时，两者需在同一局域网，并按启动提示使用可信 HTTPS。普通局域网 HTTP 通常不能使用浏览器蓝牙；手机浏览器自身也需支持 Web Bluetooth。

在线站点加载失败时可稍后重试或使用本地版。切换网址、浏览器或清理网站数据可能影响已授权设备及浏览器内保存的设置，请先导出需要保留的设备信息。客户端更新不会自动刷写固件。
