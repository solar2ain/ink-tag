import { describe, expect, it } from 'vitest'
import { deviceScreenProfiles as sharedDeviceScreenProfiles } from '../../../packages/inktag-client-core/src/screen-profiles'
import { controllerDisplayName, decodeScreenConfig, encodeScreenConfig, imageStoreCompatibilityChanged, makeScreenConfig, screenConfigsEqual, validateScreenConfig } from '../protocol/screen'
import {
  builtInScreenProfiles, compareScreenProfiles, dedupeScreenProfiles, findMatchingScreenProfile, findScreenProfileForDevice, findScreenProfileById, genericScreenProfiles,
  defaultProfileName, screenProfileFamily, screenProfileFamilyLabels, screenProfileNote,
  screenProfileTitle, screenRecipeDisplayName, screenProfileConfigForDevice, sortScreenProfiles, screenDriverAllowedOnBoard, screenProfileAllowedOnBoard, type ScreenProfile,
} from './screenProfiles'

describe('built-in screen profiles', () => {
  it('selects the GR5515 SSD1619 screen without borrowing the SSD1683 preset', () => {
    const selected = findScreenProfileById(builtInScreenProfiles, 'zkc42vm_gr5515_ssd1619')!
    const wire = Uint8Array.from([1, 0x20, 0, 2, 0, 144, 1, 44, 1, 0, 0, 0, 0, 144, 1, 44, 1])
    const current = decodeScreenConfig(wire)!
    expect(selected.device).toBe('ZKC42VM GR5515')
    expect(screenProfileTitle(selected)).toBe('SSD1619 4.2寸 400x300 三色')
    expect(encodeScreenConfig(screenProfileConfigForDevice(selected, null))).toEqual(wire)
    expect(findScreenProfileForDevice(builtInScreenProfiles, current)).toBe(selected)
    const ssd1683 = findScreenProfileById(builtInScreenProfiles, 'zkc42vm_original')!
    expect(ssd1683.device).toBe('ZKC42VM GR5513')
    expect(findScreenProfileForDevice([ssd1683], current)).toBeUndefined()
    expect(findScreenProfileForDevice([selected], ssd1683.config)).toBeUndefined()
    const shared = sharedDeviceScreenProfiles.find(p => p.id === selected.id)!
    expect(shared.config).toEqual(selected.config)
  })
  it('defaults HSEL3 to the human-confirmed 90 degrees and native X mirror', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hs_hsel3_266q')!
    expect(selected.config).toMatchObject({driver: 0x3f, recipe: 3, color: 3,
      width: 152, height: 296, transform: 5})
    expect(screenProfileConfigForDevice(selected, null).transform).toBe(5)
    expect(decodeScreenConfig(encodeScreenConfig(selected.config))).toEqual(selected.config)
  })
  it('offers the Hema 2.13-inch preset with its existing firmware recipe and orientation', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hema_213_bwr')!
    expect(selected).toMatchObject({ name: '盒马 2.13 寸三色屏', controllerLabel: 'SSD1619',
      adaptationLabel: '盒马213', device: '盒马 AESL EL022H2' })
    expect(selected.config).toMatchObject({ driver: 0x20, recipe: 1, color: 2,
      width: 128, height: 250, visibleX: 0, visibleY: 0, visibleWidth: 122, visibleHeight: 250, transform: 3 })
    expect(validateScreenConfig(selected.config)).toBe(true)
    const wire = encodeScreenConfig(selected.config)
    expect([...wire]).toEqual([1, 0x20, 1, 2, 3, 128, 0, 250, 0, 0, 0, 0, 0, 122, 0, 250, 0])
    expect(decodeScreenConfig(wire)).toEqual(selected.config)
    expect(findScreenProfileForDevice(builtInScreenProfiles, selected.config)).toBe(selected)
    expect(screenProfileTitle(selected)).toBe('SSD1619 2.13寸 128x250 三色 （适配盒马213）')
    expect(screenProfileNote(selected)).toContain('HEMA_213_BWR')
    expect(screenProfileAllowedOnBoard(selected, 'aesl22_nrf')).toBe(true)
    // Preserve a user's installed orientation, and do not relabel the standard recipe.
    const current = { ...selected.config, transform: 1, nativeUiScale: 1 }
    expect(screenProfileConfigForDevice(selected, current)).toEqual(current)
    const standard = genericScreenProfiles.find(p => p.id === 'generic_0f')!
    expect(standard.config.recipe).toBe(0)
    expect(findScreenProfileForDevice([selected], standard.config)).toBeUndefined()
  })
  it('keeps P154030 selectable with clockwise 90 degrees and horizontal mirror', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'ssd1681_p154030_bwr')!
    expect(selected).toMatchObject({ model: 'P154030', device: 'HS_EL5101_5M_60_04',
      name: 'SSD1681 1.54 寸三色屏',
      adaptationLabel: 'HS_EL5101_5M_60_04', controllerLabel: 'SSD1681' })
    expect(selected.config).toMatchObject({ driver: 0x2f, recipe: 1, color: 2,
      width: 200, height: 200, visibleWidth: 200, visibleHeight: 200, transform: 5 })
    expect(controllerDisplayName(selected.config.driver)).toBe(selected.controllerLabel)
    expect(selected.config.transform & 3).toBe(1)
    expect(selected.config.transform & 4).toBe(4)
    expect(selected.config.transform & 8).toBe(0)
    const wire = encodeScreenConfig(selected.config)
    expect([...wire]).toEqual([1, 0x2f, 1, 2, 5, 200, 0, 200, 0, 0, 0, 0, 0, 200, 0, 200, 0])
    expect(decodeScreenConfig(wire)).toEqual(selected.config)
    expect(findMatchingScreenProfile(builtInScreenProfiles, selected.config)).toBe(selected)
    expect(screenProfileConfigForDevice(selected, null).transform).toBe(5)
    expect(screenProfileAllowedOnBoard(selected, 'tlsr8359_hanshow')).toBe(true)
    expect(screenProfileAllowedOnBoard(selected, 'unknown')).toBe(true)
    expect(screenProfileTitle(selected)).toBe('SSD1681 1.54寸 200x200 三色 （适配HS_EL5101_5M_60_04）')
    expect(screenProfileNote(selected)).toBe('SSD1681 / 1.54寸 / 200x200 / BWR / HANSHOW_200_BWR_OLD')
    expect(screenProfileTitle(selected)).not.toContain('P154030')
    expect(builtInScreenProfiles.filter(p => p.model === 'P154030')).toHaveLength(1)
    // Existing device/user orientation stays authoritative until explicitly changed.
    const current = { ...selected.config, transform: 0, nativeUiScale: 1 }
    expect(findScreenProfileForDevice(builtInScreenProfiles, current)).toBe(selected)
    expect(screenProfileConfigForDevice(selected, current)).toEqual(current)
  })
  it('uses the agreed SSD1681 label while keeping HSEL5_02 native OTP distinct from P154', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hsel5_01_54m_02_bwr')!
    expect(selected.config).toMatchObject({ driver: 0x2f, recipe: 2, color: 2,
      width: 200, height: 200, visibleWidth: 200, visibleHeight: 200, transform: 5 })
    expect(validateScreenConfig(selected.config)).toBe(true)
    expect(Array.from(encodeScreenConfig(selected.config)))
      .toEqual([1, 0x2f, 2, 2, 5, 200, 0, 200, 0, 0, 0, 0, 0, 200, 0, 200, 0])
    expect(findMatchingScreenProfile(builtInScreenProfiles, selected.config)?.id).toBe(selected.id)
    expect(controllerDisplayName(0x2f, 2)).toBe('SSD1681')
    expect(controllerDisplayName(0x2f, 1)).toBe('SSD1681')
    expect(screenProfileTitle(selected)).toBe('SSD1681 1.54寸 200x200 三色 （适配HSEL5_01_54M_02）')
    expect(screenRecipeDisplayName(0x2f, 2)).toBe('适配hsel5_01_54m_02')
    expect(validateScreenConfig({ ...selected.config, color: 1 })).toBe(false)
    expect(validateScreenConfig({ ...selected.config, width: 208, visibleWidth: 208 })).toBe(false)
  })

  it('retains the qualified OM Hanshow 4.37-inch profile after shared catalog sync', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hsela_an0437a480')!
    expect(selected.model).toBe('AN0437A480')
    expect(selected.device).toBe('HSELAQ_04_37MG_38')
    expect(selected.config).toMatchObject({ driver: 0x3f, recipe: 5, color: 3,
      width: 176, height: 480, transform: 5 })
    expect(findMatchingScreenProfile(builtInScreenProfiles, selected.config)?.id).toBe(selected.id)
    expect(builtInScreenProfiles.filter(p => p.id === selected.id)).toHaveLength(1)
  })
  it('matches the observed HSEL5 JD79653A orientation and runtime configuration', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hs_stellar154')!
    expect(selected.config).toMatchObject({ driver: 0x34, recipe: 1, color: 2,
      width: 200, height: 200, transform: 5 })
    expect(Array.from(encodeScreenConfig(selected.config)))
      .toEqual([1, 0x34, 1, 2, 5, 200, 0, 200, 0, 0, 0, 0, 0, 200, 0, 200, 0])
    expect(findMatchingScreenProfile(builtInScreenProfiles, selected.config)?.id).toBe('hs_stellar154')
    expect(findScreenProfileForDevice(builtInScreenProfiles, {...selected.config, transform:0})?.id).toBe('hs_stellar154')
    expect(screenProfileConfigForDevice(selected, null).transform).toBe(5)
    expect(screenProfileTitle(selected)).toBe('JD79653A 1.54寸 200x200 三色 （适配HSEL5_01_54M_00）')
    expect(builtInScreenProfiles.filter(p => p.id === 'hs_stellar154')).toHaveLength(1)
  })
  it('keeps the verified Nebular154 orientation aligned with Full and CLI defaults', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hs_nebularpro154q')!
    expect(selected.config).toMatchObject({ driver: 0x3f, recipe: 2, color: 3,
      width: 200, height: 200, transform: 5 })
    expect(validateScreenConfig(selected.config)).toBe(true)
    expect(encodeScreenConfig(selected.config)[4]).toBe(5)
    expect(findMatchingScreenProfile(builtInScreenProfiles, selected.config)?.id)
      .toBe('hs_nebularpro154q')
    expect(findScreenProfileForDevice(builtInScreenProfiles, { ...selected.config, transform: 0 })?.id)
      .toBe('hs_nebularpro154q')
    expect(findScreenProfileForDevice(builtInScreenProfiles, { ...selected.config, recipe: 99 }))
      .toBeUndefined()
  })
  it('names new panels by controller and model without experimental suffixes', () => {
    const ids = ['osprey_154_e6', 'osprey_169_e6', 'osprey_370_e6',
      'hs_nebularpro154q', 'hs_hsel3_266q', 'hs_stellarpro154q', 'ssd1681_p154030_bwr', 'hs_stellar154']
    for (const id of ids) {
      const selected = builtInScreenProfiles.find(p => p.id === id)!
      expect(selected).toBeDefined()
      expect(`${selected.name} ${screenProfileTitle(selected)} ${screenProfileNote(selected)}`)
        .not.toMatch(/experimental|实验|软件候选/i)
      expect(screenProfileTitle(selected)).toMatch(/IST|JD|SSD/)
      expect(selected.model || selected.device).toBeTruthy()
    }
  })
  it('matches the DA original horizontal panel only on its qualified board and retains the generic vertical profile', () => {
    const original = builtInScreenProfiles.find((p) => p.id === 'da_asel29_e029a33_original')!
    const generic = builtInScreenProfiles.find((p) => p.id === 'generic_12')!
    expect(original.config).toMatchObject({ driver: 0x20, recipe: 0, transform: 1, color: 2, width: 128, height: 296 })
    expect(generic.config.transform).toBe(0)
    expect(findMatchingScreenProfile(builtInScreenProfiles, original.config)).toBe(original)
    expect(findMatchingScreenProfile(builtInScreenProfiles, generic.config)).toBe(generic)
    expect(screenProfileTitle(original)).toBe('SSD1619 2.9寸 128x296 三色 （适配asel29）')
    expect(screenProfileNote(original)).toBe('SSD1619 / 2.9寸 / 128x296 / BWR / STANDARD')
    expect(screenProfileTitle(original) + screenProfileNote(original)).not.toMatch(/兼容|IC 待确认/)
    expect(screenProfileNote(original)).not.toMatch(/E029|HINK|da14585|原屏验证/)
    expect(original.model).toBe('E029A33N8C380 / HINK-E029A33-A0')
    expect(original.qualifiedBoard).toBe('da14585_asel29')
    for (const board of ['aesl29_da', 'da14585_asel29', 'da14585_aesl29'])
      expect(screenProfileAllowedOnBoard(original, board)).toBe(true)
    for (const board of ['aesl42_da', 'unknown', 'nrf52832_priceetag', 'gr551x_zkc42vm'])
      expect(screenProfileAllowedOnBoard(original, board)).toBe(false)
    expect(screenProfileConfigForDevice(original, original.config)).toEqual(original.config)
    expect(findMatchingScreenProfile([original], { ...original.config, transform: 0 })).toBeUndefined()
    expect(findMatchingScreenProfile([original], { ...original.config, recipe: 1 })).toBeUndefined()
  })
  it('retires SES and its UC8151 alias only on nRF52811 while retaining other profiles', () => {
    expect(screenDriverAllowedOnBoard(0x11, ' AESL22_NRF ')).toBe(false)
    for (const board of ['nrf52832_priceetag', 'aesl22_da', 'gr5515_zkc42v', 'unknown'])
      expect(screenDriverAllowedOnBoard(0x11, board)).toBe(true)
    for (const driver of [0x10, 0x13, 0x20, 0x31])
      expect(screenDriverAllowedOnBoard(driver, 'aesl22_nrf')).toBe(true)
    const retained = builtInScreenProfiles.filter((p) => screenDriverAllowedOnBoard(p.config.driver, 'aesl22_nrf'))
    expect(retained.some((p) => p.config.driver === 0x11)).toBe(false)
    expect(retained.some((p) => p.id === 'huawei_398_a0')).toBe(true)
    expect(retained.some((p) => p.id === 'huawei_398_a1')).toBe(true)
  })
  it('keeps distinct panel protocols and prefers named presets over orientation-only duplicates', () => {
    expect(genericScreenProfiles).toHaveLength(37)
    expect(genericScreenProfiles.some((profile) => profile.id === 'generic_14')).toBe(true)
    expect(genericScreenProfiles.some((profile) => profile.id === 'generic_15')).toBe(true)
    expect(genericScreenProfiles.some((profile) => profile.id === 'generic_35')).toBe(true)
    expect(genericScreenProfiles.some((profile) => profile.id === 'generic_36')).toBe(true)
    expect(builtInScreenProfiles).toHaveLength(58)
    const candidates = new Set(['osprey_154_e6','osprey_169_e6','osprey_370_e6',
      'hs_nebularpro154q','hs_hsel3_266q','hs_stellarpro154q','ssd1681_p154030_bwr','hs_stellar154','hsel5_01_54m_02_bwr'])
    expect(builtInScreenProfiles.filter(p => !candidates.has(p.id))).toHaveLength(49)
    expect(builtInScreenProfiles.filter(p => candidates.has(p.id))).toHaveLength(9)
    expect(new Set(builtInScreenProfiles.map((profile) => JSON.stringify(profile.config))).size)
      .toBe(builtInScreenProfiles.length)
    expect(builtInScreenProfiles.every((profile) => profile.tag === 'preset' && profile.group === 'preset')).toBe(true)
    expect(builtInScreenProfiles.every((profile) => profile.name.trim().length > 0)).toBe(true)
    expect(builtInScreenProfiles.every((profile) => validateScreenConfig(profile.config))).toBe(true)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'huawei_398_a0')).toBe(true)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'huawei_398_a1')).toBe(true)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'lenmory_h364f067_e6')).toBe(true)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'hs_nowa154')).toBe(true)
    expect(builtInScreenProfiles.find((profile) => profile.id === 'hs_nowa154')?.config.transform).toBe(5)
    expect(builtInScreenProfiles.find((profile) => profile.id === 'hs_nebular213')?.config).toMatchObject({
      driver: 0x3f, recipe: 1, color: 3, transform: 5,
      width: 128, height: 250, visibleWidth: 122, visibleHeight: 250,
    })
    expect(builtInScreenProfiles.find((profile) => profile.id === 'ses_se2213fs093')?.config.transform).toBe(1)
    expect(builtInScreenProfiles.find((profile) => profile.id === 'ses_se2266fs093')?.config.transform).toBe(1)
    expect(builtInScreenProfiles.find((profile) => profile.id === 'huawei_398_a0')?.config.transform).toBe(2)
    expect(builtInScreenProfiles.find((profile) => profile.id === 'huawei_398_a1')?.config.transform).toBe(2)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_14')).toBe(false)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_15')).toBe(false)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_35')).toBe(false)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_36')).toBe(false)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_34')).toBe(false)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_1c')).toBe(false)
    expect(builtInScreenProfiles.some((profile) => profile.id === 'generic_02')).toBe(true)
    expect(builtInScreenProfiles.filter((profile) => profile.config.driver === 0x30 && profile.config.recipe === 1).map((profile) => profile.id)).toEqual(['zkc21b_original'])
    expect(builtInScreenProfiles.filter((profile) => profile.config.driver === 0x33 && profile.config.recipe === 2).map((profile) => profile.id)).toEqual(['zkc42b_original'])
    expect(builtInScreenProfiles.filter((profile) => profile.config.driver === 0x33 && profile.config.recipe === 1).map((profile) => profile.id)).toEqual(['hs_nebular420'])
    expect(builtInScreenProfiles.find((profile) => profile.id === 'hs_nebular420')?.config.color).toBe(3)
    expect(builtInScreenProfiles.filter((profile) => profile.config.driver === 0x15).map((profile) => profile.id)).toEqual(['uc8276_standard'])
  })

  it('keeps StellarPro profile8 separate from old small-Q and BWR Stellar panels', () => {
    const selected = builtInScreenProfiles.find(p => p.id === 'hs_stellarpro154q')!
    expect(selected.config).toMatchObject({ driver: 0x3f, recipe: 4, color: 3,
      width: 200, height: 200, visibleWidth: 200, visibleHeight: 200 })
    expect(selected.device).toBe('HSEL5Q_01_54M_00')
    expect(findMatchingScreenProfile(builtInScreenProfiles, selected.config)?.id).toBe(selected.id)
    expect(screenConfigsEqual(selected.config, builtInScreenProfiles.find(p => p.id === 'hs_nebularpro154q')!.config)).toBe(false)
    expect(screenConfigsEqual(selected.config, builtInScreenProfiles.find(p => p.id === 'hs_stellar154')!.config)).toBe(false)
    expect(validateScreenConfig({ ...selected.config, width: 196, visibleWidth: 196 })).toBe(false)
    expect(validateScreenConfig({ ...selected.config, color: 2 })).toBe(false)
  })

  it('uses one controller-first title and keeps protocol parameters in the secondary line', () => {
    const profile = builtInScreenProfiles.find((item) => item.id === 'gdeq0426t82')!
    expect(screenProfileTitle(profile)).toBe('SSD1677 4.26寸 800x480 四灰阶')
    expect(screenProfileNote(profile)).toBe('SSD1677 / 4.26寸 / 800x480 / GRAY4 / STANDARD')
    const huawei = builtInScreenProfiles.find((item) => item.id === 'huawei_398_a0')!
    expect(screenProfileTitle(huawei)).toBe('JD79665AA 3.98寸 768x552 四色 （适配huawei_a0）')
  })

  it('matches a device-decoded config to its preset regardless of property order', () => {
    const config = {
      schemaVersion: 1, driver: 0x31, recipe: 1, color: 3, transform: 2, nativeUiScale: 2,
      width: 768, height: 552, visibleX: 0, visibleY: 0, visibleWidth: 768, visibleHeight: 552,
    }
    expect(findMatchingScreenProfile(builtInScreenProfiles, config)?.id).toBe('huawei_398_a0')
    expect(validateScreenConfig({ ...config, schemaVersion: 2 })).toBe(false)
    expect(findMatchingScreenProfile(builtInScreenProfiles, { ...config, schemaVersion: 2 })).toBeUndefined()
  })

  it('defaults every high-PPI native page profile to 150% text', () => {
    for (const id of ['huawei_398_a0', 'huawei_398_a1', 'lenmory_h364f067_e6']) {
      expect(builtInScreenProfiles.find((profile) => profile.id === id)?.config.nativeUiScale).toBe(2)
    }
  })

  it('gives unnamed generic profiles a deterministic controller-first name', () => {
    const config = makeScreenConfig({
      driver: 0x13, recipe: 0, color: 2, transform: 0,
      width: 400, height: 300, visibleX: 0, visibleY: 0,
      visibleWidth: 400, visibleHeight: 300,
    })
    expect(defaultProfileName(config)).toBe('UC8176_400x300_BWR_STANDARD')
  })

  it('does not invent board adaptations for recipes using the standard driver', () => {
    const profile = builtInScreenProfiles.find((item) => item.id === 'zkc21v_original')!
    expect(screenProfileTitle(profile)).toBe('SSD1680 2.13寸 128x250 三色')
    expect(screenProfileNote(profile)).toBe('SSD1680 / 2.13寸 / 128x250 / BWR / ZKC21V_FACTORY')
    expect(profile.config.transform).toBe(9)
    const uc8276 = builtInScreenProfiles.find((item) => item.id === 'uc8276_standard')!
    expect(screenProfileTitle(uc8276)).toBe('UC8276 4.2寸 400x300 三色')
    expect(screenProfileNote(uc8276)).toBe('UC8276 / 4.2寸 / 400x300 / BWR / STANDARD')
    expect(screenRecipeDisplayName(0x15, 1)).toBe('标准驱动预设 · ZKC42V_HE042AD7A2')
    const zkc42vm = builtInScreenProfiles.find((item) => item.id === 'zkc42vm_original')!
    expect(screenProfileTitle(zkc42vm)).toBe('SSD1683 4.2寸 400x300 三色')
    expect(zkc42vm.config.recipe).toBe(1)
    expect(screenRecipeDisplayName(0x24, 1)).toBe('标准驱动预设 · ZKC42VM_FACTORY')
  })

  it('uses lowercase adaptation labels only for distinct command/scan paths', () => {
    const expected = {
      hs_nowa154: 'SSD1675 1.54寸 152x152 三色 （适配nowa154r）',
      hs_nebular420: 'JD79668AA 4.2寸 400x300 四色 （适配nebular420r）',
      hs_nebular213: 'JD79676 2.13寸 128x250 四色 （适配nebular213r）',
      zkc21b_original: 'JD79661 2.13寸 128x250 四色 （适配zkc21b）',
      zkc42b_original: 'JD79668AA 4.2寸 400x300 四色 （适配zkc42b）',
      huawei_398_a1: 'JD79665AA 3.98寸 768x552 四色 （适配huawei_a1）',
      lenmory_h364f067_e6: 'JD79665CA 3.64寸 760x568 KWRYGB （适配lenmory_h364f067_e6）',
    }
    for (const [id, title] of Object.entries(expected)) {
      const profile = builtInScreenProfiles.find((item) => item.id === id)!
      expect(screenProfileTitle(profile)).toBe(title)
    }
    // Actual wire values remain distinct even when two human titles match.
    const wire = (id: string) => Array.from(encodeScreenConfig(
      builtInScreenProfiles.find((item) => item.id === id)!.config,
    ), (byte) => byte.toString(16).padStart(2, '0')).join('')
    expect(wire('zkc42vm_original')).toBe('012401020090012c010000000090012c01')
    expect(wire('generic_33')).toBe('012400020090012c010000000090012c01')
    expect(wire('hs_nowa154')).toBe('0121010205980098000000000098009800')
    expect(wire('hs_nebular213')).toBe('013f0103058000fa00000000007a00fa00')
    expect(screenRecipeDisplayName(0x21, 1)).toBe('适配nowa154r')
    expect(screenRecipeDisplayName(0x20, 1)).toBe('适配hema213')
    expect(screenRecipeDisplayName(0x10, 1)).toBe('适配nowa213r')
    expect(screenRecipeDisplayName(0x24, 99)).toBe('方案 RECIPE_99')
    expect(builtInScreenProfiles.every((profile) => !screenProfileTitle(profile).includes('XXX'))).toBe(true)
  })

  it('sorts by size, resolution, controller, color and recipe', () => {
    expect(builtInScreenProfiles.map((profile) => profile.id)).toEqual(
      [...builtInScreenProfiles].sort(compareScreenProfiles).map((profile) => profile.id),
    )
    const sample = (id: string, size: string, width: number, height: number, driver: number, color: number, recipe = 0): ScreenProfile => ({
      id, tag: 'preset', group: 'preset', name: id, size,
      config: makeScreenConfig({ driver, recipe, color, transform: 0, width, height, visibleX: 0, visibleY: 0, visibleWidth: width, visibleHeight: height }),
    })
    expect(sortScreenProfiles([
      sample('larger-inch', '4.2', 128, 250, 0x10, 1),
      sample('uc-small', '2.13', 104, 212, 0x10, 1),
      sample('jd-small', '2.13', 104, 212, 0x30, 1),
      sample('uc-medium', '2.13', 400, 300, 0x10, 1),
      sample('ssd', '2.13', 400, 300, 0x20, 1),
      sample('jd-recipe-2', '2.13', 800, 480, 0x30, 1, 2),
      sample('jd-recipe-1', '2.13', 800, 480, 0x30, 1, 1),
      sample('jd-six', '2.13', 800, 480, 0x30, 10),
      sample('jd-bwry', '2.13', 800, 480, 0x30, 3),
      sample('jd-bwr', '2.13', 800, 480, 0x30, 2),
      sample('jd-bw', '2.13', 800, 480, 0x30, 1),
    ]).map((profile) => profile.id)).toEqual([
      'uc-small', 'jd-small', 'uc-medium', 'ssd', 'jd-bw', 'jd-recipe-1', 'jd-recipe-2', 'jd-bwr', 'jd-bwry', 'jd-six', 'larger-inch',
    ])
  })

  it('appends a user remark to the generated title without deduplicating saved profiles', () => {
    const base = builtInScreenProfiles.find((item) => item.id === 'uc8276_standard')!
    const user: ScreenProfile = { ...base, id: 'user-1', tag: 'user', group: 'user', name: '客厅屏' }
    expect(screenProfileTitle(user)).toBe('UC8276 4.2寸 400x300 三色 客厅屏')
    expect(dedupeScreenProfiles([base, user])).toEqual([base])
    expect(sortScreenProfiles([base, user])).toHaveLength(2)
  })

  it('groups presets into the three controller families', () => {
    expect(screenProfileFamily(builtInScreenProfiles.find((item) => item.config.driver === 0x13)!)).toBe('uc8xxx')
    expect(screenProfileFamily(builtInScreenProfiles.find((item) => item.config.driver === 0x24)!)).toBe('ssd1xxx')
    expect(screenProfileFamily(builtInScreenProfiles.find((item) => item.config.driver === 0x31)!)).toBe('jd79xxx')
    expect(screenProfileFamilyLabels.uc8xxx).toBe('UC 系列')
    expect(screenProfileFamilyLabels.ssd1xxx).toBe('SSD 系列')
    expect(screenProfileFamilyLabels.jd79xxx).toBe('JD 系列')
  })

  it('keeps SES geometry and protocol identity without a spurious adaptation suffix', () => {
    const ses = builtInScreenProfiles.find((item) => item.id === 'ses_se2266fs093')!
    expect(screenProfileTitle(ses)).toBe('UC8151 2.66寸 152x296 三色')
    expect(screenProfileNote(ses)).toBe('UC8151 / 2.66寸 / 152x296 / BWR / SES_SE2266FS093')
  })

  it('resolves retired Hanshow saved preset IDs to canonical profiles', () => {
    expect(findScreenProfileById(builtInScreenProfiles, 'hs_nowa154')?.id).toBe('hs_nowa154')
    expect(findScreenProfileById(builtInScreenProfiles, 'hanshow_nowa_154r_n')).toBeUndefined()
  })

  it('renames the P154 catalog entry without duplicating it or replacing user records', () => {
    const selected = findScreenProfileById(builtInScreenProfiles, 'ssd1681_p154030_bwr')!
    expect(findScreenProfileById(builtInScreenProfiles, 'hs_old200_bwr')).toBe(selected)
    expect(builtInScreenProfiles.some(p => p.id === 'hs_old200_bwr')).toBe(false)
    expect(`${selected.name} ${screenProfileTitle(selected)} ${screenProfileNote(selected)}`)
      .not.toMatch(/old200|SSD1 系列|兼容|compatible/i)
    const saved: ScreenProfile = { ...selected, id: 'hs_old200_bwr', tag: 'user', group: 'user',
      name: 'My panel', config: { ...selected.config, transform: 0 } }
    expect(findScreenProfileById([selected, saved], saved.id)).toBe(saved)
    expect(saved.config.transform).toBe(0)
  })

  it('merges only the known ZKC42V display preset without weakening recipe or image validation', () => {
    const standard = findScreenProfileById(builtInScreenProfiles, 'uc8276_standard')!
    const installed = { ...standard.config, recipe: 1 }
    expect(validateScreenConfig(installed)).toBe(true)
    expect(findMatchingScreenProfile(builtInScreenProfiles, installed)).toBe(standard)
    expect(screenConfigsEqual(installed, standard.config)).toBe(false)
    expect(imageStoreCompatibilityChanged(installed, standard.config)).toBe(true)
    expect(Array.from(encodeScreenConfig(installed), (byte) => byte.toString(16).padStart(2, '0')).join(''))
      .toBe('011501020090012c010000000090012c01')
    for (const altered of [
      { ...installed, driver: 0x13 }, { ...installed, recipe: 2 },
      { ...installed, color: 1 }, { ...installed, transform: 4 },
      { ...installed, nativeUiScale: 1 }, { ...installed, width: 408 },
      { ...installed, height: 304 }, { ...installed, visibleX: 1 },
      { ...installed, visibleY: 1 }, { ...installed, visibleWidth: 392 },
      { ...installed, visibleHeight: 296 }, { ...installed, schemaVersion: 2 },
    ]) expect(findMatchingScreenProfile([standard], altered)).toBeUndefined()
    expect(validateScreenConfig({ ...installed, recipe: 2 })).toBe(false)
    const user: ScreenProfile = { ...standard, id: 'saved-user', tag: 'user', group: 'user', config: installed }
    expect(findMatchingScreenProfile([standard, user], installed)).toBe(user)
  })

  it('preserves an installed ZKC42V recipe on reselection and aliases old catalog IDs only', () => {
    const standard = findScreenProfileById(builtInScreenProfiles, 'uc8276_standard')!
    for (const id of ['zkc42v_original', 'nrf_aesl_420_test'])
      expect(findScreenProfileById(builtInScreenProfiles, id)).toBe(standard)
    const installed = { ...standard.config, recipe: 1 }
    const selected = screenProfileConfigForDevice(standard, installed)
    expect(selected).toEqual(installed)
    expect(selected).not.toBe(installed)
    expect(imageStoreCompatibilityChanged(installed, selected)).toBe(false)
    expect(screenProfileConfigForDevice(standard, null)).toEqual(standard.config)
    // Reselecting the displayed current preset must not reset orientation.
    expect(screenProfileConfigForDevice(standard, { ...installed, transform: 4 })).toEqual({ ...installed, transform: 4 })
    const other = builtInScreenProfiles.find((profile) => profile.id === 'zkc42vm_original')!
    expect(screenProfileConfigForDevice(other, installed)).toEqual(other.config)
    expect(standard.config.recipe).toBe(0)
    expect(installed.recipe).toBe(1)
    const savedAlias: ScreenProfile = { ...standard, id: 'zkc42v_original', tag: 'user', group: 'user', config: installed }
    expect(findScreenProfileById([standard, savedAlias], savedAlias.id)).toBe(savedAlias)
  })
})
