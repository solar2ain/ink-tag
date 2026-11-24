import type { RgbaFrame } from '../protocol/screen'
import type { PaletteColor } from './processing'
import {
  adjustBrightness,
  adjustContrast,
  ditherImage,
  type InkTagImageData,
} from './inktag_dither.js'

export type LegacyImageAlgorithm =
  | 'legacyHybrid' | 'legacyOstromoukhov' | 'linearLightSierra'
  | 'floydSteinberg' | 'atkinson' | 'jarvis' | 'stucki' | 'burkes'
  | 'sierra2' | 'sierra2-4a' | 'bayer' | 'blueNoise' | 'riemersma' | 'none'

type LegacyColorMode = 'blackWhiteColor' | 'threeColor' | 'fourColor' | 'gray4Color' | 'sixColor'

const algorithmNames: Record<LegacyImageAlgorithm, string> = {
  legacyHybrid: 'hybrid',
  legacyOstromoukhov: 'ostromoukhov',
  linearLightSierra: 'linearLightSierra',
  floydSteinberg: 'floydSteinberg',
  atkinson: 'atkinson',
  jarvis: 'jarvis',
  stucki: 'stucki',
  burkes: 'burkes',
  sierra2: 'twoRowSierra',
  'sierra2-4a': 'sierraLite',
  bayer: 'bayer',
  blueNoise: 'blueNoise',
  riemersma: 'riemersma',
  none: 'none',
}

export function supportsLegacyPalette(palette: readonly PaletteColor[]) {
  return legacyColorMode(palette) !== null
}

export function legacyAdjustFrame(frame: RgbaFrame, contrast: number, brightness = 0): RgbaFrame {
  const image = mutableImage(frame)
  adjustContrast(image, contrast)
  adjustBrightness(image, brightness)
  return image
}

export function legacyDitherFrame(
  frame: RgbaFrame,
  palette: readonly PaletteColor[],
  algorithm: LegacyImageAlgorithm,
  strength = 1,
  heavyInk = false,
): RgbaFrame {
  const mode = legacyColorMode(palette)
  if (!mode) throw new Error('墨水屏算法不支持当前色板')
  const image = mutableImage(frame)
  const dithered = ditherImage(image, algorithmNames[algorithm], calibratedLegacyStrength(algorithm, strength), mode, { heavyInk })
  return mapLegacyPalette(dithered, palette, mode)
}

export function calibratedLegacyStrength(algorithm: LegacyImageAlgorithm, strength: number) {
  const clamped = Math.max(0, Math.min(5, strength))
  if (algorithm !== 'legacyHybrid' || clamped <= 1) return clamped

  // Hybrid feeds strength into both blue-noise preparation and error diffusion.
  // Above 1.0, direct multiplication creates positive feedback and swallows
  // highlights. Keep the old 0-5 control range, but make its upper half a
  // perceptual fine-control range with diminishing gain.
  return 1 + .65 * (1 - Math.exp(-(clamped - 1) / 1.5))
}

function mutableImage(frame: RgbaFrame): InkTagImageData {
  return { width: frame.width, height: frame.height, data: new Uint8ClampedArray(frame.data) }
}

function legacyColorMode(palette: readonly PaletteColor[]): LegacyColorMode | null {
  if (palette.length === 2) return 'blackWhiteColor'
  if (palette.length === 3 && palette.some((item) => item.name === '红')) return 'threeColor'
  if (palette.length === 4 && palette.some((item) => item.name.includes('灰'))) return 'gray4Color'
  if (palette.length === 4 && palette.some((item) => item.name === '红') && palette.some((item) => item.name === '黄')) return 'fourColor'
  if (palette.length === 6 && ['红', '黄', '绿', '蓝'].every((name) => palette.some((item) => item.name === name))) return 'sixColor'
  return null
}

function mapLegacyPalette(frame: InkTagImageData, palette: readonly PaletteColor[], mode: LegacyColorMode): RgbaFrame {
  const output = new Uint8ClampedArray(frame.data.length)
  for (let offset = 0; offset < frame.data.length; offset += 4) {
    const selected = palette[legacyPaletteIndex(frame.data[offset], frame.data[offset + 1], frame.data[offset + 2], mode)]
    output[offset] = selected.perceived[0]
    output[offset + 1] = selected.perceived[1]
    output[offset + 2] = selected.perceived[2]
    output[offset + 3] = 255
  }
  return { width: frame.width, height: frame.height, data: output }
}

function legacyPaletteIndex(r: number, g: number, b: number, mode: LegacyColorMode) {
  if (mode === 'gray4Color') {
    const value = (r + g + b) / 3
    if (value < 65) return 0
    if (value < 168) return 1
    if (value < 230) return 2
    return 3
  }
  if (r < 80 && g < 80 && b < 80) return 0
  if (r > 220 && g > 220 && b > 220) return 1
  if (mode === 'threeColor') return 2
  if (mode === 'fourColor') return g < 128 ? 2 : 3
  if (r > 180 && g < 100 && b < 100) return 2
  if (r > 180 && g > 150 && b < 100) return 3
  if (g > r && g > b) return 4
  return 5
}

export function previewFrameToNative(frame: RgbaFrame, palette: readonly PaletteColor[]) {
  const output = new Uint8ClampedArray(frame.data.length)
  const nativeByPreview = new Map(palette.map((entry) => [entry.perceived.join(','), entry.rgb]))
  for (let index = 0; index < frame.data.length; index += 4) {
    const selected = nativeByPreview.get(`${frame.data[index]},${frame.data[index + 1]},${frame.data[index + 2]}`) || palette[0].rgb
    output[index] = selected[0]
    output[index + 1] = selected[1]
    output[index + 2] = selected[2]
    output[index + 3] = 255
  }
  return { width: frame.width, height: frame.height, data: output }
}
