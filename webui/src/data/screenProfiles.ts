import type { ScreenConfig } from '../types'
import { colorCode, controllerDisplayName, driverName, makeScreenConfig, recipeName, screenConfigsEqual } from '../protocol/screen'
import { boardDisplayName, canonicalBoardId } from '../protocol/boards'

/** The constrained 52811 build retires UC8151's SES profiles and alias.
 * This is a board-specific firmware boundary; the shared catalog stays intact. */
export function screenDriverAllowedOnBoard(driver: number, boardId: string): boolean {
  return canonicalBoardId(boardId) !== 'aesl22_nrf' || driver !== 0x11
}

export interface ScreenProfile {
  id: string
  tag: 'preset' | 'user'
  group: 'preset' | 'user'
  name: string
  model?: string
  device?: string
  qualifiedBoard?: string
  controllerLabel?: string
  adaptationLabel?: string
  size: string
  config: ScreenConfig
  busyActiveHigh?: boolean
}

function profile(
  id: string, name: string, size: string, width: number, height: number,
  driver: number, color: number, options: {
    model?: string
    device?: string
    qualifiedBoard?: string
    controllerLabel?: string
    adaptationLabel?: string
    transform?: number
    nativeUiScale?: number
    visibleWidth?: number
    visibleHeight?: number
    recipe?: number
  } = {},
): ScreenProfile {
  const config = makeScreenConfig({
    driver, recipe: options.recipe ?? 0, color, transform: options.transform || 0,
    nativeUiScale: options.nativeUiScale ?? 0, width, height, visibleX: 0, visibleY: 0,
    visibleWidth: options.visibleWidth ?? width, visibleHeight: options.visibleHeight ?? height,
  })
  return { id, tag: 'preset', group: 'preset', name: name || defaultProfileName(config), model: options.model, device: options.device, qualifiedBoard: options.qualifiedBoard, controllerLabel: options.controllerLabel, adaptationLabel: options.adaptationLabel, size, config }
}

export function screenProfileAllowedOnBoard(profile: ScreenProfile, boardId: string) {
  return screenDriverAllowedOnBoard(profile.config.driver, boardId) &&
    (!profile.qualifiedBoard || boardDisplayName(boardId) === profile.qualifiedBoard)
}

export type ScreenProfileFamily = 'uc8xxx' | 'ssd1xxx' | 'jd79xxx' | 'ist7xxx' | 'other'

export const screenProfileFamilyLabels: Record<ScreenProfileFamily, string> = {
  uc8xxx: 'UC 系列',
  ssd1xxx: 'SSD 系列',
  jd79xxx: 'JD 系列',
  ist7xxx: 'IST 系列（E6）',
  other: '其他控制器',
}

export function screenProfileFamily(profile: ScreenProfile): ScreenProfileFamily {
  const driver = profile.config.driver
  if (driver >= 0x10 && driver <= 0x1f) return 'uc8xxx'
  if (driver >= 0x20 && driver <= 0x2f) return 'ssd1xxx'
  if (driver >= 0x30 && driver <= 0x3f) return 'jd79xxx'
  if (driver === 0x40 || driver === 0x41) return 'ist7xxx'
  return 'other'
}

