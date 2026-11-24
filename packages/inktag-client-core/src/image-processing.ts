import type { RgbaFrame } from './screen'

export type DitherAlgorithm = 'none' | 'floyd-steinberg' | 'atkinson' | 'sierra-lite' | 'bayer'
export interface DitherOptions {
  algorithm: DitherAlgorithm
  strength: number
  contrast: number
  heavyInk: boolean
  color: number
}
export interface PaletteColor {
  name: string
  rgb: readonly [number, number, number]
  perceived: readonly [number, number, number]
}

const black: PaletteColor = { name: '黑', rgb: [0, 0, 0], perceived: [30, 30, 30] }
const white: PaletteColor = { name: '白', rgb: [255, 255, 255], perceived: [220, 215, 205] }
const red: PaletteColor = { name: '红', rgb: [220, 48, 38], perceived: [180, 50, 50] }
const yellow: PaletteColor = { name: '黄', rgb: [235, 194, 25], perceived: [200, 195, 60] }
const blue: PaletteColor = { name: '蓝', rgb: [44, 97, 165], perceived: [30, 40, 140] }
const green: PaletteColor = { name: '绿', rgb: [19, 116, 99], perceived: [35, 140, 35] }
const orange: PaletteColor = { name: '橙', rgb: [225, 139, 35], perceived: [190, 90, 35] }

export function paletteForColor(color: number): readonly PaletteColor[] {
  if (color === 2) return [black, white, red]
  if (color === 3) return [black, white, red, yellow]
  if (color === 4) return [
    { name: '黑', rgb: [0, 0, 0], perceived: [0, 0, 0] },
    { name: '深灰', rgb: [85, 85, 85], perceived: [130, 130, 130] },
    { name: '浅灰', rgb: [170, 170, 170], perceived: [205, 205, 205] },
    { name: '白', rgb: [255, 255, 255], perceived: [255, 255, 255] },
  ]
  if (color === 5) return [black, white, yellow]
  if (color === 6) return [black, white, blue]
  if (color === 7) return [black, white, green]
  if (color === 8) return [black, white, orange]
  if (color === 9) return Array.from({ length: 16 }, (_, index) => {
    const level = Math.round(index * 255 / 15)
    return { name: `${index + 1} 阶`, rgb: [level, level, level] as const, perceived: [level, level, level] as const }
  })
  if (color === 10) return [black, white, red, yellow, green, blue]
  if (color === 11) return [black, white, red, yellow, green, blue, orange]
  return [black, white]
}

function clamp(value: number): number { return Math.max(0, Math.min(255, value)) }
/** Neutral text/gray antialiasing must not acquire a colored pigment. */
export function neutralPaletteMismatch(r: number, g: number, b: number, target: readonly number[]): boolean {
  return Math.max(r, g, b) - Math.min(r, g, b) <= 8 && Math.max(...target) - Math.min(...target) > 8
}
function prepareChannel(value: number, contrast: number, heavyInk: boolean): number {
  const contrasted = (value - 128) * contrast + 128
  if (!heavyInk) return clamp(contrasted)
  return clamp(255 * Math.pow(clamp(contrasted) / 255, 1.12))
}
function nearest(r: number, g: number, b: number, palette: readonly PaletteColor[]): readonly [number, number, number] {
  let result = palette[0]
  let best = Number.POSITIVE_INFINITY
  for (const candidate of palette) {
    if (neutralPaletteMismatch(r, g, b, candidate.rgb)) continue
    const dr = r - candidate.rgb[0]
    const dg = g - candidate.rgb[1]
    const db = b - candidate.rgb[2]
    const distance = dr * dr * .3 + dg * dg * .59 + db * db * .11
    if (distance < best) { best = distance; result = candidate }
  }
  return result.rgb
}

type KernelEntry = readonly [dx: number, dy: number, weight: number]
const kernels: Record<Exclude<DitherAlgorithm, 'none' | 'bayer'>, readonly KernelEntry[]> = {
  'floyd-steinberg': [[1, 0, 7 / 16], [-1, 1, 3 / 16], [0, 1, 5 / 16], [1, 1, 1 / 16]],
  atkinson: [[1, 0, 1 / 8], [2, 0, 1 / 8], [-1, 1, 1 / 8], [0, 1, 1 / 8], [1, 1, 1 / 8], [0, 2, 1 / 8]],
  'sierra-lite': [[1, 0, 2 / 4], [-1, 1, 1 / 4], [0, 1, 1 / 4]],
}
const bayer8 = [
  0, 32, 8, 40, 2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
  12, 44, 4, 36, 14, 46, 6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
  3, 35, 11, 43, 1, 33, 9, 41, 51, 19, 59, 27, 49, 17, 57, 25,
  15, 47, 7, 39, 13, 45, 5, 37, 63, 31, 55, 23, 61, 29, 53, 21,
] as const

export function ditherFrame(frame: RgbaFrame, options: DitherOptions): RgbaFrame {
  const { width, height } = frame
  const palette = paletteForColor(options.color)
  const work = new Float32Array(width * height * 3)
  const output = new Uint8ClampedArray(width * height * 4)
  for (let index = 0; index < width * height; index += 1) {
    const source = index * 4
    const alpha = frame.data[source + 3] / 255
    let r = frame.data[source] * alpha + 255 * (1 - alpha)
    let g = frame.data[source + 1] * alpha + 255 * (1 - alpha)
    let b = frame.data[source + 2] * alpha + 255 * (1 - alpha)
    if (options.heavyInk) {
      const gray = .299 * r + .587 * g + .114 * b
      r = gray + (r - gray) * 1.18
      g = gray + (g - gray) * 1.18
      b = gray + (b - gray) * 1.18
    }
    work[index * 3] = prepareChannel(r, options.contrast, options.heavyInk)
    work[index * 3 + 1] = prepareChannel(g, options.contrast, options.heavyInk)
    work[index * 3 + 2] = prepareChannel(b, options.contrast, options.heavyInk)
  }
  for (let y = 0; y < height; y += 1) {
    const reverse = options.algorithm !== 'bayer' && (y & 1) === 1
    for (let column = 0; column < width; column += 1) {
      const x = reverse ? width - 1 - column : column
      const index = y * width + x
      const offset = index * 3
      let r = work[offset]
      let g = work[offset + 1]
      let b = work[offset + 2]
      if (options.algorithm === 'bayer') {
        const threshold = (bayer8[(y & 7) * 8 + (x & 7)] / 63 - .5) * 128 * options.strength
        r += threshold; g += threshold; b += threshold
      }
      const selected = nearest(clamp(r), clamp(g), clamp(b), palette)
      const target = index * 4
      output[target] = selected[0]; output[target + 1] = selected[1]; output[target + 2] = selected[2]; output[target + 3] = 255
      const kernel = options.algorithm === 'none' || options.algorithm === 'bayer' ? null : kernels[options.algorithm]
      if (!kernel) continue
      const errorR = (r - selected[0]) * options.strength
      const errorG = (g - selected[1]) * options.strength
      const errorB = (b - selected[2]) * options.strength
      for (const [rawDx, dy, weight] of kernel) {
        const dx = reverse ? -rawDx : rawDx
        const nextX = x + dx
        const nextY = y + dy
        if (nextX < 0 || nextX >= width || nextY >= height) continue
        const next = (nextY * width + nextX) * 3
        work[next] += errorR * weight; work[next + 1] += errorG * weight; work[next + 2] += errorB * weight
      }
    }
  }
  return { width, height, data: output }
}
