import { createQrMatrix, INKTAG_DEVICE_QR_COMPACT_VALUE, packQrMatrix } from '../qr/qr'
import { INKTAG_FIRMWARE_VERSION } from '../generated/version'
import {
  DEVICE_BLACK,
  DEVICE_RED,
  DEVICE_WHITE,
  DevicePixelGfx,
  type DevicePixelColor,
  type DevicePixelFrame,
} from './devicePixelGfx'
import {
  firmwareDayOfWeek,
  firmwareIsoWeek,
  firmwareJieQiDate,
  firmwareMonthMaxDays,
  firmwareSolarToLunar,
  firmwareTransformTime,
  jieQiStrings,
  lunarBranchStrings,
  lunarDateStrings,
  lunarDayStrings,
  lunarMonthLeapStrings,
  lunarMonthStrings,
  lunarStemStrings,
  lunarZodiacStrings,
  type FirmwareLunarDate,
  type FirmwareTime,
} from './lunarFirmware'
import {
  drawDevicePowerStatus,
  drawDeviceQr,
  drawFittedDeviceText,
  drawInkTagBrandTitle,
  drawInkTagEditionBadge,
  drawSevenSegmentNumber as drawSevenSegmentComponent,
} from './deviceGuiComponents'
import type { DeviceGuiFontId, DeviceGuiFrame } from './model'
import { nativeNamedText, nativeSceneGroup, nativeSceneOptions, nativeTextGroup, recordNativeScene, type NativeScene } from './nativeScene'

export type NativePageKind = 'device-info' | 'calendar' | 'clock' | 'low-battery'
export type NativeLayoutRevision = 'v1'
export type NativeEdition = 'base' | 'pro'
export const nativeLayoutRevisions: readonly NativeLayoutRevision[] = ['v1']
export const currentNativeLayoutRevision: NativeLayoutRevision = 'v1'

import { resolveV1Countdown, type NativeCountdown } from './countdownData'
export { resolveV1Countdown, type NativeCountdown } from './countdownData'

export interface NativePageRenderOptions {
  /** Web-only semantic editor extraction; no changes to the firmware protocol. */
  scene?: NativeScene
  width: number
  height: number
  page: NativePageKind
  /** The firmware protocol enum: 0/1=100%, 2=150%, 3=200%, 4=125%, 5=175%. */
  nativeUiScale?: number
  timestamp?: number
  weekStart?: number
  temperature?: number
  voltage?: number
  bluetoothName?: string
  /** Packed firmware matrix; the renderer supplies its four-module quiet zone. */
  deviceInfoQr?: { size: number; data: Uint8Array }
  activated?: boolean
  boardName?: string
  boardNameShort?: string
  screenInfo?: string
  colorCount?: number
  defaultMark?: boolean
  edition?: NativeEdition
  /** Effective device-side Pro setting. base callers must never set this. */
  brandHidden?: boolean
  deviceName?: string
  /** Exact reported MCU; never infer it from a board/product alias. */
  mcuName?: string
  firmwareVersion?: string
  /** V1 wide calendar/clock only. Missing, invalid or expired targets fall back to a solar term. */
  countdown?: NativeCountdown
  /** Current product layout revision; retired revisions are rejected. */
  layoutRevision?: NativeLayoutRevision
}

const div = (value: number, divisor: number) => Math.trunc(value / divisor)
const clamp = (value: number, minimum: number, maximum: number) => Math.max(minimum, Math.min(maximum, value))

interface NativePageData {
  page: NativePageKind
  width: number
  height: number
  color: number
  nativeUiScale: number
  weekStart: number
  temperature: number
  voltage: number
  bluetoothName: string
  deviceInfoQr?: { size: number; data: Uint8Array }
  activated: boolean
  boardName: string
  boardNameShort?: string
  screenInfo: string
  edition: NativeEdition
  brandHidden: boolean
  deviceName: string
  mcuName: string
  firmwareVersion: string
  countdown?: ReturnType<typeof resolveV1Countdown>
}

const scaleQ8 = (data: NativePageData) => {
  switch (data.nativeUiScale) {
    case 2: return 384
    case 3: return 512
    case 4: return 320
    case 5: return 448
    default: return 256
  }
}
const largeLayout = (data: NativePageData) => data.height >= 400
const v1ShowsVoltage = (data: Pick<NativePageData, 'width' | 'height'>) => v1LayoutPresetForSize(data.width, data.height).showVoltage
const bwOnly = (data: NativePageData) => data.color === 1

const v1DeviceQr = packQrMatrix(createQrMatrix(INKTAG_DEVICE_QR_COMPACT_VALUE, 21, 'L'))

const deviceInfoColorName = (data: NativePageData) => data.color === 1 ? 'BW' : data.color === 2 ? 'BWR' : data.color === 3 ? 'BWRY' : data.color === 10 ? 'E6' : 'COLOR'
const deviceInfoScreenToken = (value: string) => value.split(/[ /]/, 1)[0]!.slice(0, 11)

function deviceInfoFitText(gfx: DevicePixelGfx, value: string, maximumWidth: number) {
  const characters = Array.from(value)
  while (characters.length > 0 && gfx.getUTF8Width(characters.join('')) > maximumWidth) characters.pop()
  return characters.join('')
}

interface Festival { month: number; day: number; name: string }
const festivals: readonly Festival[] = [
  { month: 1, day: 1, name: '元旦节' }, { month: 2, day: 14, name: '情人节' },
  { month: 3, day: 8, name: '妇女节' }, { month: 3, day: 12, name: '植树节' },
  { month: 4, day: 1, name: '愚人节' }, { month: 5, day: 1, name: '劳动节' },
  { month: 5, day: 4, name: '青年节' }, { month: 6, day: 1, name: '儿童节' },
  { month: 7, day: 1, name: '建党节' }, { month: 8, day: 1, name: '建军节' },
  { month: 9, day: 10, name: '教师节' }, { month: 10, day: 1, name: '国庆节' },
  { month: 11, day: 1, name: '万圣节' }, { month: 12, day: 24, name: '平安夜' },
  { month: 12, day: 25, name: '圣诞节' },
]
const lunarFestivals: readonly Festival[] = [
  { month: 1, day: 1, name: '春节' }, { month: 1, day: 15, name: '元宵节' },
  { month: 2, day: 2, name: '龙抬头' }, { month: 5, day: 5, name: '端午节' },
  { month: 7, day: 7, name: '七夕节' }, { month: 7, day: 15, name: '中元节' },
  { month: 8, day: 15, name: '中秋节' }, { month: 9, day: 9, name: '重阳节' },
  { month: 10, day: 1, name: '寒衣节' }, { month: 12, day: 8, name: '腊八节' },
  { month: 12, day: 30, name: '除夕' },
]
const holidays2026 = [
  0x0101, 0x0102, 0x0103, 0x1104, 0x120e, 0x020f, 0x0210, 0x0211, 0x0212, 0x0213,
  0x0214, 0x0215, 0x0216, 0x0217, 0x121c, 0x0404, 0x0405, 0x0406, 0x0501, 0x0502,
  0x0503, 0x0504, 0x0505, 0x1509, 0x0613, 0x0614, 0x0615, 0x0919, 0x091a, 0x091b,
  0x1914, 0x0a01, 0x0a02, 0x0a03, 0x0a04, 0x0a05, 0x0a06, 0x0a07, 0x1a0a,
] as const

function getHoliday(year: number, month: number, day: number) {
  if (year !== 2026) return undefined
  const entry = holidays2026.find((value) => ((value >> 8) & 0xf) === month && (value & 0xff) === day)
  return entry === undefined ? undefined : { work: ((entry >> 12) & 0xf) > 0 }
}

function nextDay(year: number, month: number, day: number) {
  day += 1
  if (day > firmwareMonthMaxDays(year, month)) {
    day = 1
    month += 1
    if (month > 12) { month = 1; year += 1 }
  }
  return { year, month, day }
}

function getFestival(
  year: number,
  month: number,
  day: number,
  weekday: number,
  lunar: FirmwareLunarDate,
) {
  const lunarFestival = lunarFestivals.find((item) => item.month === lunar.month && item.day === lunar.day)
  if (lunarFestival) return lunarFestival.name
  if (lunar.month === 12 && lunar.day === 29) {
    const next = nextDay(year, month, day)
    const nextLunar = firmwareSolarToLunar(next.year, next.month, next.day)
    if (nextLunar.month === 1 && nextLunar.day === 1) return '除夕'
  }
  if (month === 5 && weekday === 0 && day >= 8 && day <= 14) return '母亲节'
  if (month === 6 && weekday === 0 && day >= 15 && day <= 21) return '父亲节'
  if (month === 11 && weekday === 4 && day >= 22 && day <= 28) return '感恩节'
  const solarFestival = festivals.find((item) => item.month === month && item.day === day)
  if (solarFestival) return solarFestival.name
  const solarTerm = firmwareJieQiDate(year, month, day)
  if (solarTerm?.date === day) return jieQiStrings[solarTerm.index] + (solarTerm.index === 6 ? '节' : '')
  return undefined
}

