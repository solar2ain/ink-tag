import { colorCode, driverName, makeScreenConfig, recipeName, screenConfigsEqual, type ScreenConfig } from './screen-ui'

export interface ScreenProfile {
  id: string
  tag: 'preset' | 'user'
  group: 'preset' | 'user'
  name: string
  model?: string
  device?: string
  size: string
  config: ScreenConfig
}

function profile(
  id: string, name: string, size: string, width: number, height: number,
  driver: number, color: number, options: {
    model?: string
    device?: string
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
  return { id, tag: 'preset', group: 'preset', name: name || defaultProfileName(config), model: options.model, device: options.device, size, config }
}

export type ScreenProfileFamily = 'uc81xxx' | 'ssd16xxx' | 'jd79xxx' | 'other'

export const screenProfileFamilyLabels: Record<ScreenProfileFamily, string> = {
  uc81xxx: 'UC81XX 系列',
  ssd16xxx: 'SSD16XX 系列',
  jd79xxx: 'JD79XXX 系列',
  other: '其他控制器',
}

export function screenProfileFamily(profile: ScreenProfile): ScreenProfileFamily {
  const driver = profile.config.driver
  if (driver >= 0x10 && driver <= 0x1f) return 'uc81xxx'
  if (driver >= 0x20 && driver <= 0x2f) return 'ssd16xxx'
  if (driver >= 0x30 && driver <= 0x3f) return 'jd79xxx'
  return 'other'
}

export const deviceScreenProfiles: ScreenProfile[] = [
  profile('gdeq0426t82', 'GDEQ0426T82 4.26 寸四灰阶', '4.26', 800, 480, 0x22, 4, { model: 'GDEQ0426T82' }),
  profile('zkc21v_original', 'ZKC21V 2.13 寸原屏', '2.13', 128, 250, 0x23, 2, { recipe: 1, model: 'KEGM021321T01-S822', device: 'ZKC21V', transform: 9, visibleWidth: 122 }),
  profile('zkc21b_original', 'ZKC21B 2.13 寸四色原屏', '2.13', 128, 250, 0x30, 3, { recipe: 1, model: 'HINK-E0213A349', device: 'ZKC21B', transform: 9, visibleWidth: 122 }),
  profile('zkc42b_original', 'ZKC42B 4.2 寸四色原屏', '4.2', 400, 300, 0x33, 3, { recipe: 2, device: 'ZKC42B 原屏', transform: 6 }),
  profile('hs_nebular420', 'Hanshow Nebular 4.2 寸四色屏', '4.2', 400, 300, 0x33, 3, { recipe: 1, model: 'AN0420A300-400BJ668-L04', device: 'Hanshow Nebular-420R-N', transform: 8 }),
  profile('zkc42vm_original', 'ZKC42VM GR5513 SSD1683 4.2 寸原屏', '4.2', 400, 300, 0x24, 2, { recipe: 1, model: 'HINK-E042A13-FPC-A0 / HE042A55', device: 'ZKC42VM GR5513' }),
  profile('zkc42vm_gr5515_ssd1619', 'ZKC42VM GR5515 SSD1619 4.2 寸原屏', '4.2', 400, 300, 0x20, 2, { model: 'FPC-194', device: 'ZKC42VM GR5515' }),
  profile('zkc42v_original', 'ZKC42V 4.2 寸原屏', '4.2', 400, 300, 0x13, 2, { recipe: 1, model: 'HE042AD7A2', device: 'ZKC42V-N v1.2' }),
  profile('ses_se2213fs093', 'SES 2.13 寸三色屏', '2.13', 104, 212, 0x11, 2, { recipe: 1, model: 'SE2213FS093 / A1360067-00', device: 'SES-imagotag', transform: 1 }),
  profile('ses_se2266fs093', 'SES 2.66 寸三色屏', '2.66', 152, 296, 0x11, 2, { recipe: 2, model: 'SE2266FS093 / A1360061-00', device: 'SES-imagotag' }),
  profile('hs_nowa154', 'Hanshow Nowa 1.54 寸三色屏', '1.54', 152, 152, 0x21, 2, { recipe: 1, device: 'Hanshow Nowa-154R-N', transform: 5 }),
  profile('huawei_398_a0', '华为 3.98 寸四色 A0', '3.98', 768, 552, 0x31, 3, { recipe: 1, model: 'JD79665AA A0', transform: 2, nativeUiScale: 2 }),
  profile('huawei_398_a1', '华为 3.98 寸四色 A1', '3.98', 768, 552, 0x31, 3, { recipe: 2, model: 'JD79665AA A1', nativeUiScale: 2 }),
  profile('lenmory_h364f067_e6', '图公主 3.64 寸六色 E6', '3.64', 760, 568, 0x32, 10, { recipe: 1, model: 'H364F067', device: 'Lenmory', nativeUiScale: 2 }),
  profile('nrf_aesl_420_test', 'AESL 4.2 寸测试屏', '4.2', 400, 300, 0x13, 2, { model: 'FPC-194', device: 'nRF52811 AESL 测试屏' }),
  profile('pricetag_152_recovered', 'PriceETag 1.54 寸黑白屏', '1.54', 152, 152, 0x21, 1, { device: 'PriceETag 恢复路径' }),
]

export function findScreenProfileById(profiles: ScreenProfile[], id: string) {
  return profiles.find((item) => item.id === id)
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
  [6, '三色'], [7, '三色'], [8, '三色'], [9, '十六灰阶'], [10, '六色'], [11, '七色'],
])

