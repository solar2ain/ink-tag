import type { ScreenProfileV2 } from './decoders'
import {
  NativeUiScaleV2,
  SCREEN_PROFILE_V2_VERSION,
  SCREEN_PROFILE_V2_WIRE_SIZE,
  ScreenRuleRegistryV2,
} from './generated/commands-v2'

export type ScreenConfigV2 = Omit<ScreenProfileV2, 'controller'> & { driver: number }
export type ScreenProfileLikeV2 = ScreenProfileV2 | ScreenConfigV2

/**
 * Common physical screen presets used by client-side previews while no device
 * is connected. Exact device profiles still come from the connected device.
 */
export const COMMON_SCREEN_RESOLUTIONS = [
  ['1.54', 152, 152], ['1.54', 200, 200], ['1.54', 240, 240], ['1.69', 400, 400], ['2.13', 104, 212], ['2.13', 128, 250],
  ['2.66', 152, 296], ['2.8', 152, 296], ['2.9', 128, 296], ['2.9', 168, 384], ['3.1', 300, 300],
  ['3.52', 360, 240], ['3.64', 760, 568], ['3.7', 480, 280], ['3.7', 720, 480], ['3.98', 768, 552], ['4.01', 640, 400],
  ['4.2', 400, 300], ['4.26', 800, 480], ['5.65', 600, 448], ['5.83', 600, 448], ['5.83', 648, 480],
  ['7.3', 800, 480], ['7.5', 640, 384], ['7.5', 800, 480], ['7.5', 880, 528],
  ['10.2', 960, 640],
] as const

export interface ImagePreviewResolution {
  id: string
  width: number
  height: number
  sizes: string[]
}

export function commonImagePreviewResolutions(): ImagePreviewResolution[] {
  return [...COMMON_SCREEN_RESOLUTIONS.reduce((items, [size, rawWidth, rawHeight]) => {
    const width = Math.max(rawWidth, rawHeight)
    const height = Math.min(rawWidth, rawHeight)
    const id = `${width}x${height}`
    const existing = items.get(id)
    if (existing) existing.sizes.push(size)
    else items.set(id, { id, width, height, sizes: [size] as string[] })
    return items
  }, new Map<string, ImagePreviewResolution>()).values()]
}

export interface RgbaFrame {
  width: number
  height: number
  data: Uint8ClampedArray
}

export const TransferFormat = {
  MONO_1BPP: 1,
  DUAL_1BPP: 2,
  INDEXED_2BPP: 3,
  PLANAR_2X1BPP: 4,
  INDEXED_4BPP: 5,
  INDEXED_2X2BPP: 6,
} as const

type ScreenRule = {
  controller: number
  recipe: number
  colors: readonly number[]
  widthAlignment: number
  maxWidth: number
  maxHeight: number
  fixedSizes: readonly (readonly [number, number])[]
  colorFixedSizes: readonly { color: number; sizes: readonly (readonly [number, number])[] }[]
  transferFormat: number
  fullViewport?: boolean
}

const driverRules = new Map<string, ScreenRule>(
  ScreenRuleRegistryV2.map((rule) => [`${rule.controller}:${rule.recipe}`, rule]),
)

export function screenController(profile: ScreenProfileLikeV2): number {
  return 'controller' in profile ? profile.controller : profile.driver
}

function ruleFor(profile: ScreenProfileLikeV2): ScreenRule | undefined {
  return driverRules.get(`${screenController(profile)}:${profile.recipe}`)
}

export function supportedColorsForScreenProfile(controller: number, recipe = 0): number[] {
  return [...(driverRules.get(`${controller}:${recipe}`)?.colors || [])]
}

export function validateScreenProfileV2(profile: ScreenProfileLikeV2): boolean {
  const rule = ruleFor(profile)
  if (!rule || profile.schemaVersion !== SCREEN_PROFILE_V2_VERSION ||
    !(rule.colors as readonly number[]).includes(profile.color)) return false
  const values = [profile.width, profile.height, profile.visibleX, profile.visibleY,
    profile.visibleWidth, profile.visibleHeight]
  if (values.some((value) => !Number.isInteger(value) || value < 0 || value > 0xffff)) return false
  if (!profile.width || !profile.height || !profile.visibleWidth || !profile.visibleHeight ||
    profile.width > rule.maxWidth || profile.height > rule.maxHeight ||
    profile.width % rule.widthAlignment !== 0 ||
    profile.visibleX + profile.visibleWidth > profile.width ||
    profile.visibleY + profile.visibleHeight > profile.height ||
    !Number.isInteger(profile.transform) || profile.transform < 0 || profile.transform > 15 ||
    !Number.isInteger(profile.nativeUiScale) || profile.nativeUiScale < NativeUiScaleV2.AUTO ||
    profile.nativeUiScale > NativeUiScaleV2.X1_75) return false
  const size = [profile.width, profile.height]
  if (rule.fullViewport && (profile.visibleX !== 0 || profile.visibleY !== 0 ||
    profile.visibleWidth !== profile.width || profile.visibleHeight !== profile.height)) return false
  if (rule.fixedSizes.length && !rule.fixedSizes.some(([width, height]) => width === size[0] && height === size[1])) return false
  const colorSizes = rule.colorFixedSizes.find((item) => item.color === profile.color)?.sizes || []
  if (colorSizes.length && !colorSizes.some(([width, height]) => width === size[0] && height === size[1])) return false
  return true
}