function drawSevenSegmentNumber(
  gfx: DevicePixelGfx,
  number: number,
  xLocation: number,
  yLocation: number,
  characterSize: number,
  foreground: DevicePixelColor,
  background: DevicePixelColor,
  digitCount: number,
  heightSize = characterSize,
) {
  drawSevenSegmentComponent(
    gfx,
    number,
    xLocation,
    yLocation,
    characterSize,
    foreground,
    background,
    digitCount,
    heightSize,
  )
}

function drawTimeStyled(
  gfx: DevicePixelGfx,
  time: FirmwareTime,
  x: number,
  y: number,
  characterSize: number,
  digits: number,
  digitColor: DevicePixelColor,
  colonColor: DevicePixelColor,
  colonPadding = 0,
  heightSize = characterSize,
) {
  drawSevenSegmentNumber(gfx, time.hour, x, y, characterSize, digitColor, DEVICE_WHITE, digits, heightSize)
  x += digits * (11 * characterSize + 2) + colonPadding
  gfx.fillRect(x, y + div(9 * heightSize, 2) + 1, 2 * characterSize, 2 * characterSize, colonColor)
  gfx.fillRect(x, y + div(27 * heightSize, 2) + 3, 2 * characterSize, 2 * characterSize, colonColor)
  x += 4 * characterSize + colonPadding
  drawSevenSegmentNumber(gfx, time.minute, x, y, characterSize, digitColor, DEVICE_WHITE, digits, heightSize)
}

interface V1TextStyle {
  boldNumbers?: boolean
  baselineY?: number
  gap?: number
  spanOffsets?: readonly number[]
  glyphRasterMode?: DevicePixelGfx['fontRasterMode']
  font?: DeviceGuiFontId
  scale?: number
  minimumScale?: number
  color?: DevicePixelColor
  background?: DevicePixelColor
  align?: 'left' | 'center' | 'right'
}

function drawV1Text(
  gfx: DevicePixelGfx,
  x: number,
  y: number,
  width: number,
  height: number,
  text: string,
  style: V1TextStyle = {},
) {
  const face = v1TextFace(gfx, text, style, width, height)
  const raster = gfx.fontRasterMode
  if (style.glyphRasterMode) gfx.fontRasterMode = style.glyphRasterMode
  const result = drawFittedDeviceText(gfx, { x, y, width: Math.max(1, width), height: Math.max(1, height) }, text, {
    fontId: face.font,
    fontScaleQ8: face.scale,
    minimumScaleQ8: face.minimumScale,
    baselineY: style.baselineY ?? (gfx.frame.width < 350 && /^[\x20-\x7e]+$/.test(text) ? y + div(height - 8, 2) + 8 : undefined),
    foreground: style.color ?? DEVICE_BLACK,
    background: style.background ?? DEVICE_WHITE,
    align: style.align,
  })
  gfx.fontRasterMode = raster
  return result
}

const V1_MIN_META_SCALE = 160
const V1_NATIVE_TEXT_SCALE = 256
const V1_CENTER_HANZI_BASELINE = -2
const V1_BODY_FONT: DeviceGuiFontId = 'wqy9-lunar'
const V1_HEADING_FONT: DeviceGuiFontId = 'wqy12-lunar'

/** Prefer the larger regular master before scaling the 9-point bitmap. */
function v1TextFace(gfx: DevicePixelGfx, text: string, style: V1TextStyle, width = Infinity, height = Infinity) {
  let font = style.font ?? V1_BODY_FONT
  let scale = style.scale ?? V1_NATIVE_TEXT_SCALE
  let minimumScale = style.minimumScale ?? 96
  if (font === 'helv-bold-14-num' || font === 'helv-bold-18-num') return {font, scale, minimumScale}
  if (gfx.frame.width === 200 && gfx.frame.height === 200 && font === V1_HEADING_FONT && (scale === 224 || scale === 240)) {
    gfx.setFont(font); gfx.setFontScale(scale)
    if (gfx.getUTF8Width(text) + 1 <= width && gfx.getFontHeight() <= height) return {font, scale, minimumScale:scale}
  }
  if (gfx.frame.width <= 500) {
    // Native masters first. Only genuinely undersized cells may shrink WQY9.
    gfx.setFont(V1_HEADING_FONT); gfx.setFontScale(256)
    const heading = (font === V1_HEADING_FONT || scale >= 320) && gfx.supportsText(text)
      && gfx.getUTF8Width(text) <= width && gfx.getFontHeight() <= height
    font = heading ? V1_HEADING_FONT : V1_BODY_FONT
    gfx.setFont(font); gfx.setFontScale(256)
    scale = 256
    if (gfx.getUTF8Width(text) <= width && gfx.getFontHeight() <= height) minimumScale = 256
    return {font, scale, minimumScale: Math.min(minimumScale, 256)}
  }
  if (font === V1_BODY_FONT && scale >= 320) {
    gfx.setFont(V1_HEADING_FONT)
    if (!gfx.supportsText(text)) return {font, scale, minimumScale}
    font = V1_HEADING_FONT
    scale = div(scale * 3 + 2, 4)
    if (Math.abs(scale - 256) <= 8) scale = 256
    minimumScale = div(minimumScale * 3, 4)
  }
  return { font, scale, minimumScale }
}

/** Display metadata only: never infer a short name from an internal board ID. */
export function v1BoardDisplayName(gfx: DevicePixelGfx, longName: string, shortName: string | undefined,
  width: number, height: number, scale = 256, minimumScale = 160) {
  const name = longName || 'BOARD'
  if (!shortName) return name
  const face = v1TextFace(gfx, name, { scale, minimumScale }, width, height)
  gfx.setFont(face.font); gfx.setFontScale(face.scale)
  return gfx.getUTF8Width(name) > width || gfx.getFontHeight() > height ? shortName : name
}

export type V1LayoutFamily = 'compact-square' | 'compact-rect' | 'normal' | 'large'

export interface V1LayoutPreset {
  family: V1LayoutFamily
  geometryScaleQ8: number
  calendarLunarCells: boolean
  compactQr: boolean
  showVoltage: boolean
}

const V1_LAYOUT_PRESETS: Readonly<Record<string, V1LayoutPreset>> = {
  '152x152': { family: 'compact-square', geometryScaleQ8: 256, calendarLunarCells: false, compactQr: true, showVoltage: false },
  '200x200': { family: 'compact-square', geometryScaleQ8: 337, calendarLunarCells: false, compactQr: true, showVoltage: true },
  '212x104': { family: 'compact-rect', geometryScaleQ8: 256, calendarLunarCells: false, compactQr: true, showVoltage: false },
  '250x128': { family: 'compact-rect', geometryScaleQ8: 302, calendarLunarCells: false, compactQr: false, showVoltage: true },
  '296x152': { family: 'compact-rect', geometryScaleQ8: 256, calendarLunarCells: true, compactQr: false, showVoltage: true },
  '400x300': { family: 'normal', geometryScaleQ8: 256, calendarLunarCells: true, compactQr: false, showVoltage: true },
}

export function v1LayoutPresetForSize(width: number, height: number): V1LayoutPreset {
  const exact = V1_LAYOUT_PRESETS[`${Math.trunc(Math.max(width, height))}x${Math.trunc(Math.min(width, height))}`]
  if (exact) return exact
  const longSide = Math.max(width, height)
  const shortSide = Math.min(width, height)
  if (width < 300 || height < 300) {
    const family: V1LayoutFamily = longSide / shortSide <= 1.25 ? 'compact-square' : 'compact-rect'
    // The exact 152/200 presets above omit lunar cells; 240 shares this path.
    return { family, geometryScaleQ8: 256, calendarLunarCells: shortSide >= 152, compactQr: false, showVoltage: longSide >= 200 }
  }
  if (longSide <= 500) return { family: 'normal', geometryScaleQ8: 256, calendarLunarCells: true, compactQr: false, showVoltage: true }
  return { family: 'large', geometryScaleQ8: 256, calendarLunarCells: true, compactQr: false, showVoltage: true }
}

function v1ScaleQ8(data: NativePageData) {
  return scaleQ8(data)
}

interface V1InlineSpan {
  text: string
  color: DevicePixelColor
}

/** Mixed fonts share a baseline. Text stays native; only the missing WQY12 brackets use WQY9 at 150%. */
interface V1NativeRun extends V1InlineSpan { font: DeviceGuiFontId; gap?: number; scale?: number }
function v1NativeRuns(gfx: DevicePixelGfx, runs: readonly V1NativeRun[], x = 0, baseline?: number) {
  const start = x
  const week = runs.findIndex((run) => run.text === '[')
  const groups = week > 0 ? [runs.slice(0, week), runs.slice(week)] : [runs]
  for (const group of groups) nativeTextGroup(gfx, group.map((run) => run.text).join(''), () => {
   for (const run of group) {
    x += run.gap ?? 1
    gfx.setFont(run.font); gfx.setFontScale(run.scale ?? 256)
    if (baseline !== undefined) {
      gfx.setFontMode(1); gfx.setTextColor(run.color, DEVICE_WHITE)
      gfx.setCursor(x, baseline); gfx.print(run.text)
    }
    x += gfx.getUTF8Width(run.text)
   }
  })
  return x - start
}

