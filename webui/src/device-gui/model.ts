import { calligraphyFont } from '../data/bundledFontMetadata'
import type { NativeElement } from './nativeScene'
export const DEVICE_GUI_SCHEMA_VERSION = 1 as const
export const DEFAULT_DEVICE_GUI_COLOR_COUNT = 3 as const

export type DeviceGuiDocumentKind = 'device-info' | 'calendar' | 'clock' | 'agenda' | 'schedule'
export type DeviceGuiSizeClass = 'compact' | 'normal' | 'large'
export type DeviceGuiColor = 'transparent' | 'white' | 'black' | 'accent'
export type DeviceGuiFontId = 'wqy9-lunar' | 'wqy12-lunar' | 'helv-bold-14-num' | 'helv-bold-18-num'
export type DeviceGuiAlign = 'left' | 'center' | 'right'
export type DeviceGuiVerticalAlign = 'top' | 'middle' | 'bottom'
export type DeviceGuiBorder = 'none' | 'solid' | 'dotted'
export type DeviceGuiAppearance = 'default' | 'grid' | 'minimal' | 'band' | 'seven-segment' | 'numbered' | 'checkbox' | 'block' | 'pill'
export type DeviceGuiPresetId =
  | 'layout-v1-calendar'
  | 'layout-v1-clock'
  | 'layout-v1-device-info'
  | 'agenda-board'
  | 'schedule-grid'
  | 'custom'

export type DeviceGuiWidgetKind =
  | 'inktag-brand'
  | 'device-info-details'
  | 'device-info-footer'
  | 'qr-code'
  | 'status-bar'
  | 'date-header'
  | 'clock-footer'
  | 'board-name'
  | 'activation-status'
  | 'calendar'
  | 'digital-clock'
  | 'analog-clock'
  | 'battery'
  | 'voltage'
  | 'temperature'
  | 'device-name'
  | 'bluetooth-name'
  | 'date'
  | 'year'
  | 'lunar'
  | 'week-number'
  | 'countdown'
  | 'weekday-strip'
  | 'agenda-list'
  | 'schedule-table'
  | 'divider'
  | 'image-panel'
  | 'canvas-region'
  | 'text'
  | 'raster-text'

export type RasterFontId = 'system-sans' | 'system-serif' | 'system-mono' | 'yahei' | 'simsun' | 'kaiti' | 'ma-shan-zheng'

export interface DeviceGuiFrame {
  x: number
  y: number
  width: number
  height: number
}

/**
 * Protocol-shaped style data. Every field has a bounded enum or integer form
 * so this can later map to a fixed C struct without carrying browser CSS.
 */
export interface DeviceGuiStyle {
  fontId: DeviceGuiFontId
  /** Q8 fixed-point device-font scale. 256 means 100%. */
  fontScaleQ8: number
  foreground: DeviceGuiColor
  background: DeviceGuiColor
  align: DeviceGuiAlign
  verticalAlign: DeviceGuiVerticalAlign
  border: DeviceGuiBorder
  borderWidth: 0 | 1 | 2
  radius: 0 | 2 | 4 | 8
  padding: number
  inverted: boolean
}

export interface DeviceGuiWidgetData {
  text?: string
  /** Per-control preview values. Not device state or a global page clock. */
  sample?: NativeElement['sample']
  /** Web-only font for text rasterized into the static page background. */
  rasterFontId?: RasterFontId
  titleText?: string
  subtitleText?: string
  appearance?: DeviceGuiAppearance
  dateFormat?: 'ymd' | 'md' | 'day' | 'weekday'
  timeFormat?: '24h' | '12h'
  weekStart?: 0 | 1
  showLunar?: boolean
  showHolidays?: boolean
  showWeekday?: boolean
  showValue?: boolean
  lunarFormat?: 'date' | 'zodiac' | 'full'
  countdownDate?: string
  countdownTarget?: { name: string; targetTimestamp: number }
  footerText?: string
  columns?: number
  headerRows?: number
  orientation?: 'horizontal' | 'vertical'
  resourceId?: number
  qrPurpose?: 'firmware' | 'webui' | 'custom'
  qrValue?: string
  showLabel?: boolean
  showTitle?: boolean
  showBluetooth?: boolean
  showScreen?: boolean
  showActivation?: boolean
}

/**
 * This semantic widget is the application data. Canvas scene objects are
 * views only and must never be persisted or sent to firmware as the protocol.
 */