export function screenProfilesEqualV2(left: ScreenProfileLikeV2, right: ScreenProfileLikeV2): boolean {
  return left.schemaVersion === right.schemaVersion && screenController(left) === screenController(right) &&
    left.recipe === right.recipe && left.color === right.color && left.transform === right.transform &&
    left.nativeUiScale === right.nativeUiScale && left.width === right.width && left.height === right.height &&
    left.visibleX === right.visibleX && left.visibleY === right.visibleY &&
    left.visibleWidth === right.visibleWidth && left.visibleHeight === right.visibleHeight
}

export function imageStoreCompatibilityChangedV2(before: ScreenProfileLikeV2, after: ScreenProfileLikeV2): boolean {
  return screenController(before) !== screenController(after) || before.recipe !== after.recipe ||
    before.color !== after.color || before.transform !== after.transform ||
    before.width !== after.width || before.height !== after.height ||
    before.visibleX !== after.visibleX || before.visibleY !== after.visibleY ||
    before.visibleWidth !== after.visibleWidth || before.visibleHeight !== after.visibleHeight
}

export function encodeScreenProfileV2(profile: ScreenProfileLikeV2): Uint8Array {
  const bytes = new Uint8Array(SCREEN_PROFILE_V2_WIRE_SIZE)
  const view = new DataView(bytes.buffer)
  bytes[0] = profile.schemaVersion
  bytes[1] = screenController(profile)
  bytes[2] = profile.recipe
  bytes[3] = profile.color
  bytes[4] = profile.transform | (profile.nativeUiScale << 4)
  view.setUint16(5, profile.width, true)
  view.setUint16(7, profile.height, true)
  view.setUint16(9, profile.visibleX, true)
  view.setUint16(11, profile.visibleY, true)
  view.setUint16(13, profile.visibleWidth, true)
  view.setUint16(15, profile.visibleHeight, true)
  return bytes
}

export function logicalCanvasSize(profile: ScreenProfileLikeV2): { width: number; height: number } {
  const rotation = profile.transform & 3
  return rotation === 1 || rotation === 3
    ? { width: profile.visibleHeight, height: profile.visibleWidth }
    : { width: profile.visibleWidth, height: profile.visibleHeight }
}

export const canvasSizeForScreen = logicalCanvasSize

export function screenColorCode(color: number): string {
  return ({
    1: 'BW', 2: 'BWR', 3: 'BWRY', 4: '4 灰阶', 5: 'BWY', 6: 'BWB', 7: 'BWG',
    8: 'BWO', 9: '16 灰阶', 10: 'KWRYGB', 11: 'BWRYGBO',
  } as Record<number, string>)[color] || String(color)
}

export function transferFormatFor(profile: ScreenProfileLikeV2): number {
  const transferFormat = ruleFor(profile)?.transferFormat || 0
  if (transferFormat) return transferFormat
  if (profile.color === 4) return TransferFormat.PLANAR_2X1BPP
  return profile.color === 1 ? TransferFormat.MONO_1BPP : TransferFormat.DUAL_1BPP
}

export function transformRgba(image: RgbaFrame, transform: number): RgbaFrame {
  const rotation = transform & 3
  const mirrorX = (transform & 4) !== 0
  const mirrorY = (transform & 8) !== 0
  const width = rotation === 1 || rotation === 3 ? image.height : image.width
  const height = rotation === 1 || rotation === 3 ? image.width : image.height
  const data = new Uint8ClampedArray(width * height * 4)
  for (let y = 0; y < image.height; y++) {
    for (let x = 0; x < image.width; x++) {
      let tx = x
      let ty = y
      if (rotation === 1) { tx = image.height - 1 - y; ty = x }
      else if (rotation === 2) { tx = image.width - 1 - x; ty = image.height - 1 - y }
      else if (rotation === 3) { tx = y; ty = image.width - 1 - x }
      if (mirrorX) tx = width - 1 - tx
      if (mirrorY) ty = height - 1 - ty
      const source = (y * image.width + x) * 4
      const target = (ty * width + tx) * 4
      data.set(image.data.subarray(source, source + 4), target)
    }
  }
  return { width, height, data }
}
