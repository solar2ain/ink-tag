# Third-party notices

The byte-bound partial inventory is
[`webui/licenses/BUNDLED-INVENTORY.json`](webui/licenses/BUNDLED-INVENTORY.json).
Observed runtime versions are React/React DOM 19.2.8, Dexie 4.4.5, React Router
7.18.2 and Leafer runtime 2.2.10 (not a plugin inventory). Exact-version upstream
license texts and Dexie's additional NOTICE are under `webui/licenses/`.
Ma Shan Zheng WOFF2 metadata identifies Version 2.003/OFL-1.1. These facts do
not resolve font modifications/provenance, image rights, every plugin or renderer
Corresponding Source. Public release remains blocked; see [release-materials.md](release-materials.md).

This first release contains CLI clients, compiled WebUI and user documentation;
it contains no firmware or flashing package. Third-party components retain their
own licenses and are not relicensed by the CLI's AGPL declaration. See
[LICENSING.md](LICENSING.md) for scope.

| Distribution area | Components identified | License evidence / remaining review |
| --- | --- | --- |
| CLI Python dependencies | bleak, cryptography, mini-racer, Pillow, qrcode, rich, typer and their installed dependencies | Requirements are declared in `cli/pyproject.toml`; confirm exact resolved versions and their original notices for each distributed package. These are not covered by an InkTag-owned-code grant. |
| CLI bundled JavaScript | epdoptimize and other rendering/font adapters | Apache-2.0 text for epdoptimize is included in `cli/src/inktag_cli/_assets/EPDOPTIMIZE_LICENSE.txt`; review other resources individually. |
| CLI compiled GUI module | `nativePreviewWasm.js` | Corresponding Source, build materials and combined-work licensing remain unresolved. |
| Compiled WebUI | React/React DOM, Fabric, Dexie, Lucide, RgbQuant, epdoptimize, Leafer components/plugins | See `webui/THIRD_PARTY_NOTICES.txt`. Exact versions, full dependency inventory and required notices still need verification. |
| WebUI font | Ma Shan Zheng | Upstream OFL-1.1 text and copyright are supplied in `webui/licenses/MaShanZheng-OFL.txt`; verify the shipped font's provenance and any modifications. |
| WebUI images and other fonts | Tutorial/screenshot images, sample artwork and generated font resources | Confirm ownership or redistribution permission for each asset; a filename is not license evidence. |
| Local-service Find runtime | Runtime files distributed inside the local-service ZIP | Inspect the actual archive, including its bundled `tools/findmy/third_party/os-findmy-e.LICENSE`; confirm any further dependencies and notices. |

The inventory is **not yet complete** and is a public-release gate. Do not label
all bundled code as AGPL, assume compiled assets have no attribution duties, or
treat an upstream project's current license as proof of the exact bundled version.

本期不含固件和烧录包。CLI 的 AGPL 声明不替代第三方组件的原许可；表中的未决项
须在公开分发前完成核查。新增的上游许可文本只补充声明，不代表整个包已通过合规审核。