/** Fit a caller-owned label on UTF-8 boundaries, without squeezing adjacent groups. */
function v1CountdownRuns(gfx: DevicePixelGfx, data: NativePageData, width: number): V1NativeRun[] {
  if (!data.countdown) return []
  const body = data.width > 500 ? V1_HEADING_FONT : V1_BODY_FONT
  const runs: V1NativeRun[] = [
    {text: '离', font: body, color: DEVICE_BLACK, gap: 0},
    {text: '', font: V1_HEADING_FONT, color: bwOnly(data) ? DEVICE_BLACK : DEVICE_RED, gap: 2, scale:data.width>500?320:256},
    {text: String(data.countdown.days), font: body, color: DEVICE_BLACK, gap: 3},
    {text: '天', font: body, color: DEVICE_BLACK},
  ]
  const available = width - v1NativeRuns(gfx, runs)
  gfx.setFont(V1_HEADING_FONT); gfx.setFontScale(runs[1]!.scale!)
  if (!gfx.supportsText(data.countdown.name)) { runs[1]!.font = V1_BODY_FONT; gfx.setFont(V1_BODY_FONT) }
  let bytes = 0
  for (const character of data.countdown.name) {
    const code = character.codePointAt(0)!
    bytes += code < 128 ? 1 : code < 2048 ? 2 : code < 65536 ? 3 : 4
    const visible = gfx.supportsText(character) ? character : '?'
    if (bytes > 63 || gfx.getUTF8Width(runs[1]!.text + visible) > available) break
    runs[1]!.text += visible
  }
  return runs
}

// A group boundary is geometry, not the font's unusually wide space glyph.
const v1SpanWidth = (gfx: DevicePixelGfx, text: string) => text === '  '
  ? Math.max(8, div(gfx.fontScaleQ8 * 8, 256)) : text === ' '
    ? Math.max(gfx.frame.width < 350 ? 4 : 2, div(gfx.fontScaleQ8 * 4, 256) - 2) : gfx.getUTF8Width(text)

/*
 * V1 typography contract:
 * - compact metadata, including footer BLE, stays native;
 * - wide calendar numerals reuse V0 Helvetica Bold, with native lunar masters;
 * - requested scale is bounded by each region, not a whole-page bitmap zoom.
 */

function drawV1InlineText(
  gfx: DevicePixelGfx,
  x: number,
  y: number,
  width: number,
  height: number,
  spans: readonly V1InlineSpan[],
  style: Pick<V1TextStyle, 'font' | 'scale' | 'minimumScale' | 'background' | 'align' | 'baselineY' | 'gap' | 'glyphRasterMode' | 'boldNumbers' | 'spanOffsets'> = {},
) {
  const gap = style.gap ?? 0
  const spacingWidth = style.spanOffsets?.reduce((sum,value)=>sum+value,0) ?? 0
  const face = v1TextFace(gfx, spans.map((span) => span.text).join(''), style, width - Math.max(0, spans.length - 1) * gap - spacingWidth, height)
  const font = face.font
  const minimum = face.minimumScale
  let scale = Math.max(minimum, face.scale)
  gfx.setFont(font)
  gfx.setFontScale(scale)
  const textWidth = () => spans.reduce((sum, span) => sum + v1SpanWidth(gfx, span.text), 0) + Math.max(0, spans.length - 1) * gap + spacingWidth
  while (scale > minimum && (textWidth() > width || gfx.getFontHeight() > height)) {
    scale = Math.max(minimum, scale - 16)
    gfx.setFontScale(scale)
  }
  const totalWidth = textWidth()
  let cursorX = style.align === 'right'
    ? x + width - totalWidth
    : style.align === 'center'
      ? x + div(width - totalWidth, 2)
      : x
  const inkAscent = div((font === V1_HEADING_FONT ? 14 : 10) * scale, 256)
  const baselineY = style.baselineY === V1_CENTER_HANZI_BASELINE
    ? y + div(height - inkAscent - Math.ceil(scale / 256), 2) + inkAscent
    : style.baselineY ?? y + div(height - gfx.getFontHeight(), 2) + gfx.getFontAscent()
  const background = style.background ?? DEVICE_WHITE
  gfx.setFontMode(1)
  const groups: V1InlineSpan[][] = [[]]
  for (const span of spans) { if (/^ {1,2}$/.test(span.text)) groups.push([span]); else groups[groups.length - 1]!.push(span) }
  let spanIndex = 0
  for (const group of groups) nativeTextGroup(gfx, group.filter((span) => !/^ {1,2}$/.test(span.text)).map((span) => span.text).join(''), () => {
    for (const span of group) {
      cursorX += style.spanOffsets?.[spanIndex++] ?? 0
      gfx.setTextColor(span.color, background)
      gfx.setCursor(cursorX, baselineY)
      if (!/^ {1,2}$/.test(span.text)) {
        const rasterMode = gfx.fontRasterMode
        gfx.fontRasterMode = style.glyphRasterMode ?? rasterMode
        gfx.print(span.text)
        if (style.boldNumbers && /^\d/.test(span.text)) {
          gfx.setCursor(cursorX + 1, baselineY)
          gfx.print(span.text)
        }
        gfx.fontRasterMode = rasterMode
      }
      cursorX += v1SpanWidth(gfx, span.text) + gap
    }
  })
}

function v1DateSpans(time: FirmwareTime, data: NativePageData, includeYear: boolean, includeDay: boolean): V1InlineSpan[] {
  const accent: DevicePixelColor = bwOnly(data) ? DEVICE_BLACK : DEVICE_RED
  const spans: V1InlineSpan[] = []
  if (includeYear) {
    spans.push({ text: String(time.year), color: accent }, { text: '年', color: DEVICE_BLACK })
  }
  spans.push({ text: String(time.month), color: accent }, { text: '月', color: DEVICE_BLACK })
  if (includeDay) {
    spans.push({ text: String(time.day), color: accent }, { text: '日', color: DEVICE_BLACK })
  }
  return spans
}

function v1LunarYearSpans(lunar: FirmwareLunarDate, data: NativePageData): V1InlineSpan[] {
  const accent: DevicePixelColor = bwOnly(data) ? DEVICE_BLACK : DEVICE_RED
  return [
    { text: `${lunarStemStrings[lunar.year % 10]}${lunarBranchStrings[lunar.year % 12]}`, color: DEVICE_BLACK },
    { text: lunarZodiacStrings[lunar.year % 12]!, color: accent },
    { text: '年', color: DEVICE_BLACK },
  ]
}

function v1LunarDateSpans(lunar: FirmwareLunarDate, data: NativePageData): V1InlineSpan[] {
  const accent = bwOnly(data) ? DEVICE_BLACK : DEVICE_RED
  const month = `${lunarMonthLeapStrings[lunar.isLeap]}${lunarMonthStrings[lunar.month]}`.trim()
  return [{ text: month.replace(/月$/, ''), color: accent }, { text: '月', color: DEVICE_BLACK }, { text: lunarDateStrings[lunar.day]!, color: accent }]
}

function v1HeaderDate(gfx: DevicePixelGfx, time: FirmwareTime, data: NativePageData, width: number, weekday = false) {
  const spans = v1DateSpans(time, data, true, true)
  if (weekday) spans.push({text: ' ', color: DEVICE_BLACK}, {text: '星期', color: DEVICE_BLACK},
    {text: lunarDayStrings[time.weekday]!, color: bwOnly(data) ? DEVICE_BLACK : DEVICE_RED})
  gfx.setFont(V1_BODY_FONT); gfx.setFontScale(256)
  const fits = spans.reduce((sum, span) => sum + v1SpanWidth(gfx, span.text), spans.length - 1) <= width
  return {spans, gap: data.width < 350 && !fits ? 0 : 1}
}