export const deviceScreenProfiles: ScreenProfile[] = [
  profile('osprey_154_e6', '鱼鹰 1.54 寸 E6', '1.54', 240, 240, 0x40, 10, {
    recipe: 1, model: 'HINK-T0154A10-A1', device: 'Osptek', adaptationLabel: 'HT0154A10',
  }),
  profile('osprey_169_e6', '鱼鹰 1.69 寸圆形 E6', '1.69', 400, 400, 0x41, 10, {
    recipe: 1, model: 'HINK-T0169A06-A0', device: 'Osptek', adaptationLabel: 'HT0169A06 圆屏',
  }),
  profile('osprey_370_e6', '鱼鹰 3.7 寸 E6', '3.7', 720, 480, 0x41, 10, {
    recipe: 2, model: 'HINK-T037A02-A1', device: 'Osptek', controllerLabel: 'IST7601/JD7601',
    adaptationLabel: 'HT037A02',
  }),
  profile('hs_nebularpro154q', 'Hanshow Nebular Pro 1.54 寸四色屏', '1.54', 200, 200, 0x3f, 3, {
    recipe: 2, transform: 5, device: 'HSEL4Q_01_54M_31', controllerLabel: 'JD79 系列', adaptationLabel: 'HSEL4Q_01_54M_31',
  }),
  profile('hs_hsel3_266q', 'Hanshow 2.66 寸四色屏', '2.66', 152, 296, 0x3f, 3, {
    recipe: 3, transform: 5, device: 'HSEL3_02_66M_36', controllerLabel: 'JD79 系列', adaptationLabel: 'HSEL3_02_66M_36',
  }),
  profile('hs_stellarpro154q', 'Hanshow StellarPro 1.54 寸四色屏', '1.54', 200, 200, 0x3f, 3, {
    recipe: 4, model: 'HS-E1.54LE-V3.0', device: 'HSEL5Q_01_54M_00', controllerLabel: 'JD79 系列', adaptationLabel: 'HSEL5Q_01_54M_00',
  }),
  profile('ssd1681_p154030_bwr', 'SSD1681 1.54 寸三色屏', '1.54', 200, 200, 0x2f, 2, {
    recipe: 1, transform: 5, model: 'P154030', device: 'HS_EL5101_5M_60_04', controllerLabel: 'SSD1681', adaptationLabel: 'HS_EL5101_5M_60_04',
  }),
  profile('hs_stellar154', 'Hanshow Stellar 1.54 寸三色屏', '1.54', 200, 200, 0x34, 2, {
    recipe: 1, transform: 5, model: 'DEPG0154RWF653CF', device: 'HSEL5_01_54M_00', adaptationLabel: 'HSEL5_01_54M_00',
  }),
  // Product-facing SSD1681 inference; exact die is unverified, native OTP stays recipe2.
  profile('hsel5_01_54m_02_bwr', 'SSD1681 1.54 寸三色屏', '1.54', 200, 200, 0x2f, 2, {
    recipe: 2, transform: 5, device: 'HSEL5_01_54M_02', controllerLabel: 'SSD1681', adaptationLabel: 'HSEL5_01_54M_02',
  }),
  // Label the proven SSD1619 driver; physical controller markings remain unconfirmed.
  profile('da_asel29_e029a33_original', 'E029A33N8C380 原屏', '2.9', 128, 296, 0x20, 2, {
    transform: 1, model: 'E029A33N8C380 / HINK-E029A33-A0', device: 'da14585_asel29',
    qualifiedBoard: 'da14585_asel29', controllerLabel: 'SSD1619', adaptationLabel: 'asel29',
  }),
  profile('gdeq0426t82', 'GDEQ0426T82 4.26 寸四灰阶', '4.26', 800, 480, 0x22, 4, { model: 'GDEQ0426T82' }),
  profile('hema_213_bwr', '盒马 2.13 寸三色屏', '2.13', 128, 250, 0x20, 2, {
    recipe: 1, transform: 3, visibleWidth: 122, device: '盒马 AESL EL022H2',
    controllerLabel: 'SSD1619', adaptationLabel: '盒马213',
  }),
  profile('zkc21v_original', 'ZKC21V 2.13 寸原屏', '2.13', 128, 250, 0x23, 2, { recipe: 1, model: 'KEGM021321T01-S822', device: 'ZKC21V', transform: 9, visibleWidth: 122 }),
  profile('zkc21b_original', 'ZKC21B 2.13 寸四色原屏', '2.13', 128, 250, 0x30, 3, { recipe: 1, model: 'HINK-E0213A349', device: 'ZKC21B', transform: 9, visibleWidth: 122 }),
  profile('zkc42b_original', 'ZKC42B 4.2 寸四色原屏', '4.2', 400, 300, 0x33, 3, { recipe: 2, device: 'ZKC42B 原屏', transform: 6 }),
  profile('hs_nebular420', 'Hanshow Nebular 4.2 寸四色屏', '4.2', 400, 300, 0x33, 3, { recipe: 1, model: 'AN0420A300-400BJ668-L04', device: 'Hanshow Nebular-420R-N', transform: 8 }),
  profile('hs_nebular213', 'Hanshow Nebular 2.13 寸四色屏', '2.13', 128, 250, 0x3f, 3, { recipe: 1, model: 'AN0213A250-122KS682', device: 'Hanshow Nebular-213R-N', transform: 5, visibleWidth: 122 }),
  profile('hsela_an0437a480', 'Hanshow 4.37 寸四色屏', '4.37', 176, 480, 0x3f, 3, { recipe: 5, model: 'AN0437A480', device: 'HSELAQ_04_37MG_38', transform: 5 }),
  profile('zkc42vm_original', 'ZKC42VM GR5513 SSD1683 4.2 寸原屏', '4.2', 400, 300, 0x24, 2, { recipe: 1, model: 'HINK-E042A13-FPC-A0 / HE042A55', device: 'ZKC42VM GR5513' }),
  profile('zkc42vm_gr5515_ssd1619', 'ZKC42VM GR5515 SSD1619 4.2 寸原屏', '4.2', 400, 300, 0x20, 2, { model: 'FPC-194', device: 'ZKC42VM GR5515' }),
  profile('ses_se2213fs093', 'SES 2.13 寸三色屏', '2.13', 104, 212, 0x11, 2, { recipe: 1, model: 'SE2213FS093 / A1360067-00', device: 'SES-imagotag', transform: 1 }),
  profile('ses_se2266fs093', 'SES 2.66 寸三色屏', '2.66', 152, 296, 0x11, 2, { recipe: 2, model: 'SE2266FS093 / A1360061-00', device: 'SES-imagotag', transform: 1 }),
  profile('hs_nowa154', 'Hanshow Nowa 1.54 寸三色屏', '1.54', 152, 152, 0x21, 2, { recipe: 1, device: 'Hanshow Nowa-154R-N', transform: 5 }),
  profile('huawei_398_a0', '华为 3.98 寸四色 A0', '3.98', 768, 552, 0x31, 3, { recipe: 1, model: 'JD79665AA A0', transform: 2, nativeUiScale: 2 }),
  profile('huawei_398_a1', '华为 3.98 寸四色 A1', '3.98', 768, 552, 0x31, 3, { recipe: 2, model: 'JD79665AA A1', transform: 2, nativeUiScale: 2 }),
  profile('lenmory_h364f067_e6', '图公主 3.64 寸六色 E6', '3.64', 760, 568, 0x32, 10, { recipe: 1, model: 'H364F067', device: 'Lenmory', nativeUiScale: 2 }),
  profile('uc8276_standard', 'UC8276 4.2 寸三色屏', '4.2', 400, 300, 0x15, 2),
  profile('pricetag_152_recovered', 'PriceETag 1.54 寸黑白屏', '1.54', 152, 152, 0x21, 1, { device: 'PriceETag 恢复路径' }),
]

