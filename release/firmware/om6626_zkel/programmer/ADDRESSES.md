# 初始化地址与边界 / Initialization addresses and boundaries

下表列出维护的 layout-v1 主 Flash 范围，供查看烧录日志。XIP = 物理地址 + 0x400000。
These layout-v1 main-Flash ranges identify the regions in the programming log.

| Region | Physical start | XIP start | Length / end |
|---|---:|---:|---|
| Common Recovery | 0x04000 | 0x404000 | actual BIN, at most 0x26000 |
| App + integrity/signature | 0x2A000 | 0x42A000 | actual package bytes, slot ends 0x78000 |
| User font bank | 0x78000 | 0x478000 | 0x8000, erased default; embedded fonts remain |
| Journal + empty images + settings | 0x82000 | 0x482000 | ZK ends 0xFF000; HS ends 0x100000 |
| Manufacturer boot/CPFT, skip | 0x00000 | 0x400000 | 0x4000 |
| Runtime MBR, skip | 0x80000 | 0x480000 | 0x2000 |
| ZK CONF, skip | 0xFF000 | 0x4FF000 | 0x1000; ZK only |

厂家加载器仍更新 ROM CRC 元数据 0x1184–0x118D 及必要的 0x2000 备份；独立预测
和回读核对这些既有动作。必须有厂家启动格式（0x1182=0x60、0x11FE=55AA），
不能把全片擦空后的未知厂家区填 FF 并称为保全。
The vendor loader still updates ROM CRC metadata and its required backup; independent
prediction/readback checks those changes. Missing manufacturer formatting cannot be
invented after whole-chip erase.

Full 清除普通设置、密码/Find、内部图片、用户字体及健康记录。外置 SPI 是独立
空间，本工具不访问/擦除它，不把巨大地址空洞拼成一份 BIN，OM42B 外置图片保留。
无效/缺失可选旧资料使用默认值；连接、容量、必需厂家读取、擦写/回读失败仍失败，
不自动重放写入。基础版和高级版沿用同家族 Common Recovery，但签名、身份和配对检查保留。
Full clears ordinary settings/password/Find/internal images/user fonts/health. External
SPI is a separate untouched space. Missing optional data defaults; genuine transport,
capacity, mandatory-read/write/readback errors remain failures without write replay.

这不是板型或外置硬件的新资格，不能把某个 MCU 地址窗口当作实际容量证明。
These addresses do not qualify new hardware or infer physical capacity from an address window.