function v1CompactLunarSpans(
  gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData,
  width: number, scale = V1_NATIVE_TEXT_SCALE, prefix: 'weekday' | 'bluetooth' = 'weekday', font = V1_BODY_FONT,
): V1InlineSpan[] {
  const weekday: V1InlineSpan[] = [{text: '星期', color: DEVICE_BLACK},
    {text: lunarDayStrings[time.weekday]!, color: bwOnly(data) ? DEVICE_BLACK : DEVICE_RED}]
  const separator: V1InlineSpan = { text: !v1LayoutPresetForSize(data.width, data.height).family.startsWith('compact-') ? '  ' : ' ', color: DEVICE_BLACK }
  const date = v1LunarDateSpans(lunar, data)
  const year = [...v1LunarYearSpans(lunar, data), separator, ...date]
  const alternatives = [[...weekday, separator, ...year], [...weekday, separator, ...date], [...weekday, separator, date[2]!], weekday]
  gfx.setFont(font)
  gfx.setFontScale(scale)
  const spanWidth = (spans: V1InlineSpan[]) => spans.reduce((sum, span) => sum + v1SpanWidth(gfx, span.text), 0)
  const selected = alternatives.find((spans) => spanWidth(spans) <= width) ?? alternatives.at(-1)!
  if (prefix !== 'bluetooth') return selected
  // Preserve the old lunar detail level; shorten BLE before losing lunar text.
  const space = selected.length > 2 ? spanWidth(selected.slice(3)) + 6 : 0
  return [{text: v1BluetoothLabel(gfx, data, width - space, 256), color: DEVICE_BLACK}, ...selected.slice(2)]
}

function drawV1EditionBadge(
  gfx: DevicePixelGfx,
  data: NativePageData,
  x: number,
  y: number,
  width: number,
  height: number,
  align: 'left' | 'center' | 'right' = 'right',
) {
  if (data.brandHidden && data.edition === 'pro') return
  if (nativeSceneGroup(gfx, 'edition', 'InkTag 标识', 'graphic', { x, y, width, height },
    () => drawV1EditionBadge(gfx, data, x, y, width, height, align), undefined, data.edition !== 'pro')) return
  drawInkTagEditionBadge(gfx, { x, y, width, height }, {
    edition: data.edition,
    colorCount: data.color,
    align,
  })
}

function v1SquareTextScale(data:{width:number;height:number;page?:NativePageKind}) {
  if(data.width!==data.height || (data.page!=='calendar' && data.page!=='clock')) return 256
  return data.width===200 ? 320 : data.width===240 ? 384 : 256
}
function v1CompactFooterHeight(data:NativePageData) { return data.width===200 && v1SquareTextScale(data)===320 ? 23:18 }

export function v1PowerStatusFrame(data: { width: number; height: number; nativeUiScale: number; temperature?: number; page?:NativePageKind }): DeviceGuiFrame {
  const compact = v1LayoutPresetForSize(data.width, data.height).family.startsWith('compact-')
  const inset = compact ? 2 : clamp(div(Math.min(data.width, data.height), 24), 10, 28)
  const temperatureWidth = String(data.temperature ?? 24).length * 7 + 13
  const width = compact ? v1SquareTextScale(data)>256 && data.width !== 200
    ? String(data.temperature ?? 24).length*8+(v1ShowsVoltage(data)?73:41)
    : temperatureWidth + (v1ShowsVoltage(data) ? data.width <= 250 ? 46 : 48 : 16)
    : clamp(div(data.width, 4), 100, 160)
  const sharedHeader = data.page === 'calendar' || data.page === 'clock'
  const height = sharedHeader && data.width === 200 && data.height === 200 ? 26
    : sharedHeader && data.width === 400 && data.height === 300 ? 23
    : compact ? div(18*v1SquareTextScale(data)+128,256) : clamp(div(data.height, 12), 25, 48)
  return { x: data.width - inset - width, y: 0, width, height }
}

function drawV1PowerStatus(gfx: DevicePixelGfx, data: NativePageData) {
  drawDevicePowerStatus(gfx, v1PowerStatusFrame(data), {
    temperature: data.temperature,
    voltageMillivolts: data.voltage,
    fontScaleQ8: data.width <= 500 ? v1SquareTextScale(data) : v1ScaleQ8(data),
    showVoltage: v1ShowsVoltage(data),
    batteryWarningColor: [2, 3, 10, 11].includes(data.color) ? DEVICE_RED : DEVICE_BLACK,
    temperatureFont: 'wqy9-lunar',
    baselineY: v1HeaderBaseline(data),
    labelGap: data.width <= 250 ? 2 : undefined,
  })
}

function v1HeaderBaseline(data: NativePageData) {
  const height = v1PowerStatusFrame(data).height
  const scale = Math.min(v1ScaleQ8(data), 384)
  return data.width > 500 ? Math.max(div(height - 1 + div(10 * scale, 256), 2), 2 + div(16 * scale, 256)) : height - div(5*v1SquareTextScale(data)+128,256)
}

function drawV1CompactFooter(gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData) {
  const margin = 3, height = v1CompactFooterHeight(data), badge = 66
  const width = data.width - margin * 3 - badge
  const y = data.height - height
  gfx.drawFastHLine(margin, y, data.width - margin * 2, DEVICE_BLACK)
  const square = data.width === data.height
  const spans: V1InlineSpan[] = square ? [{text: v1BluetoothLabel(gfx, data, width, 256), color: DEVICE_BLACK}]
    : v1CompactLunarSpans(gfx, time, lunar, data, width, 256, 'bluetooth')
  // Native face selection preserves brackets and Latin glyphs without bitmap enlargement.
  if(square) drawV1InlineText(gfx,margin,y+1,width,height-1,spans,
    {font:V1_HEADING_FONT,scale:256,minimumScale:256,gap:0,align:'left',baselineY:V1_CENTER_HANZI_BASELINE})
  else drawV1InlineText(gfx, margin, y + 3, width, height - 5, spans,
    {scale: 256, minimumScale: 256, baselineY: y + 14})
  drawV1EditionBadge(gfx, data, data.width - margin - badge, y + 3, badge, height - 5)
}

export function v1FactoryBluetoothName(name: string) {
  return /^(?:IKT|InkTag)_((?:GR|OM|TL|NRF|DA)_[0-9A-F]{4})$/.exec(name)?.[1] ?? null
}

export function v1BluetoothDisplayLabel(gfx: DevicePixelGfx, name: string, width: number, scale = 192, brackets = true, compact = false) {
  gfx.setFont(V1_BODY_FONT)
  gfx.setFontScale(scale)
  name ||= 'InkTag'
  const factory = v1FactoryBluetoothName(name)
  const label = compact && factory ? factory.slice(-4) : name
  const bracketWidth = brackets ? gfx.getUTF8Width('[]') : 0
  const fitted = deviceInfoFitText(gfx, label, Math.max(0, width - bracketWidth))
  return brackets ? `[${fitted}]` : fitted
}

function v1BluetoothLabel(gfx: DevicePixelGfx, data: NativePageData, width: number, scale = 192, brackets = true,
  shortFactory = v1LayoutPresetForSize(data.width, data.height).family.startsWith('compact-')) {
  return v1BluetoothDisplayLabel(gfx, data.bluetoothName, width, scale, brackets, shortFactory)
}

function v1InfoTextScale(data: NativePageData) {
  return data.width < 350 ? 256 : Math.min(v1ScaleQ8(data), data.width >= 600 ? 448 : 320)
}

interface V1MonthGridOptions {
  x: number
  y: number
  width: number
  height: number
  bandedWeekdays?: boolean
  showLunar?: boolean
  gridLines?: boolean
  referenceWidth?: number
  referenceHeight?: number
}

/** Shared rounded boundaries keep all seven widths within one pixel. */
export function v1CalendarColumnBounds(width: number, column: number) {
  const x = div(width * column + 3, 7)
  return { x, width: div(width * (column + 1) + 3, 7) - x }
}