export function findScreenProfileById(profiles: ScreenProfile[], id: string) {
  const exact = profiles.find((item) => item.id === id)
  if (exact) return exact
  // Only catalog IDs are aliases; saved user records are not rewritten.
  if (id === 'hs_old200_bwr')
    return profiles.find((item) => item.id === 'ssd1681_p154030_bwr')
  if (id === 'zkc42v_original' || id === 'nrf_aesl_420_test')
    return profiles.find((item) => item.id === 'uc8276_standard')
}

// Generic current-protocol profiles complete the controller/geometry catalog. They are
// selectable current configurations, not compatibility aliases for old saved records.
const generic = [
  ['generic_1c', '1.54', 152, 152, 0x21, 1], ['generic_17', '1.54', 200, 200, 0x13, 2],
  ['generic_19', '2.13', 104, 212, 0x11, 2], ['generic_0e', '2.13', 128, 250, 0x20, 1],
  ['generic_0f', '2.13', 128, 250, 0x20, 2], ['generic_35', '2.13', 128, 250, 0x30, 3, { visibleWidth: 122, recipe: 1 }],
  ['generic_1b', '2.13', 128, 250, 0x23, 1, { visibleWidth: 122 }], ['generic_1a', '2.13', 128, 250, 0x23, 2, { visibleWidth: 122 }],
  ['generic_1d', '2.13', 128, 250, 0x13, 1, { visibleWidth: 122 }], ['generic_18', '2.66', 152, 296, 0x11, 2],
  ['generic_13', '2.8', 152, 296, 0x33, 3], ['generic_11', '2.9', 128, 296, 0x20, 1],
  ['generic_12', '2.9', 128, 296, 0x20, 2], ['generic_10', '3.1', 300, 300, 0x33, 3],
  ['generic_14', '3.98', 768, 552, 0x31, 3, { recipe: 1 }], ['generic_15', '3.98', 768, 552, 0x31, 3, { recipe: 2 }],
  ['generic_01', '4.2', 400, 300, 0x13, 1], ['generic_02', '4.2', 400, 300, 0x13, 2],
  ['generic_03', '4.2', 400, 300, 0x20, 1], ['generic_04', '4.2', 400, 300, 0x20, 2],
  ['generic_05', '4.2', 400, 300, 0x33, 3], ['generic_32', '4.2', 400, 300, 0x24, 1],
  ['generic_33', '4.2', 400, 300, 0x24, 2], ['generic_36', '4.2', 400, 300, 0x33, 3, { recipe: 2 }],
  ['generic_34', '4.26', 800, 480, 0x22, 4], ['generic_21', '5.83', 600, 448, 0x12, 1],
  ['generic_20', '5.83', 600, 448, 0x12, 2], ['generic_23', '5.83', 648, 480, 0x14, 1],
  ['generic_22', '5.83', 648, 480, 0x14, 2], ['generic_0d', '5.83', 648, 480, 0x31, 3],
  ['generic_06', '7.5', 800, 480, 0x14, 1], ['generic_07', '7.5', 800, 480, 0x14, 2],
  ['generic_0c', '7.5', 800, 480, 0x31, 3], ['generic_08', '7.5', 640, 384, 0x12, 1],
  ['generic_09', '7.5', 640, 384, 0x12, 2], ['generic_0a', '7.5', 880, 528, 0x22, 1],
  ['generic_0b', '7.5', 880, 528, 0x22, 2],
] as const