const colorProtocolCodes = new Map<number, string>([
  [1, 'BW'], [2, 'BWR'], [3, 'BWRY'], [4, 'GRAY4'], [5, 'BWY'],
  [6, 'BWB'], [7, 'BWG'], [8, 'BWO'], [9, 'GRAY16'], [10, 'BWRYGB'], [11, 'BWRYGBO'],
])

const recipeDisplayNames: Record<string, string> = {
  SES_SE2213FS093: 'SES-imagotag',
  SES_SE2266FS093: 'SES-imagotag',
  HANSHOW_NOWA_154R_N: '汉朔 Nowa',
  ZKC21V_FACTORY: 'ZKC21V',
  ZKC42VM_FACTORY: 'ZKC42VM',
  ZKC42V_FACTORY: 'ZKC42V',
  ZKC21B_FACTORY_B2: 'ZKC21B',
  HUAWEI_398_A0: '华为A0',
  HUAWEI_398_A1: '华为A1',
  LENMORY_H364F067_E6: '图公主 E6',
  HANSHOW_NEBULAR_420R_N: '汉朔 Nebular',
  ZKC42B_FACTORY_B3: 'ZKC42B',
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
    const key = screenConfigKey(item.config)
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
  return profiles.find((profile) => screenConfigsEqual(profile.config, config))
}

export const builtInScreenProfiles = sortScreenProfiles(
  dedupeScreenProfiles([
    ...deviceScreenProfiles,
    ...genericScreenProfiles,
  ]),
)

function generatedProfileTitle(item: ScreenProfile) {
  const recipe = recipeName(item.config.driver, item.config.recipe)
  const recipeNote = recipe === 'STANDARD' ? '' : (recipeDisplayNames[recipe] || recipe.split('_').join(' '))
  return [
    driverName(item.config.driver), `${item.size}寸`, `${item.config.width}x${item.config.height}`,
    colorDisplayNames.get(item.config.color) || colorCode(item.config.color), recipeNote,
  ].filter(Boolean).join(' ')
}

export function screenProfileTitle(item: ScreenProfile) {
  const generated = generatedProfileTitle(item)
  return item.tag === 'user' && item.name.trim() ? `${generated} ${item.name.trim()}` : generated
}

export function screenProfileNote(item: ScreenProfile) {
  return [
    driverName(item.config.driver), `${item.size}寸`, `${item.config.width}x${item.config.height}`,
    colorProtocolCodes.get(item.config.color) || String(item.config.color),
    recipeName(item.config.driver, item.config.recipe),
  ].join(' / ')
}

export function defaultProfileName(config: ScreenConfig) {
  return `${driverName(config.driver)}_${config.width}x${config.height}_${colorCode(config.color).replace(/\s+/g, '')}_${recipeName(config.driver, config.recipe)}`
}
