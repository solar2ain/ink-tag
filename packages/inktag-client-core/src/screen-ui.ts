import { SCREEN_PROFILE_V2_VERSION, ScreenControllerRegistryV2 } from './generated/commands-v2'
import { screenColorCode, screenProfilesEqualV2, type ScreenConfigV2 } from './screen'

export type ScreenConfig = ScreenConfigV2
export function makeScreenConfig(values: Omit<ScreenConfig, 'schemaVersion' | 'recipe' | 'nativeUiScale'> & { recipe?: number; nativeUiScale?: number }): ScreenConfig {
  return { schemaVersion: SCREEN_PROFILE_V2_VERSION, recipe: 0, nativeUiScale: 0, ...values }
}
export function driverName(driver: number) {
  return ScreenControllerRegistryV2.find((item) => item.id === driver)?.name || `0x${driver.toString(16)}`
}
export function recipeName(driver: number, recipe: number) {
  return ScreenControllerRegistryV2.find((item) => item.id === driver)?.recipes.find((item) => item.id === recipe)?.name || `RECIPE_${recipe}`
}
export const colorCode = screenColorCode
export const screenConfigsEqual = screenProfilesEqualV2