export const genericScreenProfiles = generic.map(([id, size, width, height, driver, color, genericOptions]) => {
  const options = (genericOptions || {}) as Record<string, number>
  const config = makeScreenConfig({
    driver, recipe: options.recipe ?? 0, color, transform: options.transform || 0, width, height, visibleX: 0, visibleY: 0,
    visibleWidth: options.visibleWidth ?? width, visibleHeight: options.visibleHeight ?? height,
  })
  return profile(id, defaultProfileName(config), size, width, height, driver, color, options)
})

const colorSortOrder = new Map(
  [1, 2, 3, 10, 11, 5, 6, 7, 8, 4, 9].map((color, index) => [color, index]),
)

const colorDisplayNames = new Map<number, string>([
  [1, '黑白'], [2, '三色'], [3, '四色'], [4, '四灰阶'], [5, '三色'],
  [6, '三色'], [7, '三色'], [8, '三色'], [9, '十六灰阶'], [10, 'KWRYGB'], [11, '七色'],
])

const colorProtocolCodes = new Map<number, string>([
  [1, 'BW'], [2, 'BWR'], [3, 'BWRY'], [4, 'GRAY4'], [5, 'BWY'],
  [6, 'BWB'], [7, 'BWG'], [8, 'BWO'], [9, 'GRAY16'], [10, 'KWRYGB'], [11, 'BWRYGBO'],
])

// Empty labels are registered presets whose current common driver has no
// recipe-specific commands. Keep their wire IDs/transforms; do not manufacture
// an electrical adaptation from the source board's name.
const recipeAdaptations: Record<string, string> = {
  STANDARD: '',
  SES_SE2213FS093: '',
  SES_SE2266FS093: '',
  ZKC21V_FACTORY: '',
  ZKC42VM_FACTORY: '',
  ZKC42V_FACTORY: '',
  ZKC42V_HE042AD7A2: '',
  HEMA_213_BWR: 'hema213',
  HANSHOW_NOWA_154R_N: 'nowa154r',
  HANSHOW_NOWA_213R_N: 'nowa213r',
  ZKC21B_FACTORY_B2: 'zkc21b',
  HUAWEI_398_A0: 'huawei_a0',
  HUAWEI_398_A1: 'huawei_a1',
  LENMORY_H364F067_E6: 'lenmory_h364f067_e6',
  HANSHOW_NEBULAR_420R_N: 'nebular420r',
  HANSHOW_NEBULAR_213R_N: 'nebular213r',
  ZKC42B_FACTORY_B3: 'zkc42b',
  HANSHOW_HSEL5_154M02_NATIVE_OTP: 'hsel5_01_54m_02',
}

export function screenRecipeDisplayName(driver: number, recipeId: number) {
  const recipe = recipeName(driver, recipeId)
  const adaptation = recipeAdaptations[recipe]
  if (recipe === 'STANDARD') return '标准方案'
  if (adaptation === '') return `标准驱动预设 · ${recipe}`
  if (adaptation) return `适配${adaptation}`
  return `方案 ${recipe}`
}

function screenConfigKey(config: ScreenConfig) {
  return [
    config.schemaVersion, config.driver, config.recipe, config.color,
    config.width, config.height, config.visibleX, config.visibleY,
    config.visibleWidth, config.visibleHeight,
  ].join(':')
}