function drawV1MonthGrid(
  gfx: DevicePixelGfx,
  time: FirmwareTime,
  data: NativePageData,
  options: V1MonthGridOptions,
) {
  const controls = nativeSceneOptions(gfx, 'calendar')
  if (nativeSceneGroup(gfx, 'calendar', '日历网格', 'calendar', { ...options, ...controls },
    () => drawV1MonthGrid(gfx, time, { ...data, weekStart: Number(controls.weekStart ?? data.weekStart) }, { ...options, referenceWidth: options.width, referenceHeight: options.height, ...controls }),
    { bandedWeekdays: options.bandedWeekdays ?? false, showLunar: options.showLunar ?? false, gridLines: options.gridLines ?? false, weekStart: data.weekStart,
      minimumWidth: Math.min(options.width, 7 * (data.width < 350 ? options.showLunar ? 26 : 14 : data.width > 500 ? 44 : 34)),
      minimumHeight: Math.max(64, options.height - 12) })) return
  const accent = bwOnly(data) ? DEVICE_BLACK : DEVICE_RED
  const firstWeekday = firmwareDayOfWeek(time.month, 1, time.year)
  const firstIndex = (firstWeekday - data.weekStart + 7) % 7
  const maximumDays = firmwareMonthMaxDays(time.year, time.month)
  const rows = clamp(div(firstIndex + maximumDays + 6, 7), 5, 6)
  let headerHeight = data.width < 350 ? div(14*v1SquareTextScale(data)+128,256) : clamp(div(options.referenceHeight ?? options.height, options.showLunar ? 6 : 7), 15, largeLayout(data) ? 42 : 26)
  const tightLunar = data.width === 296 && data.height === 152 && rows === 6 && options.showLunar
  if (tightLunar) headerHeight = 12
  if (data.width === 400 && data.height === 300 && rows === 6) headerHeight = 22
  const daysY = headerHeight + (data.width < 350 && options.showLunar && !tightLunar ? 2 : 0)
  const daysHeight = options.height - daysY
  const cellHeight = div(daysHeight, rows)
  const textCellHeight = div((options.referenceHeight ?? options.height) - daysY, rows)
  const weekdayScale = data.width < 350 ? data.width===200&&data.height===200 ? 288:256 : options.bandedWeekdays ? Math.min(v1ScaleQ8(data), 512) : Math.min(v1ScaleQ8(data), 256)

  if (options.bandedWeekdays) {
    gfx.fillRoundRect(options.x, options.y, options.width, headerHeight, div(headerHeight, 2), DEVICE_BLACK)
  }

  for (let column = 0; column < 7; column += 1) {
    const weekday = (data.weekStart + column) % 7
    const weekend = weekday === 0 || weekday === 6
    const bounds = v1CalendarColumnBounds(options.width, column)
    const x = options.x + bounds.x
    const width = bounds.width
    if (options.bandedWeekdays) {
      const background = weekend ? accent : DEVICE_BLACK
      if (weekend && column === 0) {
        gfx.fillRoundRect(x, options.y, width, headerHeight, div(headerHeight, 2), background)
        gfx.fillRect(x + div(width, 2), options.y, width - div(width, 2), headerHeight, background)
      } else if (weekend && column === 6) {
        gfx.fillRoundRect(x, options.y, width, headerHeight, div(headerHeight, 2), background)
        gfx.fillRect(x, options.y, div(width, 2), headerHeight, background)
      } else if (weekend) {
        gfx.fillRect(x, options.y, width, headerHeight, background)
      }
      // Keep WQY9 counters open; the larger requests promote to WQY12.
      const bold = weekdayScale >= 320
      const text = lunarDayStrings[weekday]!
      drawV1InlineText(gfx, x, options.y, width - Number(bold), headerHeight - Number(bold), [{text, color: DEVICE_WHITE}], {
        align: 'center', background, scale: weekdayScale, minimumScale: V1_MIN_META_SCALE,
        baselineY: V1_CENTER_HANZI_BASELINE,
      })
      if (bold) {
        const left = gfx.tx - gfx.getUTF8Width(text), baseline = gfx.ty
        for (let offset = 1; offset < 4; offset++) {
          gfx.setCursor(left + (offset & 1), baseline + (offset >> 1))
          gfx.print(text)
        }
      }
    } else {
      drawV1Text(gfx, x, options.y, width, headerHeight - 1, lunarDayStrings[weekday]!, {
        align: 'center', color: weekend ? accent : DEVICE_BLACK, scale: weekdayScale, minimumScale: 128,
      })
    }
  }
  if (!options.bandedWeekdays) gfx.drawFastHLine(options.x, options.y + headerHeight - 1, options.width, DEVICE_BLACK)

  if (options.gridLines) {
    for (let column = 1; column < 7; column += 1) {
      const x = options.x + v1CalendarColumnBounds(options.width, column).x
      gfx.drawDottedLine(x, options.y + headerHeight, x, options.y + options.height - 1, DEVICE_BLACK, 1, 5)
    }
    for (let row = 1; row < rows; row += 1) {
      gfx.drawDottedLine(options.x, options.y + headerHeight + row * cellHeight, options.x + options.width - 1, options.y + headerHeight + row * cellHeight, DEVICE_BLACK, 1, 5)
    }
  }

  for (let day = 1; day <= maximumDays; day += 1) {
    const index = firstIndex + day - 1
    const column = index % 7
    const row = div(index, 7)
    const actualWeekday = (firstWeekday + day - 1) % 7
    const weekend = actualWeekday === 0 || actualWeekday === 6
    const bounds = v1CalendarColumnBounds(options.width, column)
    const x = options.x + bounds.x
    const rowTop = row * cellHeight
    const rowBottom = rowTop + cellHeight
    const y = options.y + daysY + rowTop
    const width = bounds.width
    const height = rowBottom - rowTop
    /*
     * Every row uses the same typographic height. Division remainders become
     * breathing room instead of silently enlarging the last row's glyphs.
     */
    const rowGap = data.width < 350 ? options.showLunar ? 1 : 0 : clamp(div(textCellHeight, 10), 2, 5)
    const compactCell = data.width < 300 || data.height < 300
    const contentHeight = Math.min(Math.max(7, textCellHeight - rowGap), compactCell && options.showLunar ? 21 : Infinity)
    const contentY = y + div(height - contentHeight, 2)
    const highlighted = day === time.day
    const foreground = highlighted ? DEVICE_WHITE : weekend ? accent : DEVICE_BLACK
    const background = highlighted ? accent : DEVICE_WHITE
    const insetX = data.width < 350 && options.showLunar ? 1 : clamp(div(width, 10), 1, 5)
    // Match shipping C's short-edge classification, including 240px squares.
    const insetY = compactCell ? 0 : clamp(div(height, 12), 1, 4)
    const showLunar = Boolean(options.showLunar)
    const holiday = getHoliday(time.year, time.month, day)
    /* Reserve a baseline directly below the numeral for statutory marks. */
    const markerSize = compactCell && height < 14 ? 2 : 3
    const markerBand = compactCell ? showLunar ? 1 : markerSize + 1 : 0
    const numeralHeight = tightLunar ? 8 : 9
    const lunarHeight = showLunar ? compactCell ? contentHeight - numeralHeight - markerBand : clamp(div(contentHeight, 3), 7, 18) : 0
    const numberHeight = compactCell ? showLunar ? numeralHeight : Math.max(8, contentHeight - markerBand) : Math.max(7, contentHeight - lunarHeight)
    const largeSquare = data.width === 200 && data.height === 200 && !showLunar
    const numberFont: DeviceGuiFontId = largeSquare ? 'wqy12-lunar' : compactCell ? 'wqy9-lunar' : 'helv-bold-14-num'
    const numberScale = largeSquare ? 240 : compactCell ? Math.min(v1ScaleQ8(data), showLunar && contentHeight < 28 ? 256 : 512) : Math.min(v1ScaleQ8(data), 384)
    const baselineY = largeSquare ? contentY + div(numberHeight - 10, 2) + 10
      : compactCell ? contentY + div(numberHeight - 8, 2) + 8 : undefined
    let selectedHeight=height-insetY*2
    let selectedInsetY=insetY
    if (compactCell) {
      let inkEnd=baselineY!-y
      if(showLunar) {
        const inkHeight=Math.min(11,Math.max(1,lunarHeight))
        inkEnd=contentY-y+numberHeight+markerBand+div(lunarHeight-inkHeight,2)+inkHeight
        selectedInsetY=Math.max(0,contentY-y-2)
      } else if(holiday) inkEnd+=(height<(largeSquare?20:12)?1:2)+markerSize
      selectedHeight=Math.min(height,Math.max(1,inkEnd+2))-selectedInsetY
    }
    if (highlighted) gfx.fillRoundRect(x+insetX,y+selectedInsetY,width-insetX*2,selectedHeight,
      compactCell?2:clamp(div(Math.min(width,height),5),2,5),accent)
    const numberWidth = options.referenceWidth ? v1CalendarColumnBounds(options.referenceWidth, column).width : width
    if (largeSquare) {
      const face = v1TextFace(gfx, String(day), {font:numberFont, scale:numberScale, minimumScale:256}, numberWidth-2, numberHeight)
      gfx.setFont(face.font); gfx.setFontScale(face.scale)
    }
    const numeral = drawV1Text(gfx, x + div(width - numberWidth, 2) + 1, contentY, numberWidth - 2, numberHeight, String(day), {
      font: numberFont,
      align: 'center',
      color: foreground,
      background,
      scale: numberScale,
      minimumScale: largeSquare ? 240 : compactCell ? 256 : 112,
      glyphRasterMode: largeSquare ? 'strokes' : undefined,
      baselineY,
    })
    if (holiday) {
      const markerColor = highlighted ? DEVICE_WHITE : holiday.work ? DEVICE_BLACK : accent
      if (compactCell) {
        const markerWidth = markerSize === 2 ? 2 : 4
        const markerInset = showLunar && ((data.width === 296 && data.height === 152) || (data.width === 240 && data.height === 240)) ? 1 : 0
        const markerX = showLunar ? x + width - insetX - markerWidth - 1 - markerInset : numeral.x + div(numeral.width - markerWidth, 2)
        const markerY = showLunar ? numeral.baselineY - 6 : numeral.baselineY + (height < (largeSquare ? 20:12) ? 1 : 2)
        if (markerSize === 2) gfx.fillRect(markerX, markerY, 2, 2, markerColor)
        else gfx.fillRoundRect(markerX, markerY, markerWidth, markerSize, 1, markerColor)
      } else {
        const markX = Math.min(x + width - 15, numeral.x + numeral.width + 2)
        const markBaseline = Math.max(y + 14, numeral.baselineY - gfx.getFontAscent() + 10)
        if (highlighted) {
          gfx.fillCircle(markX + 6, markBaseline - 5, 8, DEVICE_WHITE)
          gfx.drawCircle(markX + 6, markBaseline - 5, 8, accent)
        }
        drawV1Text(gfx, markX, y + insetY + 2, 14, 15, holiday.work ? '班' : '休', {
          color: holiday.work ? DEVICE_BLACK : accent, background: DEVICE_WHITE, scale: 256, minimumScale: 256, baselineY: markBaseline,
        })
      }
    }
    if (showLunar) {
      const lunar = firmwareSolarToLunar(time.year, time.month, day)
      const festival = getFestival(time.year, time.month, day, actualWeekday, lunar)
      let lunarText = festival ?? (lunar.day === 1
        ? `${lunarMonthLeapStrings[lunar.isLeap]}${lunarMonthStrings[lunar.month]}`
        : lunarDateStrings[lunar.day]!).trim()
      if (compactCell && Array.from(lunarText).length > 2) lunarText = Array.from(lunarText).slice(0, 2).join('')
      const lunarY = contentY + numberHeight + markerBand
      const lunarInset = 1
      if (compactCell) {
        // Calendar WQY9 ink is [-10, 1), with 13px advances per Hanzi.
        // Only six-row months shrink; coverage sampling keeps thin strokes.
        let scale = 256
        gfx.setFont(V1_BODY_FONT); gfx.setFontScale(scale)
        const inkHeight = () => Math.ceil(10 * scale / 256) + Math.ceil(scale / 256)
        while (scale > 128 && (inkHeight() > lunarHeight || gfx.getUTF8Width(lunarText) > width - lunarInset * 2)) {
          scale -= 16; gfx.setFontScale(scale)
        }
        gfx.setFontMode(1); gfx.setTextColor(highlighted ? DEVICE_WHITE : festival ? accent : foreground, background)
        gfx.setCursor(x + div(width - gfx.getUTF8Width(lunarText), 2), lunarY + div(lunarHeight - inkHeight(), 2) + Math.ceil(10 * scale / 256))
        const raster = gfx.fontRasterMode
        if (scale < 256) gfx.fontRasterMode = tightLunar ? 'strokes' : 'coverage'
        gfx.print(lunarText)
        gfx.fontRasterMode = raster
        continue
      }
      // Native lunar masters sit beside the numeral, not at the bottom of a tall cell.
      const lunarAscent = data.width > 500 ? 14 : 10
      gfx.setFont(data.width > 500 ? V1_HEADING_FONT : V1_BODY_FONT); gfx.setFontScale(256)
      gfx.setFontMode(1); gfx.setTextColor(highlighted ? DEVICE_WHITE : festival ? accent : foreground, background)
      gfx.setCursor(x + div(width - gfx.getUTF8Width(lunarText), 2),
        Math.min(numeral.baselineY + (data.width > 500 ? 5 : 3) + lunarAscent, y + height - insetY - 3))
      gfx.print(lunarText)
    }
  }
}