export interface DeviceGuiWidget {
  id: string
  kind: DeviceGuiWidgetKind
  variant: DeviceGuiSizeClass
  frame: DeviceGuiFrame
  style: DeviceGuiStyle
  data: DeviceGuiWidgetData
  hidden?: boolean
  locked?: boolean
  /** Web-only, pixel-exact region of the frozen native page, not a firmware opcode. */
  /** Independent semantic V1 control; the authoring record stores no bitmap crop. */
  nativeElement?: NativeElement
}

export interface DeviceGuiDocument {
  schemaVersion: typeof DEVICE_GUI_SCHEMA_VERSION
  id: string
  name: string
  kind: DeviceGuiDocumentKind
  width: number
  height: number
  colorCount: number
  sizeClass: DeviceGuiSizeClass
  presetId?: DeviceGuiPresetId
  widgets: DeviceGuiWidget[]
  updatedAt: number
  /** Reference composition only; never serialize this as the reserved layout protocol. */
  /** Fixed-resolution authoring document. Edition is preview metadata, never authorization. */
  nativeReference?: { page: 'calendar' | 'clock' | 'device-info'; timestamp: number; weekStart: 0 | 1; elementKeys?: string[]; edition?: 'base' | 'pro' }
}

export interface DeviceGuiSampleData {
  now: Date
  voltage: number | null
  temperature: number | null
  deviceName: string
  bluetoothName: string
  batteryPercent: number
  boardName: string
  screenInfo: string
  activated: boolean
}

export interface DeviceGuiCapabilitySet {
  protocolVersion: number
  maxWidgets: number
  maxLayoutBytes: number
  fonts: DeviceGuiFontId[]
  widgets: DeviceGuiWidgetKind[]
}

export interface DeviceGuiValidationIssue {
  level: 'error' | 'warning'
  code: 'bounds' | 'overlap' | 'unsupported-font' | 'unsupported-widget' | 'limit' | 'content-limit' | 'glyph-coverage'
  message: string
  widgetId?: string
}

export const deviceGuiFontOptions: ReadonlyArray<{
  id: DeviceGuiFontId
  label: string
  shortLabel: string
  glyphs: 'text' | 'numeric'
  pixelHeight: number
}> = [
  { id: 'wqy9-lunar', label: '文泉驿点阵 9（固件字符子集）', shortLabel: 'WQY 9', glyphs: 'text', pixelHeight: 12 },
  { id: 'wqy12-lunar', label: '文泉驿点阵 12（固件字符子集）', shortLabel: 'WQY 12', glyphs: 'text', pixelHeight: 16 },
  { id: 'helv-bold-14-num', label: 'Helvetica Bold 14（仅数字）', shortLabel: 'Helv 14', glyphs: 'numeric', pixelHeight: 17 },
  { id: 'helv-bold-18-num', label: 'Helvetica Bold 18（仅数字）', shortLabel: 'Helv 18', glyphs: 'numeric', pixelHeight: 22 },
]

export const rasterFontOptions: ReadonlyArray<{ id: RasterFontId; label: string; family: string }> = [
  { id: 'system-sans', label: '系统黑体', family: 'system-ui, sans-serif' },
  { id: 'system-serif', label: '系统宋体', family: 'serif' },
  { id: 'system-mono', label: '等宽体', family: 'ui-monospace, monospace' },
  { id: 'yahei', label: '微软雅黑', family: '"Microsoft YaHei", system-ui, sans-serif' },
  { id: 'simsun', label: '宋体', family: 'SimSun, serif' },
  { id: 'kaiti', label: '楷体', family: 'KaiTi, STKaiti, serif' },
  calligraphyFont,
]

export const defaultDeviceGuiStyle: DeviceGuiStyle = {
  fontId: 'wqy9-lunar',
  fontScaleQ8: 256,
  foreground: 'black',
  background: 'transparent',
  align: 'left',
  verticalAlign: 'middle',
  border: 'none',
  borderWidth: 0,
  radius: 0,
  padding: 2,
  inverted: false,
}

export function normalizeDeviceGuiDocument(document: DeviceGuiDocument): DeviceGuiDocument {
  return {
    ...document,
    widgets: document.widgets.map((widget) => ({
      ...widget,
      style: { ...defaultDeviceGuiStyle, ...widget.style, fontScaleQ8: widget.style.fontScaleQ8 ?? 256 },
      data: { ...widget.data },
    })),
  }
}

export function cloneDeviceGuiDocument(document: DeviceGuiDocument): DeviceGuiDocument {
  return JSON.parse(JSON.stringify(document)) as DeviceGuiDocument
}
