import { defaultStyleForWidget, widgetDefinition } from './catalog'
import {
  DEVICE_GUI_SCHEMA_VERSION,
  type DeviceGuiDocument,
  type DeviceGuiDocumentKind,
  type DeviceGuiFrame,
  type DeviceGuiPresetId,
  type DeviceGuiSizeClass,
  type DeviceGuiStyle,
  type DeviceGuiWidget,
  type DeviceGuiWidgetData,
  type DeviceGuiWidgetKind,
} from './model'
import { clampDeviceGuiFrame, deviceGuiSizeClass } from './space'
import { createNativeReference, nativeReferencePresets } from './nativeReference'

interface WidgetSeed {
  kind: DeviceGuiWidgetKind
  frame: DeviceGuiFrame
  data?: Partial<DeviceGuiWidgetData>
  style?: Partial<DeviceGuiStyle>
  variant?: DeviceGuiSizeClass
  locked?: boolean
}

export const deviceGuiPresetOptions: ReadonlyArray<{ id: DeviceGuiPresetId; kind: DeviceGuiDocumentKind; label: string; description: string }> = [
  ...nativeReferencePresets,
  { id: 'agenda-board', kind: 'agenda', label: '纵向待办', description: '反色标题、编号事项与反色页脚' },
  { id: 'schedule-grid', kind: 'schedule', label: '网格课表', description: '固定标题与有界课程表格' },
]

export function defaultDeviceGuiPreset(kind: DeviceGuiDocumentKind): DeviceGuiPresetId {
  if (kind === 'device-info') return 'layout-v1-device-info'
  if (kind === 'clock') return 'layout-v1-clock'
  if (kind === 'agenda') return 'agenda-board'
  if (kind === 'schedule') return 'schedule-grid'
  return 'layout-v1-calendar'
}

export function createDefaultDeviceGuiDocument(kind: DeviceGuiDocumentKind, width: number, height: number, colorCount: number): DeviceGuiDocument {
  return createDeviceGuiPresetDocument(defaultDeviceGuiPreset(kind), width, height, colorCount)
}

export function createDeviceGuiPresetDocument(presetId: DeviceGuiPresetId, width: number, height: number, colorCount: number): DeviceGuiDocument {
  const reference = nativeReferencePresets.find((item) => item.id === presetId)
  if (reference) return createNativeReference(reference, width, height, colorCount)
  const option = deviceGuiPresetOptions.find((item) => item.id === presetId)
  if (!option) throw new RangeError(`Unknown current device GUI preset: ${presetId}`)
  const sizeClass = deviceGuiSizeClass(width, height)
  const seeds = presetSeeds(option.id, sizeClass, width, height)
  const widgets = seeds.map((seed, index) => createWidget(seed, sizeClass, index))
  return {
    schemaVersion: DEVICE_GUI_SCHEMA_VERSION,
    id: 'device-gui:' + option.kind + ':' + width + 'x' + height,
    name: option.label,
    kind: option.kind,
    width,
    height,
    colorCount,
    sizeClass,
    presetId: option.id,
    widgets,
    updatedAt: Date.now(),
  }
}

export function createDeviceGuiWidget(kind: DeviceGuiWidgetKind, variant: DeviceGuiSizeClass, frame: DeviceGuiFrame, index = Date.now()): DeviceGuiWidget {
  return createWidget({ kind, frame }, variant, index)
}

function createWidget(seed: WidgetSeed, documentSize: DeviceGuiSizeClass, index: number): DeviceGuiWidget {
  const variant = seed.variant || documentSize
  const definition = widgetDefinition(seed.kind)
  return {
    id: seed.kind + '-' + index,
    kind: seed.kind,
    variant,
    frame: seed.frame,
    style: { ...defaultStyleForWidget(seed.kind, variant), ...seed.style },
    data: { ...definition.defaultData, ...seed.data },
    locked: seed.locked,
  }
}

function presetSeeds(presetId: DeviceGuiPresetId, size: DeviceGuiSizeClass, width: number, height: number): WidgetSeed[] {
  const p = size === 'compact' ? 4 : size === 'normal' ? 10 : 16
  const seed = (
    kind: DeviceGuiWidgetKind,
    x: number,
    y: number,
    w: number,
    h: number,
    data?: Partial<DeviceGuiWidgetData>,
    style?: Partial<DeviceGuiStyle>,
    variant?: DeviceGuiSizeClass,
    locked?: boolean,
  ): WidgetSeed => ({
    kind,
    frame: clampDeviceGuiFrame({ x, y, width: w, height: h }, width, height),
    data,
    style,
    variant,
    locked,
  })

  if (presetId === 'agenda-board') return agendaSeeds(size, width, height, p, seed)
  if (presetId === 'schedule-grid') return scheduleSeeds(size, width, height, p, seed)
  throw new RangeError(`Preset ${presetId} is handled by the native reference renderer`)
}

type SeedFactory = (
  kind: DeviceGuiWidgetKind,
  x: number,
  y: number,
  width: number,
  height: number,
  data?: Partial<DeviceGuiWidgetData>,
  style?: Partial<DeviceGuiStyle>,
  variant?: DeviceGuiSizeClass,
  locked?: boolean,
) => WidgetSeed

function agendaSeeds(_size: DeviceGuiSizeClass, width: number, height: number, p: number, seed: SeedFactory): WidgetSeed[] {
  return [
    seed('agenda-list', p, p, width - p * 2, height - p * 2, {
      appearance: 'numbered',
      titleText: '待办事项',
      subtitleText: '7月5日  星期六',
      text: '09:00|接见北京客户\n12:00|安排午餐\n13:00|总结会\n15:00|项目会议\n17:00|下班',
      footerText: '宜：接口出错',
    }, { foreground: 'accent', padding: 0 }),
  ]
}

function scheduleSeeds(_size: DeviceGuiSizeClass, width: number, height: number, p: number, seed: SeedFactory): WidgetSeed[] {
  return [
    seed('schedule-table', p, p, width - p * 2, height - p * 2, {
      titleText: '课程表',
      text: '节次|周一|周二|周三|周四|周五\n第1节|语文|数学|英语|语文|舞蹈\n第2节|体育|数学|语文|历史|美术\n第3节|英语|政治|语文|生物|物理\n第4节|化学|数学|体育|物理|音乐\n第5节|语文|数学|美术|英语|自习\n第6节|政治|生物|语文|英语|课外',
      columns: 6,
      headerRows: 1,
    }),
  ]
}
