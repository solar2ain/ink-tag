# OM6626 ZKEL 基础版构建 / BASIC build

从源码根目录操作。使用单独安装的 OM6626 SDK，将其目录放在
`.sdk/onmicro/OM6626.SDK`；源码投影不包含 SDK、编译器或私有 Pro 实现。
SDK 仍受原厂许可约束。GNU Arm Embedded 10.3-2021.10（GCC 10.3.1）、
GNU make、Python 3.10 或更新版本是本构建配置的依赖。Windows 使用 WSL
执行下面的命令，工具链路径使用 WSL 路径。

```sh
make -C firmware/ports/onmicro/om6626 -f Makefile.base BOARD=om6626_zkc TOOLCHAIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin
```

原生配置 `om6626_zkc` 对应发行目标 `om6626_zkel`，
`om6626_zkcxxb` 是同族原生别名。构建会增量物化基础版源码，
检查 ELF/BIN/HEX 和实际地址边界，分别生成 App 与公共 Recovery：

- `.sdk/onmicro/OM6626.SDK/projects/ble_app_inktag_base/gcc/InkTag-<BOARD>-base.{elf,bin,hex,map}`
- `.sdk/onmicro/OM6626.SDK/projects/inktag_om_swap_boot_base/gcc/InkTag-om6626-recovery-base.{elf,bin,hex,map}`

原生构建及验证仅使用 Python 标准库。可选字体资源生成依赖完整共享客户端的
字体角色编译器及 `mini-racer>=0.12.4,<1`，OTA 文件打包另外依赖
`cryptography>=44,<51`、`pyelftools>=0.31,<1`：

```sh
python3 -m pip install 'mini-racer>=0.12.4,<1' 'cryptography>=44,<51' 'pyelftools>=0.31,<1'
make -C firmware/ports/onmicro/om6626 -f Makefile.base BOARD=om6626_zkc gui-asset-package
make -C firmware/ports/onmicro/om6626 -f Makefile.base BOARD=om6626_zkc TOOLCHAIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin ota
```

本地输出在 `_build/onmicro/<BOARD>/`。OTA 输出必须是新文件；已有包保留，
重新打包时可设置 `OTA_OUTPUT=/path/to/new-app-ota.zip`。它保留目标、
layout-v1、epoch1、CRC 与地址校验，不执行签名、设备写入或交付发布。
已安装 Recovery 自身的升级准入仍有效；生成本地包不等于设备已经允许升级。
配套 Recovery 文件的离线核对也不代表它已安装。

## English

Run from the source root with a separately installed OM6626 SDK at
`.sdk/onmicro/OM6626.SDK`, GNU Arm Embedded 10.3-2021.10, GNU make and Python
3.10+. On Windows run the commands in WSL. The command above builds the ZKEL
BASIC App and Common Recovery incrementally, with native ELF/BIN/HEX and address
validation. The source export contains no SDK, compiler, signing keys or Pro
implementation. See the adjacent third-party notices for SDK adaptations.

The optional `gui-asset-package` and `ota` targets use the dependencies listed
above. OTA packaging validates the actual paired native files and retains the
existing target/layout/epoch/integrity checks. It performs no signing, device
operation or publication. Use a new `OTA_OUTPUT` path to preserve an older
package. The installed Recovery's own admission rules remain authoritative.
