import { DevicePixelGfx, type DevicePixelColor } from './devicePixelGfx'
import type { DeviceGuiFontId, DeviceGuiFrame } from './model'

export type CalendarTextRole = 'weekday' | 'day' | 'lunar' | 'holiday'

export interface NativeElement {
  key: string
  label: string
  type: 'text' | 'calendar' | 'clock' | 'graphic'
  source: DeviceGuiFrame
  protected?: boolean
  previewText?: string
  fontId?: DeviceGuiFontId
  /** Omitted means the preset's mixed fonts, colors and spacing are preserved. */
  fontOverride?: DeviceGuiFontId
  /** Authoring-only overrides; firmware default rendering remains unchanged. */
  calendarFonts?: Partial<Record<CalendarTextRole, DeviceGuiFontId>>
  colorOverride?: DevicePixelColor
  scaleQ8?: number
  fixedText?: boolean
  options?: Record<string, number | boolean>
  settings?: Record<string, number | boolean>
  /** Authoring box changed by the user; never changes the page resolution. */
  resized?: boolean
  sample?: { timestamp?: number; weekStart?: 0 | 1; temperature?: number; voltage?: number; batteryPercent?: number;
    bluetoothName?: string; deviceName?: string; boardName?: string; boardNameShort?: string; firmwareVersion?: string; screenInfo?: string }
  page?: 'calendar' | 'clock' | 'device-info'
  templateKey?: string
}

type ShapeMethod = 'drawPixel' | 'drawFastHLine' | 'drawFastVLine' | 'fillRect' | 'drawRect'
  | 'drawLine' | 'drawDottedLine' | 'drawCircle' | 'fillCircle' | 'drawRoundRect' | 'fillRoundRect'
type ShapeCommand = { type: 'shape'; method: ShapeMethod; args: number[] }
export type NativeTextCommand = { type: 'text'; text: string; x: number; y: number; font: DeviceGuiFontId;
  scale: number; foreground: DevicePixelColor; background: DevicePixelColor; raster: DevicePixelGfx['fontRasterMode'] }
/** A one-pixel weight pass is paint, never another logical word/value. */
export function isNativeTextOverprint(current: NativeTextCommand, previous?: NativeTextCommand) {
  return Boolean(previous && current.text===previous.text && current.y===previous.y && current.x===previous.x+1
    && current.font===previous.font && current.scale===previous.scale
    && current.foreground===previous.foreground && current.background===previous.background)
}
export interface NativeScenePart { element: NativeElement; commands: Array<NativeTextCommand | ShapeCommand> }
export interface NativeScene {
  parts: NativeScenePart[]
  overrides?: Record<string, Record<string, number | boolean>>
}

export function calendarTextRole(text: string): CalendarTextRole {
  return /^\d{1,2}$/.test(text) ? 'day' : /^[一二三四五六日天]$/.test(text) ? 'weekday' : /^[班休]$/.test(text) ? 'holiday' : 'lunar'
}
interface Capture { scene: NativeScene; active?: NativeScenePart; counts: Map<string, number> }
const captures = new WeakMap<DevicePixelGfx, Capture>()
const shapes = new Set<ShapeMethod>(['drawPixel', 'drawFastHLine', 'drawFastVLine', 'fillRect', 'drawRect',
  'drawLine', 'drawDottedLine', 'drawCircle', 'fillCircle', 'drawRoundRect', 'fillRoundRect'])

