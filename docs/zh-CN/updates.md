# 客户端更新

中文 | [English](../en/updates.md)

InkTag WebUI 在“设置 → 客户端设置 → 关于”中显示自身版本与构建标识。客户端版本与设备固件版本分别检查，不互相替代。

更新检查仅提供正式版，仅包含已经批准并发布的附件。

在线版、独立 HTML 和本地服务均读取本项目 GitHub Pages 的 `updates/stable.json`。请求不包含设备地址、身份、密码或 Find 数据，也不使用内置 GitHub Token。网络故障或超时不影响连接设备与普通操作。

检查更新后，可从项目 GitHub Releases 下载所需附件并按安装手册更新。网页不会自动安装客户端或刷写设备。第一期仅提供客户端，固件更新列表为空。

以后提供固件时，必须同时匹配准确 board_id、固件家族及协议、布局和兼容代际。缺少这些信息时不会推荐 OTA。升级仍使用“下载附件 → 选择本地文件 → 正常 OTA”；SHA256 只用于完整性检查，不能代替固件签名验证与设备验身。

## 发布维护

公开发布前还必须通过 `python tools/verify-release.py --public --candidate`
及实际 ZIP、sdist、wheel 的许可检查。当前内嵌渲染器对应源码、部分插件／字库／
图片来源及 WebUI 二进制条款尚未核实，不能公开。新的独立审核收据必须采用
schema 2、purpose `public-client-release`，绑定最终提交、发布记录和附件哈希；
旧私有预览收据不适用。

发布工具默认不部署 Pages。审核并获授权后，只有显式 `--execute --deploy-pages`
才会在 Release 回下载校验、正式清单推送全部成功后发起手动部署，传入准确提交。
不带 `--deploy-pages` 时结果为 Release 已发布、Pages 待部署，不是整体完成。
部署失败时检查 `.release-local/publication-<tag>.json`，不要重复创建 Release。
Pages 尚未启用、Actions 账号付款／额度限制是外部阻断，工具不修改这些设置。

唯一发布记录为 `releases/record.json`（schema 1）。`tools/generate-updates.py` 校验已下载的 Release 附件大小和 SHA256，再生成 schema 3 正式版清单；未批准或未发布的候选不会出现。当前版本禁止发布固件。

```sh
python tools/publish-updates.py --repo . --tag <版本标签>
```

以上仅预演，不上传、不提交、不部署。独立审核人员确认精确提交、发布记录、附件和元数据哈希后，才可使用本地审核收据与 `--execute` 执行：推送审核分支、建立草稿 Release、回下载校验附件、发布 Release、生成并推送清单。执行中途失败时应先检查实际状态，不盲目重试。

**当前脚本不会触发 Pages 部署。** `.github/workflows/pages.yml` 的部署任务只接受
`main` 上的手动触发；普通推送或 PR 仅运行验证。发布脚本结束不代表网页或在线
更新清单已经更新。待发布授权、许可审核和附件核验全部完成后，维护者还需在
GitHub Actions 中手动运行 `Deploy InkTag Web`，选择 `main`。确认该次运行的提交
就是包含已批准清单的提交，验证和部署任务均成功，再核对在线 `updates/stable.json`
与已审核文件一致，才能宣布网站更新完成。

上线前须确认 Pages 已启用、Actions 可以实际运行；因账号或计费限制未启动的任务
不算验证通过。Pages 工作流必须先通过独立审核并包含验证任务；私有仓库的 Pages
套餐及 Actions 计费限制仍适用，不通过更改可见性绕过。首次真实上线后仍需验证
跨域读取、独立 HTML 与手机访问。任何文档或代码变动后，都须重新生成相关附件、
网页手册及校验和，并使审核收据绑定最终提交与文件。
