import { paletteForColor } from '../../image/processing'
import type { Document } from '../../../../packages/inktag-editor-core/src/index'

export const colorModes = [
  { value: 1, label: '双色 · 黑白' },
  { value: 2, label: '三色 · 黑白红' },
  { value: 3, label: '四色 · 黑白红黄' },
  { value: 4, label: '四灰阶 · 黑白灰' },
  { value: 5, label: '三色 · 黑白黄' },
  { value: 6, label: '三色 · 黑白蓝' },
  { value: 7, label: '三色 · 黑白绿' },
  { value: 8, label: '三色 · 黑白橙' },
  { value: 9, label: '十六灰阶 · 黑白灰' },
  { value: 10, label: '六色 · 黑白红黄绿蓝' },
  { value: 11, label: '七色 · 黑白红黄绿蓝橙' },
] as const
export function inksFor(mode: Document['colorMode'] = 2) {
  return paletteForColor(mode).map(ink => ({ name: ink.name, color: `#${ink.rgb.map(v => v.toString(16).padStart(2, '0')).join('')}` }))
}
/** Preview-only mapping: retain source styles when trying another panel palette. */
export function inkColor(color: string, mode: Document['colorMode'] = 2) {
  if(color==='transparent')return color
  const rgb = [1,3,5].map(i => Number.parseInt(color.slice(i, i + 2), 16))
  const candidates = inksFor(mode)
  return candidates.reduce((best, ink) => {
    const distance = [1,3,5].reduce((sum, i, c) => sum + (rgb[c] - Number.parseInt(ink.color.slice(i, i + 2), 16)) ** 2, 0)
    return distance < best.distance ? { distance, color: ink.color } : best
  }, { distance: Infinity, color: candidates[0].color }).color
}