function drawV1CompactHeader(gfx: DevicePixelGfx, time: FirmwareTime, data: NativePageData) {
  const margin = 2, header = v1PowerStatusFrame(data).height
  const square = data.width === data.height
  const status = v1PowerStatusFrame(data)
  let dateWidth=status.x-margin-2
  const date = v1HeaderDate(gfx, time, data, dateWidth, !square)
  if ((square && data.width === 200)) {
    gfx.setFont(V1_BODY_FONT); gfx.setFontScale(256)
    const temperatureWidth = data.temperature <= -128 ? 0 : gfx.getUTF8Width(`${data.temperature}℃`)
    const tenths = div(data.voltage + 50, 100)
    const voltageWidth = gfx.getUTF8Width(`${div(tenths, 10)}.${tenths % 10}V`)
    const textLeft = status.x + status.width - clamp(div(status.width, 4), 16, 24) - 6 - voltageWidth - temperatureWidth
    dateWidth = Math.min(dateWidth, textLeft - margin - 2)
    gfx.setFont(V1_HEADING_FONT); gfx.setFontScale(224)
    const textWidth = date.spans.reduce((sum,span)=>sum+v1SpanWidth(gfx,span.text),0)+1
    const gaps = date.spans.length - 1
    date.gap = textWidth + gaps * 3 <= dateWidth ? 3 : textWidth + gaps * 2 <= dateWidth ? 2 : 1
  }
  drawV1InlineText(gfx, margin, 0, dateWidth, header, date.spans, {
    font:(square && data.width === 200) ? V1_HEADING_FONT : V1_BODY_FONT,align:'left',
    glyphRasterMode:(square && data.width === 200) ? 'strokes' : undefined,
    scale: (square && data.width === 200) ? 224 : v1SquareTextScale(data), minimumScale: 224,
    gap: date.gap, baselineY: v1HeaderBaseline(data),
    spanOffsets:(square && data.width === 152) ? [0,0,0,-2,0,0] : undefined,
    boldNumbers: v1SquareTextScale(data)>256,
  })
  gfx.drawFastHLine(margin, header - 1, data.width - margin * 2, DEVICE_BLACK)
}

function drawV1CalendarSquare(gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData) {
  const margin = 2, header = v1PowerStatusFrame(data).height, footer = v1CompactFooterHeight(data)
  drawV1CompactHeader(gfx, time, data)
  drawV1MonthGrid(gfx, time, data, {
    x: margin, y: header + 2, width: data.width - margin * 2,
    height: data.height - header - 2 - footer, bandedWeekdays: true,
    showLunar: v1LayoutPresetForSize(data.width, data.height).calendarLunarCells,
  })
  drawV1CompactFooter(gfx, time, lunar, data)
}

function drawV1CalendarCompactRect(gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData) {
  const preset = v1LayoutPresetForSize(data.width, data.height)
  const railWidth = preset.calendarLunarCells ? 90 : Math.max(74, v1PowerStatusFrame(data).width + 1)
  const margin = 3, gap = 4, gridY = 2, footer = 18
  const gridWidth = data.width - margin * 2 - railWidth - gap
  const railX = margin + gridWidth + gap, bottom = data.height - footer
  const header = v1PowerStatusFrame(data).height
  drawV1MonthGrid(gfx, time, data, {
    x: margin, y: gridY, width: gridWidth, height: bottom - gridY,
    bandedWeekdays: true, showLunar: preset.calendarLunarCells,
  })
  gfx.drawFastVLine(railX - 2, 2, bottom - 1, DEVICE_BLACK)
  gfx.drawFastHLine(railX - 2, header - 1, data.width - margin - (railX - 2) + 1, DEVICE_BLACK)
  const monthHeight = preset.calendarLunarCells ? 24 : 20
  const bluetoothHeight = monthHeight - 2
  const monthY = bottom - bluetoothHeight - monthHeight
  const dayHeight = monthY - header
  const daySize = Math.max(1, Math.min(2, div(dayHeight - 4, 20), div(railWidth - 4, 20)))
  const daySide = 20 * daySize + 4
  const drawDay = () => drawSevenSegmentNumber(gfx, time.day, railX + div(railWidth - daySide, 2),
    header + div(dayHeight - (20 * daySize + 2), 2), daySize,
    bwOnly(data) ? DEVICE_BLACK : DEVICE_RED, DEVICE_WHITE, 2)
  if (!nativeSceneGroup(gfx, 'day', '日期大字', 'graphic', { x: railX, y: header, width: railWidth, height: dayHeight }, drawDay)) drawDay()
  for (const y of [monthY, bottom - bluetoothHeight]) gfx.drawDottedLine(railX - 2, y, data.width - margin, y, DEVICE_BLACK, 2, 2)
  drawV1InlineText(gfx, railX + 2, monthY + 3, railWidth - 4, monthHeight - 5,
    v1DateSpans(time, data, true, false), {scale: 256, minimumScale: 256, gap: 1, align: 'center', baselineY: monthY + div(monthHeight - 14, 2) + 12})
  drawV1InlineText(gfx, railX + 2, bottom - bluetoothHeight, railWidth - 4, bluetoothHeight,
    [{text: '星期', color: DEVICE_BLACK}, {text: lunarDayStrings[time.weekday]!, color: bwOnly(data) ? DEVICE_BLACK : DEVICE_RED}],
    {scale: 256, minimumScale: 256, gap: 1, align: 'center', baselineY: bottom - bluetoothHeight + div(bluetoothHeight - 11, 2) + 11})
  drawV1CompactFooter(gfx, time, lunar, data)
}

