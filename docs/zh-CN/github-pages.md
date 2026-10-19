# GitHub Pages 部署

[English](../en/github-pages.md)

站点预定地址为 <https://solar2ain.github.io/ink-tag/>。当前仍为私有预览，本次更新不启用或部署 Pages，不改变仓库可见性及账号设置。

部署目录仅为 `webui/`，包含编译资源和 HTML 手册。CLI、固件、恢复备份、设备数据及 agent 指令不进入 Pages。推送和拉取请求只做静态校验，不自动部署。

将来明确启用部署时，在 Actions → Deploy InkTag Web → Run workflow 选择 `main` 手动运行。部署须先通过资源引用、敏感文件、隐藏内容、链接和 source map 校验，再上传 `webui/`；无需工程源码或 Node.js 构建环境。

私有仓库 Pages 和 Actions 的可用性取决于 GitHub 账号套餐与计费状态。配置文件不证明服务已经可用；启用前应核查 Settings → Pages。无需公开仓库或修改套餐来使用随附本地包。

Pages 是纯静态页面，不提供本地数据或 Find 后端。设备连接仍需支持 Web Bluetooth 的浏览器和用户授权。
