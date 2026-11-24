import { describe, expect, it } from 'vitest'
import {
  assertDialogOtaAttestation,
  assertGoodixOtaAttestation,
  assertTelinkPackageTarget,
  assertApplicationPackageFamily,
  assertNordicPackageTarget,
  syncClockAfterVerifiedOta,
} from './ota'

describe('firmware-family package compatibility', () => {
  it.each(['aesl22_nrf','nrf52832_priceetag'])('matches the authenticated Nordic target %s before handoff', family => {
    expect(()=>assertNordicPackageTarget(family,family)).not.toThrow()
    expect(()=>assertNordicPackageTarget(family,family==='aesl22_nrf'?'nrf52832_priceetag':'aesl22_nrf')).toThrow('不匹配')
    expect(()=>assertNordicPackageTarget('unknown',family)).toThrow('不匹配')
    expect(()=>assertNordicPackageTarget('',family)).not.toThrow()
  })
  it('accepts the explicit public nRF52811 build-target name for its signed native identity', () => {
    expect(()=>assertNordicPackageTarget('nrf52811_aesl','aesl22_nrf')).not.toThrow()
    expect(()=>assertNordicPackageTarget('nrf52811_aesl','nrf52832_priceetag')).toThrow('不匹配')
  })
  const pairs = [
    ['goodix', 'gr5331_zkel', 'gr5331_zkc21v'],
    ['goodix', 'gr551x_zkel', 'gr551x_zkc42v'],
    ['goodix', 'gr5513_zkel_internal', 'gr5513_zkel_internal'],
    ['onmicro', 'om6626_zkcxxb', 'om6626_zkcxxb'],
    ['onmicro', 'om6626_hsela', 'om6626_hsela'],
  ] as const
  it.each(pairs)('accepts only the exact %s / %s target %s', (ota, identity, target) => {
    expect(() => assertApplicationPackageFamily(ota, identity, target)).not.toThrow()
    for (const [, , other] of pairs) {
      if (other !== target) expect(() => assertApplicationPackageFamily(ota, identity, other)).toThrow('固件系列不匹配')
    }
    expect(() => assertApplicationPackageFamily(ota === 'goodix' ? 'onmicro' : 'goodix', identity, target)).toThrow('固件系列不匹配')
  })
  it.each(['', 'gr551x_zkel_foreign', 'om6626_zkc', 'hs9118'])('does not infer a family for %s', identity => {
    expect(() => assertApplicationPackageFamily('onmicro', identity, 'om6626_zkcxxb')).toThrow()
  })
})

describe('TLSR firmware-family target without a product-model table', () => {
  it('uses the exact signed family rather than a list of board products', () => {
    expect(() => assertTelinkPackageTarget('tlsr8359_hanshow', {board: 'tlsr8359_hanshow'})).not.toThrow()
  })
  it.each([undefined, {board: 'om6626_hsel'}, {board: 'tlsr8359_foreign'},
    {board: 'tlsr8359_foreign', compatible_boards: ['tlsr8359_hanshow']}])('rejects a missing or foreign family even if a compatibility list claims it (%s)', manifest => {
    expect(() => assertTelinkPackageTarget('tlsr8359_hanshow', manifest)).toThrow('固件系列不匹配')
  })
  it.each(['hs9118', 'tlsr8359_made_up', 'om6626_hsel'])('does not guess a TLSR target from %s', identity => {
    expect(() => assertTelinkPackageTarget(identity, {board: 'tlsr8359_hanshow', compatible_boards: [identity]})).toThrow('不是已识别')
  })
})

describe('Goodix OTA attestation', () => {
  const payload = {
    manifest: { compatibilityEpoch: 2 },
    application: { size: 212104, checksum: 0x12345678 },
  }
  it('accepts only the exact active image without inventing unavailable journal fields', () => {
    expect(() => assertGoodixOtaAttestation(
      { version: 2, appBytes: 212104, checksum: 0x12345678 }, payload,
    )).not.toThrow()
    expect(() => assertGoodixOtaAttestation(
      { version: 1, appBytes: 212104, checksum: 0x12345678 }, payload,
    )).toThrow('应用与升级包不一致')
    for(const runtime of [null,{version:2,appBytes:212103,checksum:0x12345678},
      {version:2,appBytes:212104,checksum:0x12345679}]) {
      expect(()=>assertGoodixOtaAttestation(runtime,payload)).toThrow('应用与升级包不一致')
    }
  })
})

describe('DA14585 OTA attestation', () => {
  const payload = { codeSize: 90432, crc32: 0x12345678 }

  it('accepts the exact running pQ application', () => {
    expect(() => assertDialogOtaAttestation(
      { version: 0x000102, appBytes: payload.codeSize, checksum: payload.crc32 },
      payload,
    )).not.toThrow()
  })

  it('rejects missing or mismatched running application metadata', () => {
    expect(() => assertDialogOtaAttestation(null, payload)).toThrow('应用与升级包不一致')
    expect(() => assertDialogOtaAttestation(
      { version: 0x000102, appBytes: payload.codeSize - 1, checksum: payload.crc32 },
      payload,
    )).toThrow('应用与升级包不一致')
    expect(() => assertDialogOtaAttestation(
      { version: 0x000102, appBytes: payload.codeSize, checksum: payload.crc32 ^ 1 },
      payload,
    )).toThrow('应用与升级包不一致')
  })
})

describe('shared OTA clock restoration', () => {
  it('runs one explicit sync only after the caller has verified the application', async () => {
    let calls = 0
    await expect(syncClockAfterVerifiedOta(async () => { calls += 1 })).resolves.toBeNull()
    expect(calls).toBe(1)
  })

  it('keeps a successful OTA usable when clock sync is rejected', async () => {
    await expect(syncClockAfterVerifiedOta(async () => {
      throw new Error('LOW_POWER')
    })).resolves.toBe('LOW_POWER')
  })
})
