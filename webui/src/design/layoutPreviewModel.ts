export type LayoutFamily = 'compact-square' | 'compact-rect' | 'normal-rect' | 'large-rect'
export type LayoutPage = 'device-info' | 'calendar' | 'clock'

export interface LayoutScreen {
  id: string
  label: string
  note: string
  width: number
  height: number
  /** Native device-font scale in Q8. 256 = 100%, 384 = 150%. */
  fontScaleQ8: number
  /** v1 candidate scale; v0 remains an immutable rendering baseline. */
  v1FontScaleQ8?: number
}

export const layoutPagesV0: ReadonlyArray<{ id: LayoutPage; label: string; english: string }> = [
  { id: 'device-info', label: '设备页', english: 'DEVICE INFO' },
  { id: 'calendar', label: '日历', english: 'CALENDAR' },
  { id: 'clock', label: '时钟', english: 'CLOCK' },
]

export const layoutPagesV1: ReadonlyArray<{ id: LayoutPage; label: string; english: string }> = [
  { id: 'calendar', label: '日历', english: 'CALENDAR' },
  { id: 'clock', label: '时钟', english: 'CLOCK' },
  { id: 'device-info', label: '设备信息', english: 'DEVICE' },
]
export const layoutDefaultPageV1: LayoutPage = 'calendar'

/** Kept for v0 fixture generation and callers that have not selected a revision. */
export const layoutPages = layoutPagesV0

export function layoutPagesForRevision(revision: 'v0' | 'v1') {
  return revision === 'v1' ? layoutPagesV1 : layoutPagesV0
}

export const layoutFamilyLabels: Record<LayoutFamily, { label: string; code: string; rule: string }> = {
  'compact-square': { label: 'Compact 方形', code: 'compact-square', rule: '长边 < 300，接近方形' },
  'compact-rect': { label: 'Compact 矩形', code: 'compact-rect', rule: '长边 < 300，横向矩形' },
  'normal-rect': { label: 'Normal 矩形', code: 'normal', rule: '300 ≤ 长边 ≤ 500' },
  'large-rect': { label: 'Large 矩形', code: 'large', rule: '长边 > 500' },
}

const sharedRectScreens: readonly LayoutScreen[] = [
  { id: 'rect-212', label: '2.13 英寸窄屏', note: '212 × 104', width: 212, height: 104, fontScaleQ8: 256 },
  { id: 'rect-250', label: '2.13 英寸宽屏', note: '250 × 128', width: 250, height: 128, fontScaleQ8: 256 },
  { id: 'rect-296', label: '2.66 / 2.9 英寸', note: '296 × 152 · 单元格带农历', width: 296, height: 152, fontScaleQ8: 256 },
  { id: 'normal-400', label: '4.2 英寸', note: '标准矩形 · v1 字体 100%', width: 400, height: 300, fontScaleQ8: 256, v1FontScaleQ8: 256 },
  { id: 'large-600', label: '5.83 英寸', note: '高分矩形 · v1 字体 125%', width: 600, height: 448, fontScaleQ8: 384, v1FontScaleQ8: 320 },
  { id: 'large-752', label: '高分 4 英寸', note: '高 PPI · v1 字体 150%', width: 752, height: 568, fontScaleQ8: 384, v1FontScaleQ8: 384 },
  { id: 'large-800', label: '7.5 英寸', note: '宽幅矩形 · v1 字体 150%', width: 800, height: 480, fontScaleQ8: 384, v1FontScaleQ8: 384 },
]

/** Frozen v0 inventory. The historic 154×154 fixture must not be rewritten. */
export const layoutScreensV0: readonly LayoutScreen[] = [
  { id: 'square-154', label: '1.54 英寸方屏', note: '紧凑方形', width: 154, height: 154, fontScaleQ8: 256 },
  { id: 'square-200', label: '1.54 英寸方屏', note: '原始固件基线', width: 200, height: 200, fontScaleQ8: 256 },
  ...sharedRectScreens,
]

/** All three 1.54-inch geometries share the square renderer, not a driver fork. */
export const layoutScreensV1: readonly LayoutScreen[] = [
  { id: 'square-152', label: '1.54 英寸方屏', note: '152 基准', width: 152, height: 152, fontScaleQ8: 256 },
  { id: 'square-200', label: '1.54 英寸方屏', note: '与 152 等比放大', width: 200, height: 200, fontScaleQ8: 256 },
  { id: 'square-240', label: '1.54 英寸方屏', note: '240 × 240 · 单元格带农历', width: 240, height: 240, fontScaleQ8: 256 },
  ...sharedRectScreens,
]

/** Kept for v0 fixture generation and callers that have not selected a revision. */
export const layoutScreens = layoutScreensV0

export function layoutScreensForRevision(revision: 'v0' | 'v1') {
  return revision === 'v1' ? layoutScreensV1 : layoutScreensV0
}

export function layoutDefaultScaleQ8(screen: LayoutScreen, revision: 'v0' | 'v1') {
  return revision === 'v1' ? (screen.v1FontScaleQ8 ?? screen.fontScaleQ8) : screen.fontScaleQ8
}

export function classifyLayout(width: number, height: number): LayoutFamily {
  const longSide = Math.max(width, height)
  if (longSide < 300) {
    const shortSide = Math.min(width, height)
    return longSide / shortSide <= 1.25 ? 'compact-square' : 'compact-rect'
  }
  return longSide <= 500 ? 'normal-rect' : 'large-rect'
}

export function layoutScaleLabel(scaleQ8: number) {
  return `${Math.round(scaleQ8 * 100 / 256)}%`
}