export function dedupeScreenProfiles(profiles: ScreenProfile[]) {
  const seen = new Set<string>()
  return profiles.filter((item) => {
    // An exact-board preset must not remove the generic orientation on other boards.
    const key = `${screenConfigKey(item.config)}:${item.qualifiedBoard || ''}`
    if (seen.has(key)) return false
    seen.add(key)
    return true
  })
}

export function compareScreenProfiles(left: ScreenProfile, right: ScreenProfile) {
  const size = Number.parseFloat(left.size) - Number.parseFloat(right.size)
  if (size) return size
  const resolution = left.config.width * left.config.height - right.config.width * right.config.height
  if (resolution) return resolution
  if (left.config.width !== right.config.width) return left.config.width - right.config.width
  if (left.config.height !== right.config.height) return left.config.height - right.config.height
  const controller = left.config.driver - right.config.driver
  if (controller) return controller
  const colors = (colorSortOrder.get(left.config.color) ?? 100 + left.config.color) -
    (colorSortOrder.get(right.config.color) ?? 100 + right.config.color)
  if (colors) return colors
  const recipe = left.config.recipe - right.config.recipe
  if (recipe) return recipe
  if (left.config.transform !== right.config.transform) return left.config.transform - right.config.transform
  return `${left.model || ''}\u0000${left.device || ''}\u0000${left.name}`.localeCompare(`${right.model || ''}\u0000${right.device || ''}\u0000${right.name}`)
}

export function sortScreenProfiles(profiles: ScreenProfile[]) {
  return [...profiles].sort(compareScreenProfiles)
}

export function findMatchingScreenProfile(profiles: ScreenProfile[], config: ScreenConfig) {
  const exact = profiles.find((profile) => screenConfigsEqual(profile.config, config))
  if (exact) return exact
  // Display-only compatibility for the installed ZKC42V UC8276 preset.
  // Wire validation and image-store compatibility still distinguish recipes.
  if (config.driver === 0x15 && config.recipe === 1)
    return profiles.find((profile) => profile.id === 'uc8276_standard' &&
      screenConfigsEqual(profile.config, { ...config, recipe: 0 }))
}

export function screenProfileConfigForDevice(profile: ScreenProfile, current: ScreenConfig | null) {
  // Selecting the displayed default must not migrate an installed recipe or
  // make existing image slots incompatible. Different profiles use their own config.
  if (current && findScreenProfileForDevice([profile], current)) return { ...current }
  return { ...profile.config }
}

/** Match presentation without applying preset orientation or UI scale. */
export function findScreenProfileForDevice(profiles: ScreenProfile[], config: ScreenConfig) {
  return findMatchingScreenProfile(profiles, config) || profiles.find((profile) =>
    profile.tag === 'preset' && findMatchingScreenProfile([profile], {
      ...config, transform: profile.config.transform, nativeUiScale: profile.config.nativeUiScale,
    }))
}

export const builtInScreenProfiles = sortScreenProfiles(
  dedupeScreenProfiles([
    ...deviceScreenProfiles,
    ...genericScreenProfiles,
  ]),
)

function generatedProfileTitle(item: ScreenProfile) {
  const recipe = recipeName(item.config.driver, item.config.recipe)
  // An exact-board orientation preset is presentation metadata, not a new wire recipe.
  const adaptation = item.adaptationLabel ?? recipeAdaptations[recipe]
  const recipeNote = adaptation === '' ? '' : adaptation ? `（适配${adaptation}）` : `（方案 ${recipe}）`
  return [
    item.controllerLabel || controllerDisplayName(item.config.driver), `${item.size}寸`, `${item.config.width}x${item.config.height}`,
    colorDisplayNames.get(item.config.color) || colorCode(item.config.color), recipeNote,
  ].filter(Boolean).join(' ')
}

export function screenProfileTitle(item: ScreenProfile) {
  const generated = generatedProfileTitle(item)
  return item.tag === 'user' && item.name.trim() ? `${generated} ${item.name.trim()}` : generated
}

export function screenProfileNote(item: ScreenProfile) {
  return [
    item.controllerLabel || controllerDisplayName(item.config.driver), `${item.size}寸`, `${item.config.width}x${item.config.height}`,
    colorProtocolCodes.get(item.config.color) || String(item.config.color),
    recipeName(item.config.driver, item.config.recipe),
  ].join(' / ')
}

export function defaultProfileName(config: ScreenConfig) {
  return `${driverName(config.driver)}_${config.width}x${config.height}_${colorCode(config.color).replace(/\s+/g, '')}_${recipeName(config.driver, config.recipe)}`
}
