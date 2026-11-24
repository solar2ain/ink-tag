import type { ScreenConfig } from '../types'
import {
  COMMON_SCREEN_RESOLUTIONS,
  TransferFormat,
  commonImagePreviewResolutions,
  encodeScreenProfileV2,
  imageStoreCompatibilityChangedV2,
  logicalCanvasSize,
  screenColorCode,
  screenProfilesEqualV2,
  supportedColorsForScreenProfile,
  transferFormatFor as sharedTransferFormatFor,
  transformRgba,
  validateScreenProfileV2,
  type RgbaFrame,
} from '../../../packages/inktag-client-core/src/screen'
import {
  NativeUiScaleV2, PanelRecipeV2, SCREEN_PROFILE_V2_VERSION, SCREEN_PROFILE_V2_WIRE_SIZE,
  ScreenControllerRegistryV2, ScreenControllerV2,
} from './generatedCommandsV2'

export { TransferFormat, commonImagePreviewResolutions, transformRgba }
export type { RgbaFrame }

export const SCREEN_SCHEMA_VERSION = SCREEN_PROFILE_V2_VERSION
export const SCREEN_WIRE_SIZE = SCREEN_PROFILE_V2_WIRE_SIZE

/** UI guidance only; firmware verifies entitlement again before physical IO. */
export function screenRequiresPro(config: Pick<ScreenConfig, 'driver' | 'recipe'>) {
  return config.driver === ScreenControllerV2.IST7513 || config.driver === ScreenControllerV2.IST7601 ||
    (config.driver === ScreenControllerV2.JD79665CA &&
      config.recipe === PanelRecipeV2.JD79665CA.LENMORY_H364F067_E6)
}

export class ScreenWriteUnconfirmedError extends Error {
  constructor() {
    super('屏幕配置保存结果未确认；请只读核对同一设备，勿重复写入。重新连接并确认实际状态后再修改。')
    this.name = 'ScreenWriteUnconfirmedError'
  }
}

export const nativeUiScales = [
  [NativeUiScaleV2.AUTO, '自动'],
  [NativeUiScaleV2.X1, '100%'],
  [NativeUiScaleV2.X1_25, '125%'],
  [NativeUiScaleV2.X1_5, '150%'],
  [NativeUiScaleV2.X1_75, '175%'],
  [NativeUiScaleV2.X2, '200%'],
] as const

export const drivers = ScreenControllerRegistryV2.map(({ id, name }) => [id, name] as const)

export const colors = [
  [1, '黑白'], [2, '黑白红'], [3, '黑白红黄'], [4, '4 灰阶'], [5, '黑白黄'],
  [6, '黑白蓝'], [7, '黑白绿'], [8, '黑白橙'], [9, '16 灰阶'],
  [10, 'KWRYGB（六色）'], [11, '七色'],
] as const

function allRecipesForDriver(driver: number) {
  return ScreenControllerRegistryV2.find((item) => item.id === driver)?.recipes || []
}

export function recipesForDriver(driver: number) {
  return allRecipesForDriver(driver).filter((recipe) => !('retired' in recipe && recipe.retired))
}

export function recipeName(driver: number, recipe: number) {
  return allRecipesForDriver(driver).find((item) => item.id === recipe)?.name || `RECIPE_${recipe}`
}

export function supportedColorsForDriver(driver: number, recipe = 0): number[] {
  return supportedColorsForScreenProfile(driver, recipe)
}

export function validateScreenConfig(config: ScreenConfig): boolean {
  return validateScreenProfileV2(config)
}

export const resolutions = COMMON_SCREEN_RESOLUTIONS

export function driverName(driver: number) {
  return drivers.find(([id]) => id === driver)?.[1] || `0x${driver.toString(16)}`
}

/** Driver labels do not rename wire IDs or qualify a silicon revision. */
export function controllerDisplayName(driver: number, _recipe?: number) {
  if (driver === 0x10) return 'UC 原厂驱动'
  // Agreed SSD1681 display name; exact die is unverified. Recipe1/2 keep distinct OTP paths.
  if (driver === 0x2f) return 'SSD1681'
  // epd-lab 2026-10-11: AN0213A250's selected JD79676 driver name;
  // measured REV differs from the AA reference, so do not add that suffix.
  if (driver === 0x3f) return 'JD79676'
  return driverName(driver)
}

export function colorCode(color: number) {
  return screenColorCode(color)
}

export function makeScreenConfig(values: Omit<ScreenConfig, 'schemaVersion' | 'recipe' | 'nativeUiScale'> & { recipe?: number; nativeUiScale?: number }): ScreenConfig {
  return {
    schemaVersion: SCREEN_SCHEMA_VERSION,
    recipe: PanelRecipeV2.UC8XXX.STANDARD,
    nativeUiScale: NativeUiScaleV2.AUTO,
    ...values,
  }
}

export function screenConfigsEqual(left: ScreenConfig, right: ScreenConfig): boolean {
  return screenProfilesEqualV2(left, right)
}

export function imageStoreCompatibilityChanged(before: ScreenConfig, after: ScreenConfig): boolean {
  return imageStoreCompatibilityChangedV2(before, after)
}

export function encodeScreenConfig(config: ScreenConfig): Uint8Array {
  if (!validateScreenConfig(config)) throw new RangeError('invalid screen config')
  return encodeScreenProfileV2(config)
}

export function decodeScreenConfig(bytes: Uint8Array): ScreenConfig | null {
  if (bytes.length !== SCREEN_WIRE_SIZE || bytes[0] !== SCREEN_SCHEMA_VERSION) return null
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  return {
    schemaVersion: bytes[0], driver: bytes[1], recipe: bytes[2], color: bytes[3], transform: bytes[4] & 0x0f,
    nativeUiScale: (bytes[4] >> 4) & 0x0f,
    width: view.getUint16(5, true), height: view.getUint16(7, true),
    visibleX: view.getUint16(9, true), visibleY: view.getUint16(11, true),
    visibleWidth: view.getUint16(13, true), visibleHeight: view.getUint16(15, true),
  }
}

export function canvasSizeForScreen(config: ScreenConfig) {
  return logicalCanvasSize(config)
}

export function transferFormatFor(config: ScreenConfig): number {
  return sharedTransferFormatFor(config)
}
