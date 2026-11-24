import { describe, expect, it } from 'vitest'
import { boardDisplayName, boardProfiles, canonicalBoardId, firmwareFamilyProfile, mcuDisplayName, resolveBoardProfile, resolveBoardLedCapability, screenPinNames, TLSR_FIRMWARE_FAMILY } from './boards'

describe('MCU presentation', () => {
  it.each([
    ['tlsr8359_nebular154', 'TLSR8359'], ['tlsr8359_hsel', 'TLSR8359'],
    ['hs_nowa154', 'TLSR8359'], ['OM6626_HSEL', 'OM6626'],
    ['gr5331_zkel', 'GR5331'], ['gr5513_zkc42v', 'GR5513'],
    ['gr5515_zkc42vm', 'GR5515'], ['gr551x_zkel', 'GR551x'],
    ['nrf52832_priceetag', 'NRF52832'], ['da14585_asel29', 'DA14585'],
    ['', '--'], ['hs9118', '--'], ['unrecognized_model', '--'],
    ['tlsr8359foreign', '--'],
  ])('shows only the known MCU for %s', (identity, expected) => {
    expect(mcuDisplayName(identity)).toBe(expected)
  })
  it('does not change compatibility identities while formatting labels', () => {
    expect(canonicalBoardId(' TLSR8359_NEBULAR154 ')).toBe('tlsr8359_nebular154')
    expect(resolveBoardProfile('unknown')).toBeUndefined()
  })
  it.each([TLSR_FIRMWARE_FAMILY, 'gr5331_zkel', 'gr551x_zkel', 'om6626_zkcxxb', 'om6626_hsela'])('does not infer per-board GPIO or fitted peripherals from %s', identity => {
    expect(firmwareFamilyProfile(identity)).toBeDefined()
    expect(resolveBoardProfile(identity)).toBeUndefined()
    expect(resolveBoardLedCapability(identity, false)).toBe('none')
    expect(resolveBoardLedCapability(identity, true)).toBe('rgb')
  })
  it.each(['unknown', '__proto__', 'constructor', 'om6626_zkc', 'hs9118', 'gr551x_zkel_foreign'])('does not guess a firmware family for %s', identity => {
    expect(firmwareFamilyProfile(identity)).toBeUndefined()
    expect(resolveBoardProfile(identity)).toBeUndefined()
    expect(typeof boardDisplayName(identity)).toBe('string')
  })
})

describe('board screen pins', () => {
  it.each([
    ['aesl22_nrf', 'nrf52811_asel22'], ['hs_nowa154', 'tlsr8359_nowa154r'],
    ['hs_nowa213', 'tlsr8359_nowa213r'], ['hs_nebular213', 'tlsr8359_nebular213r'],
    ['hs_nebular420', 'tlsr8359_nebular420r'], ['aesl29_da', 'da14585_asel29'],
    ['aesl42_da', 'da14585_asel42'], ['da14585_aesl', 'da14585_asel'],
    ['OM6626_ZKC21B', 'om6626_zkc21b'], ['NRF52832_PRICEETAG', 'nrf52832_priceetag'],
    ['GR5331_ZKC21V', 'gr5331_zkc21v'], ['GR5513_ZKC42VM', 'gr5513_zkc42vm'],
    ['GR5515_ZKC42VM', 'gr5515_zkc42vm'],
  ])('displays %s using the lowercase MCU_board contract', (legacy, name) => {
    expect(boardDisplayName(` ${legacy} `)).toBe(name)
    expect(boardDisplayName(name)).toBe(name)
  })
  it('does not guess an exact MCU for an old shared GR runtime ID', () => {
    expect(boardDisplayName('gr551x_zkc42vm')).toBe('gr551x_zkc42vm')
    expect(resolveBoardProfile('tlsr8359_nowa154r')).toBe(boardProfiles.hs_nowa154)
    expect(resolveBoardProfile('nrf52811_asel22')).toBe(boardProfiles.aesl22_nrf)
    expect(resolveBoardProfile('gr5513_zkc42vm')).toBe(boardProfiles.gr551x_zkc42vm)
  })
  it('keeps the recovered OM6626 panel bus visible and editable', () => {
    const board = boardProfiles.om6626_zkc42b
    expect(screenPinNames.map((name) => board.screenPinValues?.[name])).toEqual([
      21, 15, 22, 20, 16, 3, 255, 255, 255, 7,
    ])
    expect(board.screenPins?.EN).toBe('GPIO07')
  })

  it('preserves registry notation and explains runtime-configured wiring', () => {
    expect(boardProfiles.gr5331_zkc21v.screenPins?.CS).toBe('GPIO0A')
    expect(boardProfiles.da14585_aesl.screenPins?.EN).toBe('P2.3')
    expect(boardProfiles.aesl22_nrf.screenPins?.MOSI).toBe('设备配置')
  })

  it('uses only canonical DA exact-board IDs', () => {
    expect(boardProfiles.aesl42_da.label).toBe('AESL-4.2')
    expect(boardProfiles.aesl29_da.label).toBe('AESL-2.9')
    expect(canonicalBoardId('da14585_aesl42')).toBe('da14585_aesl42')
    expect(canonicalBoardId('da14585_aesl29')).toBe('da14585_aesl29')
    expect(resolveBoardProfile('da14585_aesl42')).toBeUndefined()
    expect(resolveBoardProfile('da14585_aesl29')).toBeUndefined()
  })

  it('keeps the proposed ZKC21V shared-pin buzzer unavailable', () => {
    const board = boardProfiles.gr5331_zkc21v
    expect(board.capabilities.button).toBe(false)
    expect(board.capabilities.auxReserved).toBe(false)
    expect(board.auxIoDefaults).toBeUndefined()
  })

  it('records Nordic typed-image storage while leaving runtime capability discovery authoritative', () => {
    expect(boardProfiles.aesl22_nrf.capabilities.imageStore).toBe(true)
    expect(boardProfiles.nrf52832_priceetag.capabilities.imageStore).toBe(true)
  })

  it('does not accept unpublished Hanshow board aliases', () => {
    expect(canonicalBoardId('hanshow_nowa_154r_n')).toBe('hanshow_nowa_154r_n')
    expect(canonicalBoardId('hanshow_nebular_420r_n')).toBe('hanshow_nebular_420r_n')
    expect(resolveBoardProfile('hanshow_nowa_154r_n')).toBeUndefined()
    expect(boardProfiles.hanshow_nowa_154r_n).toBeUndefined()
  })
})