function drawV1WideChrome(gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData, topRule = false) {
  const layoutQ8 = clamp(Math.min(div(data.width * 256, 400), div(data.height * 256, 300)), 256, 512)
  const px = (value: number) => div(value * layoutQ8 + 128, 256)
  const margin = px(10)
  const status = v1PowerStatusFrame(data)
  const header42 = data.width === 400 && data.height === 300
  const headerHeight = status.height + (header42 ? 22 : 26)
  const badgeWidth = data.width > 500 ? 122 : 82
  const footerHeight = data.width > 500 ? 31 : 21
  const badgeHeight = footerHeight - 5
  const footerY = data.height - footerHeight
  const date: V1NativeRun[] = v1DateSpans(time, data, true, true).map((span, i) => ({...span,
    font: i % 2 === 0 ? data.width > 500 ? 'helv-bold-18-num' : 'helv-bold-14-num'
      : data.width > 500 ? V1_HEADING_FONT : V1_BODY_FONT,
    gap: !i ? 0 : i % 2 ? 2 : data.width > 500 ? 4 : 3}))
  date.push({text:'[', font:V1_BODY_FONT, color:DEVICE_BLACK, gap:10, scale:data.width>500?384:256},
    {text: String(firmwareIsoWeek(time.year, time.month, time.day)),
      font: data.width > 500 ? 'helv-bold-14-num' : V1_BODY_FONT, color: bwOnly(data) ? DEVICE_BLACK : DEVICE_RED},
    {text: '周', font: data.width > 500 ? V1_HEADING_FONT : V1_BODY_FONT, color: DEVICE_BLACK},
    {text:']', font:V1_BODY_FONT, color:DEVICE_BLACK, gap:data.width>500?-2:0, scale:data.width>500?384:256})
  v1NativeRuns(gfx, date, margin, v1HeaderBaseline(data))
  const countdown = v1CountdownRuns(gfx, data, div(data.width - margin * 2, 2))
  const countdownWidth = v1NativeRuns(gfx, countdown)
  const countdownX = status.x + status.width - countdownWidth
  const lunarWidth = Math.min(status.x - margin - px(8), countdownX - margin - 8)
  const lunarFont = data.width > 500 ? V1_HEADING_FONT : V1_BODY_FONT
  drawV1InlineText(gfx, margin, status.height + 2, lunarWidth, headerHeight - status.height - 4,
    v1CompactLunarSpans(gfx, time, lunar, data, lunarWidth, 256, 'weekday', lunarFont), {
      font: lunarFont, align: 'left', scale: 256, minimumScale: 256,
    })
  v1NativeRuns(gfx, countdown, countdownX, status.height + (header42 ? 16 : 19))
  const right = data.width - margin
  if (topRule) gfx.drawFastHLine(margin, headerHeight - 1, data.width - margin * 2, DEVICE_BLACK)
  gfx.drawFastHLine(margin, footerY, data.width - margin * 2, DEVICE_BLACK)
  const footerScale = data.width <= 500 ? 256 : Math.min(v1ScaleQ8(data), 384)
  const footerAscent = div(10 * footerScale, 256)
  drawV1Text(gfx, margin, footerY + 1, right - margin - badgeWidth - px(8), footerHeight - 1, v1BluetoothLabel(gfx, data, right - margin - badgeWidth - px(8), footerScale), {
    align: 'left', color: DEVICE_BLACK, scale: footerScale, minimumScale: footerScale,
    baselineY: footerY + 1 + div(footerHeight - 1 - footerAscent - Math.ceil(footerScale / 256), 2) + footerAscent,
  })
  drawV1EditionBadge(gfx, data, right - badgeWidth, footerY + 3, badgeWidth, badgeHeight)
  return {x: margin, y: headerHeight, width: data.width - margin * 2, height: footerY - headerHeight - 2}
}

function drawV1Calendar(gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData) {
  const family = v1LayoutPresetForSize(data.width, data.height).family
  if (family === 'compact-square') drawV1CalendarSquare(gfx, time, lunar, data)
  else if (family === 'compact-rect') drawV1CalendarCompactRect(gfx, time, lunar, data)
  else drawV1MonthGrid(gfx, time, data, {...drawV1WideChrome(gfx, time, lunar, data), bandedWeekdays: true, showLunar: true, gridLines: true})
}

function v1TimeWidth(characterSize: number) {
  return 2 * (2 * (11 * characterSize + 2) - 2 * characterSize) + 6 * characterSize + 2 * Math.max(1, div(characterSize, 2))
}

function drawV1Clock(gfx: DevicePixelGfx, time: FirmwareTime, lunar: FirmwareLunarDate, data: NativePageData) {
  const preset = v1LayoutPresetForSize(data.width, data.height)
  const family = preset.family
  const compact = family === 'compact-square' || family === 'compact-rect'
  const square = family === 'compact-square'
  const margin = compact ? 2 : clamp(div(Math.min(data.width, data.height), 24), 10, 28)
  const body = compact ? {y: v1PowerStatusFrame(data).height, height: data.height - v1PowerStatusFrame(data).height - v1CompactFooterHeight(data)}
    : drawV1WideChrome(gfx, time, lunar, data, true)
  const headerHeight = body.y
  if (compact) drawV1CompactHeader(gfx, time, data)
  const availableHeight = body.height
  let characterSize = square
    ? data.width <= 160 ? 2 : data.width >= 240 ? 4 : 3
    : compact
      ? data.width <= 170 ? 2 : data.height <= 110 ? 2 : data.width < 280 ? 3 : 4
    : Math.max(4, div((data.width - margin * 2 - 8) * 8, 470))
  while (characterSize > 1 && (v1TimeWidth(characterSize) > data.width - margin * 2 || 20 * characterSize + 2 > availableHeight - 4)) characterSize -= 1
  const clockOptions = nativeSceneOptions(gfx, 'clock')
  const clockSize = Math.max(1, Math.min(characterSize, Number(clockOptions.digitSize) || characterSize))
  const clockTime = clockOptions.twelveHour ? { ...time, hour: time.hour % 12 || 12 } : time
  const heightSize = clockSize
  const timeWidth = v1TimeWidth(clockSize), timeHeight = 18 * heightSize + 2 * clockSize + 2
  // V1 reserves a fixed HH:MM field; the leading digit never moves its origin.
  const timeX = div(data.width - timeWidth, 2)
  const timeY = headerHeight + div(availableHeight - timeHeight, 2)
  const clockDraw = () => drawTimeStyled(gfx, clockTime, timeX, timeY, clockSize, 2,
    DEVICE_BLACK, DEVICE_BLACK, Math.max(1, div(clockSize, 2)), heightSize)
  if (!nativeSceneGroup(gfx, 'clock', '大字时钟', 'clock', { x: div(data.width-timeWidth,2), y: timeY, width: timeWidth, height: timeHeight }, clockDraw,
    { digitSize: characterSize, twelveHour: false })) clockDraw()
  if (compact) {
    if (square) {
      const lunarWidth = Math.min(data.width - 6, 128)
      drawV1InlineText(gfx, div(data.width - lunarWidth, 2), data.height - v1CompactFooterHeight(data) - 16, lunarWidth, 12,
        v1CompactLunarSpans(gfx, time, lunar, data, lunarWidth, 256), {scale: 256, minimumScale: 256, align: 'center'})
    }
    drawV1CompactFooter(gfx, time, lunar, data)
  }
}

function drawV1InfoRows(
  gfx: DevicePixelGfx,
  data: NativePageData,
  x: number,
  y: number,
  width: number,
  height: number,
  rows: ReadonlyArray<[string, string]>,
  showKeys = true,
) {
  const rowHeight = div(height, rows.length)
  const keyWidth = !showKeys ? 0 : clamp(div(width, 4), 34, 92)
  const columnGap = !showKeys ? 0 : data.width < 350 ? 8 : clamp(div(width, 24), 8, 16)
  const valueX = x + keyWidth + columnGap
  const valueWidth = Math.max(1, width - keyWidth - columnGap)
  rows.forEach(([key, value], index) => {
    const rowY = y + index * rowHeight
    const textScale = v1InfoTextScale(data)
    if (showKeys) nativeNamedText(gfx, `info-key-${key}`, `${key} 标签`, () => drawV1Text(gfx, x, rowY, keyWidth, rowHeight, key, {
      align: 'right', color: bwOnly(data) ? DEVICE_BLACK : DEVICE_RED, scale: textScale, minimumScale: data.width < 350 ? 256 : V1_MIN_META_SCALE,
    }))
    gfx.setFont(V1_BODY_FONT); gfx.setFontScale(textScale)
    const selectedValue = value
    gfx.setFont(V1_BODY_FONT); gfx.setFontScale(textScale)
    const fittedValue = deviceInfoFitText(gfx, selectedValue, valueWidth)
    const fieldLabel: Record<string, string> = { NAME: '设备名称', FW: '固件版本', BLE: '蓝牙名', MCU: '主控', EPD: '屏幕信息', TIER: '产品等级' }
    nativeNamedText(gfx, `info-value-${key}`, fieldLabel[key] || key, () => drawV1Text(gfx, valueX, rowY, valueWidth, rowHeight, fittedValue, {
      scale: textScale, minimumScale: data.width < 350 ? 256 : V1_MIN_META_SCALE,
      align: showKeys ? 'left' : 'center',
    }))
    if (index < rows.length - 1) {
      gfx.drawDottedLine(x, rowY + rowHeight - 1, x + width - 1, rowY + rowHeight - 1, DEVICE_BLACK, 1, 4)
    }
  })
}