function classifyText(text: string) {
  if (/^离/.test(text) && /天$/.test(text)) return ['countdown', '倒计时']
  if (/^\[\d+周\]$/.test(text)) return ['week-number', '周数']
  if (/^\[/.test(text)) return ['bluetooth', '蓝牙名']
  if (/℃$/.test(text)) return ['temperature', '温度']
  if (/V$/.test(text)) return ['voltage', '电压']
  if (/^星期/.test(text)) return ['weekday', '星期']
  if (/^\d{4}年/.test(text)) return ['date', '日期']
  if (/年$/.test(text)) return ['lunar-year', '农历年与生肖']
  return ['lunar-date', '农历日期']
}

function newPart(capture: Capture, key: string, label: string, type: NativeElement['type'], source: DeviceGuiFrame) {
  const occurrence = capture.counts.get(key) || 0
  capture.counts.set(key, occurrence + 1)
  const { x, y, width, height } = source
  const part: NativeScenePart = { element: { key: `${key}:${occurrence}`, label, type, source: { x, y, width, height } }, commands: [] }
  capture.scene.parts.push(part)
  return part
}

/** Observe high-level GFX commands, never crop or store a rendered page bitmap. */
export function recordNativeScene(gfx: DevicePixelGfx, scene: NativeScene): DevicePixelGfx {
  const capture: Capture = { scene, counts: new Map() }
  const proxy = new Proxy(gfx, { get(target, property) {
    const value = Reflect.get(target, property)
    if (typeof value !== 'function') return value
    return (...args: unknown[]) => {
      let command: NativeTextCommand | ShapeCommand | undefined
      if (property === 'print') command = { type: 'text', text: String(args[0]), x: target.tx, y: target.ty,
        font: target.currentFontId, scale: target.fontScaleQ8, foreground: target.foreground,
        background: target.background, raster: target.fontRasterMode }
      else if (shapes.has(property as ShapeMethod)) command = { type: 'shape', method: property as ShapeMethod, args: args as number[] }
      // The initial power-area clear is redundant on a fresh white canvas, not an editable layer.
      if (command && !(command.type === 'shape' && command.method === 'fillRect' && command.args[4] === 0 && !capture.active)) {
        let part = capture.active
        if (!part) {
          if (command.type === 'text') {
            const [key, label] = classifyText(command.text)
            part = newPart(capture, key!, label!, 'text', textFrame(target, [command]))
          } else {
            const [x, y, a = 1, b = 1] = command.args
            const width = command.method === 'drawFastVLine' ? 1 : command.method.includes('Line') && command.method !== 'drawFastHLine' ? a - x! + 1 : a
            const height = command.method === 'drawFastHLine' ? 1 : command.method === 'drawFastVLine' ? a : command.method.includes('Line') ? b - y! + 1 : b
            part = newPart(capture, 'rule', '分割线', 'graphic', { x: x!, y: y!, width: Math.max(1, width), height: Math.max(1, height) })
          }
        }
        part.commands.push(command)
        if (part.element.type === 'text') {
          const runs = part.commands.filter((item): item is NativeTextCommand => item.type === 'text')
          part.element.source = textFrame(target, runs)
          part.element.previewText = runs.filter((run,i)=>!isNativeTextOverprint(run,runs[i-1])).map((run) => run.text).join('')
          part.element.fontId = runs[0]?.font
        }
      }
      // Invoke with the real target: internal glyph strokes are NOT separate commands.
      return Reflect.apply(value, target, args)
    }
  } })
  captures.set(proxy, capture)
  return proxy
}

function textFrame(gfx: DevicePixelGfx, runs: NativeTextCommand[], clamp = true): DeviceGuiFrame {
  const font = gfx.currentFontId, scale = gfx.fontScaleQ8
  let left = Infinity, top = Infinity, right = 0, bottom = 0
  for (const run of runs) {
    gfx.setFont(run.font); gfx.setFontScale(run.scale)
    const ascent = Math.ceil((run.font === 'wqy9-lunar' ? 11 : run.font === 'wqy12-lunar' ? 15 : run.font.includes('18') ? 24 : 20) * run.scale / 256)
    left = Math.min(left, run.x); top = Math.min(top, run.y - ascent)
    right = Math.max(right, run.x + gfx.getUTF8Width(run.text)); bottom = Math.max(bottom, run.y + Math.ceil(3 * run.scale / 256))
  }
  gfx.setFont(font); gfx.setFontScale(scale); gfx.setFontMode(1)
  if (!clamp) return { x: left, y: top, width: Math.max(1, right - left), height: Math.max(1, bottom - top) }
  const x = Math.max(0, left), y = Math.max(0, top)
  return { x, y, width: Math.max(1, Math.min(gfx.frame.width, right) - x), height: Math.max(1, Math.min(gfx.frame.height, bottom) - y) }
}

export function nativeTextGroup(gfx: DevicePixelGfx, text: string, draw: () => void) {
  const capture = captures.get(gfx)
  if (!capture || capture.active) { draw(); return }
  const [key, label] = classifyText(text)
  const part = newPart(capture, key!, label!, 'text', { x: 0, y: 0, width: 1, height: 1 })
  capture.active = part
  try { draw() } finally {
    capture.active = undefined
    if (!part.commands.length) {
      capture.scene.parts.pop()
      capture.counts.set(key!, (capture.counts.get(key!) || 1) - 1)
    }
  }
}

export function nativeSceneOptions(gfx: DevicePixelGfx, key: string) { return captures.get(gfx)?.scene.overrides?.[`${key}:0`] || {} }

export function nativeNamedText(gfx: DevicePixelGfx, key: string, label: string, draw: () => void) {
  if (!nativeSceneGroup(gfx, key, label, 'text', { x: 0, y: 0, width: 1, height: 1 }, draw)) draw()
}

/** Returns true when the callback was executed as one independent semantic control. */
export function nativeSceneGroup(gfx: DevicePixelGfx, key: string, label: string, type: NativeElement['type'],
  source: DeviceGuiFrame, draw: () => void, options?: NativeElement['options'], protectedMark = false) {
  const capture = captures.get(gfx)
  if (!capture || capture.active) return false
  const part = newPart(capture, key, label, type, source)
  part.element.options = options
  part.element.protected = protectedMark
  capture.active = part
  try { draw() } finally { capture.active = undefined }
  return true
}

export function drawNativeScenePart(gfx: DevicePixelGfx, part: NativeScenePart, frame: DeviceGuiFrame, overrides: NativeElement, fixedText?: string) {
  if (overrides.type === 'clock' && overrides.resized) {
    const source = part.element.source
    const raster = new DevicePixelGfx(source.width, source.height)
    drawNativeScenePart(raster, part, { ...source, x: 0, y: 0 }, { ...overrides, resized: false })
    const scale = Math.min(frame.width / source.width, frame.height / source.height)
    const width = Math.max(1, Math.floor(source.width * scale)), height = Math.max(1, Math.floor(source.height * scale))
    const left = frame.x + Math.floor((frame.width - width) / 2), top = frame.y + Math.floor((frame.height - height) / 2)
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const color = raster.frame.pixels[Math.floor(y * source.height / height) * source.width + Math.floor(x * source.width / width)] as DevicePixelColor
      if (color) gfx.drawPixel(left + x, top + y, color)
    }
    return
  }
  const dx = frame.x - part.element.source.x, dy = frame.y - part.element.source.y
  const runs = part.commands.filter((command): command is NativeTextCommand => command.type === 'text')
  let cursor: number | undefined
  for (const command of part.commands) {
    if (command.type === 'text') {
      const localFont = overrides.type === 'calendar' ? overrides.calendarFonts?.[calendarTextRole(command.text)] : undefined
      const custom = overrides.type === 'text' && (overrides.fontOverride || overrides.scaleQ8 || overrides.fixedText)
      if (overrides.fixedText && command !== runs[0]) continue
      gfx.fontRasterMode = command.raster
      gfx.setFont(command.font); gfx.setFontScale(command.scale)
      const originalWidth = gfx.getUTF8Width(command.text)
      const originalHeight = gfx.getFontHeight()
      gfx.setFont(localFont ?? overrides.fontOverride ?? command.font)
      gfx.setFontScale(overrides.scaleQ8 ?? command.scale)
      gfx.setFontMode(1); gfx.setTextColor(overrides.colorOverride ?? command.foreground, command.background)
      if (overrides.type === 'calendar' && (localFont || overrides.fontOverride)) {
        // Each run keeps its own center; never concatenate independent calendar cells.
        gfx.setCursor(command.x + dx + Math.trunc((originalWidth - gfx.getUTF8Width(command.text)) / 2),
          command.y + dy + Math.trunc((gfx.getFontHeight() - originalHeight) / 2))
        gfx.print(command.text)
        continue
      }
      const x = cursor ?? command.x + dx
      gfx.setCursor(custom ? x : command.x + dx, command.y + dy)
      gfx.print(overrides.fixedText ? fixedText ?? '' : command.text)
      if (custom) cursor = gfx.tx + 1
    } else {
      const args = [...command.args]
      args[0]! += dx; args[1]! += dy
      if (command.method === 'drawLine' || command.method === 'drawDottedLine') { args[2]! += dx; args[3]! += dy }
      const method = gfx[command.method] as (...values: number[]) => void
      method.apply(gfx, args)
    }
  }
}

/** Reuse the same replay/metrics for an edited text's hit box, preserving its baseline. */
export function nativeElementBounds(part: NativeScenePart, element: NativeElement, text: string | undefined, width: number, height: number) {
  if (element.type === 'calendar' || element.key.startsWith('qr:')) return part.element.source
  // Measure isolated painted pixels, not font advances or a reserved container.
  // The padding also keeps edited glyphs outside the page visible to validation.
  const padding = 64
  const gfx = new DevicePixelGfx(width + padding * 2, height + padding * 2)
  drawNativeScenePart(gfx, part, { ...part.element.source, x: part.element.source.x + padding, y: part.element.source.y + padding }, { ...element, resized: false }, text)
  let left = gfx.frame.width, top = gfx.frame.height, right = -1, bottom = -1
  for (let y = 0; y < gfx.frame.height; y++) for (let x = 0; x < gfx.frame.width; x++) if (gfx.frame.pixels[y * gfx.frame.width + x]) {
    left = Math.min(left, x); top = Math.min(top, y); right = Math.max(right, x); bottom = Math.max(bottom, y)
  }
  return right < 0 ? { ...part.element.source, width: 1, height: 1 }
    : { x: left - padding, y: top - padding, width: right - left + 1, height: bottom - top + 1 }
}
