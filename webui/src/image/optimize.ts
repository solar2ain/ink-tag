import { applyImageAdjustments, ditherImage, replaceColors, type DitherImageOptions, type PaletteColorEntry, type ProcessingPresetName } from 'epdoptimize'
import { legacyAdjustFrame, legacyDitherFrame, previewFrameToNative, supportsLegacyPalette, type LegacyImageAlgorithm } from './dither'
import { paletteForColor } from './processing'
import type { RgbaFrame } from '../protocol/screen'

export type ImageAlgorithm =
  | 'auto' | 'none' | 'floydSteinberg' | 'atkinson' | 'jarvis' | 'stucki' | 'burkes'
  | 'sierra3' | 'sierra2' | 'sierra2-4a' | 'bayer' | 'blueNoise' | 'riemersma' | 'simple2D'
  | 'legacyHybrid' | 'legacyOstromoukhov' | 'linearLightSierra'

export const algorithmGroups = [
  {
    label: '常用', options: [
      ['auto', 'Hybrid 自适应（默认）'],
      ['floydSteinberg', 'Floyd-Steinberg'], ['atkinson', 'Atkinson'],
      ['bayer', 'Bayer 有序抖动'], ['none', '仅色板量化'],
    ],
  },
  {
    label: '高级 · 误差扩散', options: [
      ['stucki', 'Stucki'], ['burkes', 'Burkes'], ['jarvis', 'Jarvis-Judice-Ninke'],
      ['sierra3', 'Sierra 3（实验）'], ['sierra2', 'Two-row Sierra'], ['sierra2-4a', 'Sierra Lite'],
      ['linearLightSierra', '线性光 Sierra'],
    ],
  },
  {
    label: '高级 · 空间', options: [
      ['blueNoise', 'Blue Noise'], ['riemersma', 'Riemersma'], ['simple2D', 'Simple 2D（实验）'],
      ['legacyOstromoukhov', 'InkTag 自适应三点'],
    ],
  },
] as const satisfies ReadonlyArray<{ label: string; options: ReadonlyArray<readonly [ImageAlgorithm, string]> }>

export const processingPresets: Array<readonly [ProcessingPresetName, string]> = [
  ['balanced', '不处理'], ['dynamic', '动态照片'], ['vivid', '鲜明插画'], ['soft', '柔和渐变'],
  ['grayscale', '灰阶照片'], ['restore', '旧照片修复'], ['posterScan', '纸张与海报'],
]

function hex(rgb: readonly [number, number, number]) {
  return `#${rgb.map((value) => value.toString(16).padStart(2, '0')).join('')}`
}

export function calibratedPalette(color: number): PaletteColorEntry[] {
  return paletteForColor(color).map((entry) => ({
    name: entry.name,
    color: hex(entry.perceived),
    deviceColor: hex(entry.rgb),
  }))
}

function algorithmOptions(algorithm: ImageAlgorithm): Partial<DitherImageOptions> {
  if (algorithm === 'auto') return { ditheringType: 'errorDiffusion', errorDiffusionMatrix: 'floydSteinberg', serpentine: true }
  if (algorithm === 'none') return { ditheringType: 'quantizationOnly' }
  if (algorithm === 'bayer') return { ditheringType: 'ordered', orderedDitheringType: 'bayer', orderedDitheringMatrix: [8, 8] }
  if (algorithm === 'blueNoise') return { ditheringType: 'blueNoise' }
  if (algorithm === 'riemersma') return { ditheringType: 'riemersma' }
  if (algorithm === 'simple2D') return { ditheringType: 'simple2D' }
  return { ditheringType: 'errorDiffusion', errorDiffusionMatrix: algorithm, serpentine: true }
}

export interface OptimizeOptions {
  algorithm: ImageAlgorithm
  preset: ProcessingPresetName
  contrast: number
  brightness?: number
  heavyInk: boolean
  color: number
  strength: number
}

export function directPreviewFrame(frame: RgbaFrame, options: OptimizeOptions): RgbaFrame | null {
  const palette = paletteForColor(options.color)
  const algorithm = legacyAlgorithm(options.algorithm)
  if (!algorithm || !supportsLegacyPalette(palette)) return null
  return legacyDitherFrame(frame, palette, algorithm, options.strength, options.heavyInk)
}

function legacyAlgorithm(algorithm: ImageAlgorithm): LegacyImageAlgorithm | null {
  if (algorithm === 'auto' || algorithm === 'legacyHybrid') return 'legacyHybrid'
  if (algorithm === 'sierra3' || algorithm === 'simple2D') return null
  return algorithm
}

export function usesDirectStrength(algorithm: ImageAlgorithm) {
  const legacy = legacyAlgorithm(algorithm)
  return legacy !== null && legacy !== 'none'
}

const strengthPattern = [
  0, 32, 8, 40, 2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
  12, 44, 4, 36, 14, 46, 6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
  3, 35, 11, 43, 1, 33, 9, 41, 51, 19, 59, 27, 49, 17, 57, 25,
  15, 47, 7, 39, 13, 45, 5, 37, 63, 31, 55, 23, 61, 29, 53, 21,
] as const

export function mixDitherStrength(dithered: Uint8ClampedArray, quantized: Uint8ClampedArray, width: number, strength: number) {
  const amount = Math.max(0, Math.min(1, strength))
  for (let pixel = 0; pixel < dithered.length / 4; pixel += 1) {
    const x = pixel % width
    const y = Math.floor(pixel / width)
    if (amount >= (strengthPattern[(y & 7) * 8 + (x & 7)] + 1) / 64) continue
    const offset = pixel * 4
    dithered.set(quantized.subarray(offset, offset + 4), offset)
  }
  return dithered
}