function drawV1BrandTitle(gfx: DevicePixelGfx, data: NativePageData, x: number, y: number, width: number, height: number) {
  const draw = () => { drawInkTagBrandTitle(gfx, { x, y, width, height }, data.color) }
  if (!nativeSceneGroup(gfx, 'brand-title', 'InkTag 标题', 'graphic', { x, y, width, height }, draw)) draw()
}

export function v1DeviceInfoQrGeometry(width: number, height: number, contentY: number, contentHeight: number, qr: { size: number; data: Uint8Array } = v1DeviceQr) {
  const preset = v1LayoutPresetForSize(width, height)
  const compact = preset.family.startsWith('compact-')
  const maximumQrSectionWidth = div(width, 2)
  const qrLimit = Math.min(contentHeight, maximumQrSectionWidth, compact ? 132 : 300)
  // Match C: mutable QR length must not change the information column.
  const referenceSide = (v1DeviceQr.size + 8) * Math.max(1, div(qrLimit, v1DeviceQr.size + 8))
  const qrSectionWidth = compact ? Math.min(maximumQrSectionWidth, div(referenceSide * 5, 4))
    : Math.max(referenceSide, Math.min(div(width * 47, 100), div(referenceSide * 6, 5)))
  const qrScale = Math.max(1, div(Math.min(qrLimit, qrSectionWidth), qr.size + 8))
  const qrSide = preset.family === 'compact-square' && qr.size + 8 <= referenceSide
    ? referenceSide : (qr.size + 8) * qrScale
  return { qr, qrScale, qrSide, qrSectionWidth, qrX: div(qrSectionWidth - qrSide, 2), qrY: contentY + div(contentHeight - qrSide, 2) }
}

function drawV1DeviceInfo(gfx: DevicePixelGfx, data: NativePageData) {
  const preset = v1LayoutPresetForSize(data.width, data.height)
  const family = preset.family
  const compact = family === 'compact-square' || family === 'compact-rect'
  const square = family === 'compact-square'
  const margin = compact ? 3 : clamp(div(Math.min(data.width, data.height), 24), 10, 28)
  const headerHeight = v1PowerStatusFrame(data).height
  const footerHeight = compact ? 18 : data.width > 500 ? 31 : 21
  const badgeWidth = compact ? 66 : family === 'large' ? 122 : 82
  const badgeHeight = footerHeight - 5
  const status = v1PowerStatusFrame(data)
  const brandHeight = data.width > 500 ? headerHeight - 3 : Math.min(30, headerHeight - 5)
  const brandY = v1HeaderBaseline(data) - (headerHeight>20 ? 7:5) - div(brandHeight, 2)
  drawV1BrandTitle(gfx, data, margin, brandY, status.x - margin * 2, brandHeight)
  gfx.drawFastHLine(margin, headerHeight - 1, data.width - margin * 2, DEVICE_BLACK)

  const contentY = headerHeight
  const contentHeight = data.height - contentY - footerHeight

  const { qr, qrScale, qrSide, qrSectionWidth, qrX, qrY } = v1DeviceInfoQrGeometry(data.width, data.height, contentY, contentHeight, data.deviceInfoQr)
  const drawQr = () => drawDeviceQr(gfx, qrX, qrY, qrSide, qr.data, qr.size, qrScale, DEVICE_BLACK, DEVICE_WHITE, true)
  if (!nativeSceneGroup(gfx, 'qr', '连接二维码', 'graphic', { x: qrX, y: qrY, width: qrSide, height: qrSide }, drawQr)) drawQr()
  gfx.drawFastVLine(qrSectionWidth, contentY, contentHeight, DEVICE_BLACK)
  const infoX = qrSectionWidth + margin
  const infoWidth = data.width - infoX - margin
  const infoKeyWidth = clamp(div(infoWidth, 4), 34, 92)
  const infoGap = data.width < 350 ? 8 : clamp(div(infoWidth, 24), 8, 16)
  const valueWidth = square ? infoWidth : infoWidth - infoKeyWidth - infoGap
  const infoTextScale = v1InfoTextScale(data)
  const rows: ReadonlyArray<[string, string]> = [
    ['NAME', data.deviceName],
    ['BLE', v1BluetoothLabel(gfx, data, valueWidth, infoTextScale, false, square)],
    ['MCU', data.mcuName],
    ['EPD', `${deviceInfoScreenToken(data.screenInfo)} ${deviceInfoColorName(data)}`],
    ['FW', `v${data.firmwareVersion}`],
    ['TIER', data.activated ? 'PRO' : 'InkTag'],
  ]
  const visibleRows = data.height < 150 ? [rows[1]!, rows[2]!, rows[4]!, rows[5]!] : rows
  drawV1InfoRows(gfx, data, infoX, contentY, infoWidth, contentHeight, visibleRows, !square)

  const footerY = data.height - footerHeight
  gfx.drawFastHLine(margin, footerY, data.width - margin * 2, DEVICE_BLACK)
  const captionScale = compact ? 256 : Math.min(v1ScaleQ8(data), 384)
  const capHeight = data.width <= 500 ? 8 : div(8 * captionScale, 256)
  nativeNamedText(gfx, 'device-caption', '设备信息页标识', () => drawV1InlineText(gfx, margin, footerY + 1, data.width - badgeWidth - margin * 3, footerHeight - 1,
    [{text: 'DEVICE', color: DEVICE_BLACK}, {text: 'INFO', color: DEVICE_BLACK}], {
    gap: data.width > 500 ? div(3 * captionScale + 128, 256) : 2,
    scale: captionScale, minimumScale: compact ? 256 : V1_MIN_META_SCALE,
    baselineY: footerY + 1 + div(footerHeight - 1 - capHeight, 2) + capHeight,
  }))
  drawV1EditionBadge(gfx, data, data.width - margin - badgeWidth, footerY + 3, badgeWidth, badgeHeight)
}

export function renderFirmwareNativePageV1(options: NativePageRenderOptions): DevicePixelFrame {
  if (options.page === 'low-battery') throw new RangeError('The low-battery native page is retired')
  const bluetoothName = options.bluetoothName ?? 'IKT_GR_3A57'
  const factoryName = v1FactoryBluetoothName(bluetoothName)
  const data: NativePageData = {
    page:options.page,
    width: Math.trunc(options.width),
    height: Math.trunc(options.height),
    color: options.colorCount ?? 2,
    nativeUiScale: options.nativeUiScale ?? 1,
    weekStart: clamp(options.weekStart ?? 1, 0, 6),
    temperature: options.temperature ?? 24,
    voltage: options.voltage ?? 3010,
    bluetoothName,
    deviceInfoQr: options.deviceInfoQr,
    activated: options.activated ?? options.edition === 'pro',
    boardName: options.boardName ?? 'gr551x_zkc42v',
    boardNameShort: options.boardNameShort ?? (options.boardName === undefined ? 'zkc42v' : undefined),
    screenInfo: options.screenInfo ?? `${options.width}x${options.height} / BWR`,
    edition: options.edition ?? 'base',
    brandHidden: options.brandHidden ?? false,
    deviceName: options.deviceName ?? (factoryName ? `InkTag ${factoryName.slice(-4)}` : bluetoothName || 'InkTag'),
    mcuName: options.mcuName || '--',
    firmwareVersion: options.firmwareVersion ?? INKTAG_FIRMWARE_VERSION.version,
    countdown: resolveV1Countdown(options.timestamp ?? 1790346480, options.countdown),
  }
  const rawGfx = new DevicePixelGfx(data.width, data.height)
  const gfx = options.scene ? recordNativeScene(rawGfx, options.scene) : rawGfx
  gfx.fontRasterMode = 'nearest'
  gfx.fillScreen(DEVICE_WHITE)
  const time = firmwareTransformTime(options.timestamp ?? 1790346480)
  const lunar = firmwareSolarToLunar(time.year, time.month, time.day)
  if (options.page === 'calendar' || options.page === 'clock' || options.page === 'device-info') drawV1PowerStatus(gfx, data)
  if (options.page === 'calendar') drawV1Calendar(gfx, time, lunar, data)
  else if (options.page === 'clock') drawV1Clock(gfx, time, lunar, data)
  else if (options.page === 'device-info') drawV1DeviceInfo(gfx, data)
  else throw new RangeError(`Page ${options.page} is not part of layout v1`)
  return gfx.frame
}

export function renderFirmwareNativePage(options: NativePageRenderOptions): DevicePixelFrame {
  const revision = options.layoutRevision ?? currentNativeLayoutRevision
  if (revision === 'v1') return renderFirmwareNativePageV1(options)
  const exhaustive: never = revision
  throw new RangeError(`Unsupported native layout revision: ${String(exhaustive)}`)
}
