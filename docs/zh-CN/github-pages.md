# GitHub Pages 部署

[English](../en/github-pages.md)

站点地址为 <https://solar2ain.github.io/ink-tag/>。部署目录仅为 `webui/`，包含编译后的网页资源和 HTML 手册；不上传 CLI、固件、恢复备份、设备数据或 agent 指令文件。

合入 `main` 后，修改 `webui/`、部署工作流或静态校验脚本会自动触发部署。也可在 Actions → Deploy InkTag Web → Run workflow 选择 `main` 手动运行。拉取请求只校验，不部署。

工作流先检查静态文件、敏感文件和资源引用，再使用 GitHub Pages 官方 Actions 上传 `webui/`。无需 Node.js 构建环境，也不从其他位置获取工程源码。

私有仓库需要支持私有 Pages 的 GitHub 套餐。2026-10-18 启用本站时，GitHub 返回 HTTP 422：当前套餐不支持。仓库可见性保持不变；套餐支持后重新运行工作流即可尝试自动启用，也可在 Settings → Pages 将 Source 设为 GitHub Actions。

Pages 是纯静态页面，不提供本地数据与 Find 后端。首次访问仍需浏览器支持 Web Bluetooth，并由用户授权连接设备。
