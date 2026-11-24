import type { ParsedOtaPackage } from '../../protocol/ota'

/** Synthetic, already-parsed display samples; not signed packages or device evidence. */
export const otaPresentationFixtures = [
  { id: 'gr', boardId: 'gr5331_zkel', platform: 'GR5331', appBytes: 110884,
    pkg: { type: 'goodix', summary: 'GOODIX 原始解析摘要 · 演示数据', payload: {
      fileName: 'InkTag-GR5331-v0.2.0-ota.zip', application: { size: 110884 }, firmware: new Uint8Array(111012), manifest: { release: '0.2.0', releaseVersion: 65736 },
    } } },
  { id: 'om', boardId: 'om6626_zkcxxb', platform: 'OM6626', appBytes: 120304,
    pkg: { type: 'onmicro', summary: 'ONMICRO 原始解析摘要 · 演示数据', payload: {
      fileName: 'InkTag-OM6626-v0.2.0-ota.zip', application: { size: 120304 }, firmware: new Uint8Array(120432), manifest: { release: '0.2.0', releaseVersion: 65736 },
    } } },
  { id: 'tlsr', boardId: 'tlsr8359_hanshow', platform: 'TLSR8359', appBytes: 122000,
    pkg: { type: 'telink', summary: 'TLSR8359 原始 CRC32 解析摘要 · 演示数据', payload: {
      fileName: 'InkTag-TLSR8359-v0.2.0-ota.zip', declaredSize: 122000, firmware: new Uint8Array(122128), manifest: { release: '0.2.0', releaseVersion: 65736 },
    } } },
  { id: 'nrf32', boardId: 'nrf52832_priceetag', platform: 'nRF52832', appBytes: 91352,
    pkg: { type: 'nordic', summary: '应用 91352 bytes · 签名初始化包 143 bytes · 演示数据', payload: {
      fileName: 'InkTag-nRF52832-v0.2.0-ota.zip', firmware: new Uint8Array(91352), initPacket: new Uint8Array(143),
    } } },
  { id: 'nrf11', boardId: 'aesl22_nrf', platform: 'nRF52811', appBytes: 69592,
    pkg: { type: 'nordic', summary: '应用 69592 bytes · 签名初始化包 143 bytes · 演示数据', payload: {
      fileName: 'InkTag-EL022HWRL-v0.2.0-ota.zip', firmware: new Uint8Array(69592), initPacket: new Uint8Array(143),
    } } },
  { id: 'da', boardId: 'da14585_aesl', platform: 'DA14585', appBytes: 81164,
    pkg: { type: 'dialog', summary: 'DIALOG SDK6 pQ 原始解析摘要 · 演示数据', payload: {
      fileName: 'InkTag-DA14585-v0.2.0-ota.img', codeSize: 81164, version: '0.2.0', image: new Uint8Array(81228),
    } } },
] satisfies Array<{ id: string; boardId: string; platform: string; appBytes: number; pkg: ParsedOtaPackage }>
