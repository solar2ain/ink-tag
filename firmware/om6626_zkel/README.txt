InkTag OM 完整刷机工具 0.2.0.7（Windows x64，PowerShell5.1/7）

本目录不含固件，不选择或下载默认固件。flash.ps1 必须显式提供外部签名 Full 包和准确 PCB 的 TOML。
使用 setup.ps1 准备固定依赖；check.ps1 为离线校验，不访问设备。保留确认提示，勿跳过完整回读。
发布者验签、真实容量、家族/layout、App/Recovery 配对和原生多阶段 loader 顺序不变。
所有格式、验签和默认二维码代码随工具固定，不启动、不导入、不寻找 InkTag CLI、BLE 或本地服务。

Full 仅保留本次同机新鲜读取、完整且验签通过的原生 Pro 记录；不签发新码，不导入旧备份/手填 token。
旧设置缺失、旧/未知布局、原厂无 InkTag 信息都不作为通用拒绝条件。成功读回但无法验证的旧授权告警后清除，初始化 Free；不恢复成 Pro。
NVDS 与 BKUP 两扇区按实际标记识别角色，两种物理顺序均支持。有效旧地址优先保留；缺失/无效时用本次稳定双读 Flash UID 派生确定性默认地址，写入原生 tag1。
无效旧校准字段不复制，不制造校准值，使用既有 SDK/出厂默认路径。实际通信读取失败、换靶、镜像损坏/验签失败/目标或容量不符仍在擦除前拒绝。
ZK 的 NFC UID 无宿主读取通道：只保证原授权字节同机保留，真正 Pro 仍由未修改的 App 启动验硬件 UID。
宿主不以 BLE 地址代替授权身份。HS 另核重复读取的 Flash UID。此工具不改 App/Recovery/协议/epoch/签名 ABI。

Full 清理主 Flash 的用户设置、密码/QR、Find、字体、内置图槽和崩溃/健康记录；保留必要出厂身份、BLE 地址、校准和 boot 数据。
OM42B 外部图像 Flash 不访问、不声称擦除。全部 Flash 回读后，仅清理仍暂停目标既有44B保留 RAM 健康邮箱并读回。
不清整片 SRAM，不恢复执行旧程序。完成后必须完整断电重上电以清除旧硬件复位原因；启动结果需再观察确认。
任一未知写结果停止，不自动重放。运行日志与设备读回均属私有证据，不属于可发布工具目录。

使用示例（保留默认确认）：
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/flash.ps1 -Firmware <外部签名Full.zip> -Config tools/configs/ZK_EL_OM42BA_V1.0.toml

Plaintext tools-only delivery; no firmware or private signing keys. Full preserves only freshly captured,
valid native same-unit authorization. It clears user/health data, retains required factory identity,
and leaves unsupported external Flash intact. After bounded retained-mailbox cleanup, a complete power
cycle is mandatory. The unchanged App verifies NFC hardware binding at boot; the host does not infer
it from a BLE address. Signature, capacity, pairing and readback gates remain mandatory.
Unreadable optional old formats initialize defaults with warnings. Unverifiable or known foreign old
authorization is discarded to Free, never promoted to Pro. A valid native address is retained; only
a missing/invalid one uses the deterministic same-operation stable Flash-UID default in native tag1.