export function restoreUncoveredPixels(
  coverage: Uint8ClampedArray,
  output: Uint8ClampedArray,
  color: readonly [number, number, number],
) {
  if (coverage.length !== output.length) throw new Error('图片覆盖蒙版尺寸不匹配')
  for (let offset = 0; offset < output.length; offset += 4) {
    if (coverage[offset + 3] !== 0) continue
    output[offset] = color[0]
    output[offset + 1] = color[1]
    output[offset + 2] = color[2]
    output[offset + 3] = 255
  }
  return output
}

function restoreUncoveredCanvas(mask: HTMLCanvasElement | undefined, output: HTMLCanvasElement, color: readonly [number, number, number]) {
  if (!mask) return
  const maskContext = mask.getContext('2d', { willReadFrequently: true })!
  const outputContext = output.getContext('2d', { willReadFrequently: true })!
  const coverage = maskContext.getImageData(0, 0, mask.width, mask.height)
  const image = outputContext.getImageData(0, 0, output.width, output.height)
  restoreUncoveredPixels(coverage.data, image.data, color)
  outputContext.putImageData(image, 0, 0)
}

export async function optimizeCanvas(
  source: HTMLCanvasElement,
  preview: HTMLCanvasElement,
  device: HTMLCanvasElement,
  options: OptimizeOptions,
  adjustedOutput?: HTMLCanvasElement,
  coverageMask?: HTMLCanvasElement,
) {
  preview.width = source.width
  preview.height = source.height
  device.width = source.width
  device.height = source.height
  const palette = calibratedPalette(options.color)
  const targetPalette = paletteForColor(options.color)
  const screenWhite = targetPalette.find((entry) => entry.name === '白') || targetPalette[targetPalette.length - 1]
  const adjusted = document.createElement('canvas')
  adjusted.width = source.width
  adjusted.height = source.height
  if (options.preset === 'balanced') {
    adjusted.getContext('2d')!.drawImage(source, 0, 0)
  } else {
    await applyImageAdjustments(source, adjusted, {
      palette,
      processingPreset: options.preset,
      adjustmentEngine: 'auto',
    })
  }
  const adjustedContext = adjusted.getContext('2d', { willReadFrequently: true })!
  const adjustedImage = adjustedContext.getImageData(0, 0, adjusted.width, adjusted.height)
  const contrastedFrame = legacyAdjustFrame(adjustedImage, options.contrast, options.brightness ?? 0)
  adjustedContext.putImageData(new ImageData(new Uint8ClampedArray(contrastedFrame.data), contrastedFrame.width, contrastedFrame.height), 0, 0)
  restoreUncoveredCanvas(coverageMask, adjusted, [255, 255, 255])
  if (adjustedOutput) {
    adjustedOutput.width = adjusted.width
    adjustedOutput.height = adjusted.height
    adjustedOutput.getContext('2d')!.drawImage(adjusted, 0, 0)
  }

  const previewFrame = directPreviewFrame(contrastedFrame, options)
  if (previewFrame) {
    preview.getContext('2d')!.putImageData(new ImageData(new Uint8ClampedArray(previewFrame.data), previewFrame.width, previewFrame.height), 0, 0)
    const nativeFrame = previewFrameToNative(previewFrame, targetPalette)
    device.getContext('2d')!.putImageData(new ImageData(new Uint8ClampedArray(nativeFrame.data), nativeFrame.width, nativeFrame.height), 0, 0)
    restoreUncoveredCanvas(coverageMask, preview, screenWhite.perceived)
    restoreUncoveredCanvas(coverageMask, device, screenWhite.rgb)
    return
  }

  const ditherOptions: DitherImageOptions = {
    palette,
    colorMatching: options.color >= 4 ? 'lab' : 'chroma',
    processingEngine: 'auto',
    toneMapping: options.heavyInk
      ? { mode: 'scurve', contrast: Math.max(.15, options.contrast - 1), saturation: .2, strength: .75, shadowBoost: -.08 }
      : { mode: 'off' },
    dynamicRangeCompression: { mode: 'off' },
    edgePreservation: { enabled: true, strength: options.algorithm === 'bayer' ? .85 : .55, threshold: .16, radius: 1 },
  }
  await ditherImage(adjusted, preview, { ...ditherOptions, ...algorithmOptions(options.algorithm) })
  if (options.algorithm !== 'none' && options.strength < 1) {
    const quantized = document.createElement('canvas')
    quantized.width = source.width
    quantized.height = source.height
    await ditherImage(adjusted, quantized, { ...ditherOptions, ditheringType: 'quantizationOnly' })
    const previewContext = preview.getContext('2d')!
    const previewImage = previewContext.getImageData(0, 0, preview.width, preview.height)
    const quantizedImage = quantized.getContext('2d')!.getImageData(0, 0, quantized.width, quantized.height)
    mixDitherStrength(previewImage.data, quantizedImage.data, preview.width, options.strength)
    previewContext.putImageData(previewImage, 0, 0)
  }
  replaceColors(preview, device, palette)
  restoreUncoveredCanvas(coverageMask, preview, screenWhite.perceived)
  restoreUncoveredCanvas(coverageMask, device, screenWhite.rgb)
}
