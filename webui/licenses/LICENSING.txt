# 许可范围 / Licensing scope

## CLI

InkTag 自有的 CLI 源代码采用 **GNU Affero General Public License, version 3 only**
（SPDX：`AGPL-3.0-only`）。完整正文见 [cli/LICENSE](cli/LICENSE)。
本声明不将 WebUI、固件或本仓库其他独立内容一并授予 AGPL 许可。
第三方代码、字体及其他资源保留各自的版权和许可，见
[第三方声明](THIRD_PARTY_NOTICES.md)。

CLI 当前包含 `nativePreviewWasm.js` 等编译资源。其许可归属、与 CLI 的组合关系、
对应源码及构建资料的提供方式仍须在公开分发前核实。这里没有授予链接例外，
也没有认定仅提供 Python 文件就满足整个发布包的对应源码要求；不得通过把组件
标为“资源”来绕过其适用的许可义务。

InkTag-owned CLI source code is licensed under the **GNU Affero General Public
License, version 3 only** (`AGPL-3.0-only`); see [cli/LICENSE](cli/LICENSE).
This statement does not apply that license to WebUI, firmware or other independent
repository content. Third-party works retain their own licenses.

The CLI includes compiled resources such as `nativePreviewWasm.js`. Their
licensing, relationship to the CLI, and corresponding-source/build-material
arrangements require review before public distribution. This document grants no
linking exception and makes no claim that the Python files alone constitute
complete Corresponding Source for the package.

## WebUI

WebUI 仅分发编译版，不采用上述 CLI 的 AGPL 许可，也不提供 WebUI 源码授权。
InkTag 自有 WebUI 的二进制使用和再分发条款仍待权利人确定；第三方组件继续按
各自许可提供。本文件不覆盖或限制这些第三方许可授予的权利。

WebUI is distributed in compiled form only and is not licensed under the CLI's
AGPL grant. Binary-use and redistribution terms for InkTag-owned WebUI content
remain to be confirmed by the rights holder. Third-party licenses continue to
apply and their grants are not overridden by this document.

## 发布状态 / Release status

第一期不包含固件或烧录包。本次许可说明不代表发布包已通过合规审核或已公开发布。
公开上线前须关闭 [许可审核清单](LEGAL_REVIEW_REQUIRED.md) 中的未决项，并重新
生成、核对所有附件、清单和校验和。

The first release contains no firmware or flashing packages. This notice does
not constitute release approval. Resolve the [legal review checklist](LEGAL_REVIEW_REQUIRED.md)
and regenerate and verify all attachments, manifests and checksums before launch.
