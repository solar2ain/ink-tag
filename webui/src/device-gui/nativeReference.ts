import { layoutDefaultScaleQ8, layoutScreensV1 } from '../design/layoutPreviewModel'
import { previewDefaultTimestamp } from '../design/layoutPreviewTime'
import { defaultDeviceGuiStyle, type DeviceGuiDocument, type DeviceGuiWidget } from './model'
import { renderFirmwareNativePage, v1LayoutPresetForSize } from './nativePageRenderer'
import { type DevicePixelFrame } from './devicePixelGfx'
import { nativeElementBounds, type NativeScene, type NativeScenePart } from './nativeScene'
import { resolveTextTemplate } from './textVariables'

export const nativeReferencePresets = [
  { id: 'layout-v1-calendar', kind: 'calendar', page: 'calendar', label: '预置日历模板', description: '独立日期与设备文字控件，搭配原生日历网格' },
  { id: 'layout-v1-clock', kind: 'clock', page: 'clock', label: '预置时钟模板', description: '独立日期与设备文字控件，搭配原生七段时钟' },
  { id: 'layout-v1-device-info', kind: 'device-info', page: 'device-info', label: '预置设备信息模板', description: '原生品牌、电源、身份信息与二维码' },
] as const

export function nativeReferenceScale(width: number, height: number) {
  const screen = layoutScreensV1.find((item) => item.width === width && item.height === height)
  const scale = screen ? layoutDefaultScaleQ8(screen, 'v1') : Math.max(width, height) >= 700 ? 384 : Math.max(width, height) > 500 ? 320 : 256
  return scale === 384 ? 2 : scale === 320 ? 4 : 1
}

export function createNativeReference(preset: typeof nativeReferencePresets[number], width: number, height: number, colorCount: number): DeviceGuiDocument {
  const family = v1LayoutPresetForSize(width, height).family
  const compact = family.startsWith('compact-')
  const sizeClass = compact ? 'compact' : family === 'large' ? 'large' : 'normal'
  const document: DeviceGuiDocument = { schemaVersion: 1, id: `${preset.id}:${width}x${height}`, name: preset.label,
    kind: preset.kind, width, height, colorCount, sizeClass, presetId: preset.id, widgets: [], updatedAt: Date.now(),
    nativeReference: { page: preset.page, timestamp: previewDefaultTimestamp.v1, weekStart: 1 } }
  const scene = nativeReferenceScene(document)
  return { ...document, widgets: scene.parts.map(part => sceneWidget(part, document))
      .sort((a, b) => Number(protectedNativeWidget(a)) - Number(protectedNativeWidget(b))),
    nativeReference: { ...document.nativeReference!, elementKeys: scene.parts.map(part => part.element.key) } }
}

export function nativeReferenceScene(document: DeviceGuiDocument): NativeScene {
  const scene: NativeScene = { parts: [], overrides: Object.fromEntries(document.widgets.filter((widget) => widget.nativeElement)
    .map((widget) => [widget.nativeElement!.key, { ...widget.nativeElement!.settings,
      ...(widget.nativeElement!.type === 'calendar' && widget.nativeElement!.resized ? { width: widget.frame.width, height: widget.frame.height } : {}) }])) }
  const render = (sample: NonNullable<DeviceGuiWidget['nativeElement']>['sample'] = {}, page = document.nativeReference!.page,
    overrides = scene.overrides) => {
    const target: NativeScene = { parts: [], overrides }
    renderFirmwareNativePage({ width: document.width, height: document.height, ...document.nativeReference, ...sample,
    page, layoutRevision: 'v1', nativeUiScale: nativeReferenceScale(document.width, document.height),
    colorCount: document.colorCount === 2 ? 1 : 2, defaultMark: true, edition: document.nativeReference?.edition ?? 'base', scene: target })
    return target
  }
  scene.parts = render().parts
  const samples = new Map<string, NativeScene>()
  for (const widget of document.widgets) if (widget.nativeElement?.sample || widget.nativeElement?.page) {
    const element = widget.nativeElement!
    const templateKey = element.templateKey ?? element.key
    const overrides = { ...scene.overrides, [templateKey]: scene.overrides?.[element.key] || {} }
    const key = JSON.stringify([element.sample, element.page, overrides])
    if (!samples.has(key)) samples.set(key, render(element.sample, element.page, overrides))
    const original = samples.get(key)!.parts.find((part) => part.element.key === templateKey)
    const part = original && { ...original, element: { ...original.element, key: element.key } }
    const index = scene.parts.findIndex((part) => part.element.key === widget.nativeElement!.key)
    if (part && index >= 0) scene.parts[index] = part
    else if (part) scene.parts.push(part)
  }
  for (const part of scene.parts) part.element.source = nativeElementBounds(part, part.element, undefined, document.width, document.height)
  return scene
}

function sceneWidget(part: NativeScenePart, document: DeviceGuiDocument): DeviceGuiWidget {
  const element = part.element
  return { id: `element-${element.key}`, kind: element.type === 'text' ? 'text' : element.type === 'calendar' ? 'calendar'
    : element.type === 'clock' ? 'digital-clock' : element.key.startsWith('edition:') ? 'activation-status' : element.key.startsWith('qr:') ? 'qr-code' : 'divider',
    variant: document.sizeClass, frame: { ...element.source }, style: { ...defaultDeviceGuiStyle, padding: 0,
      fontId: element.fontId || 'wqy9-lunar' }, data: { text: element.previewText }, nativeElement: element }
}

/** Refresh untouched geometry for a new date; edits remain offsets from the preset. */
export function refreshNativeElements(document: DeviceGuiDocument): DeviceGuiDocument {
  if (!document.nativeReference?.elementKeys) return document
  const parts = nativeReferenceScene(document).parts
  const widgets = document.widgets.map((widget) => {
    if (!widget.nativeElement) return widget
    const part = parts.find((part) => part.element.key === widget.nativeElement!.key)
    if (!part) return widget
    if (part.element.protected) return { ...sceneWidget(part, document), id: widget.id }
    const source = nativeElementBounds(part, widget.nativeElement, resolveTextTemplate(widget.data.text || '', widget), document.width, document.height)
    return { ...widget, frame: { ...source,
      x: source.x + widget.frame.x - widget.nativeElement.source.x,
      y: source.y + widget.frame.y - widget.nativeElement.source.y,
      ...(widget.nativeElement.resized ? { width: widget.frame.width, height: widget.frame.height } : {}) },
      data: { ...widget.data, text: widget.nativeElement.fixedText ? widget.data.text : part.element.previewText },
      nativeElement: { ...widget.nativeElement, source, label: part.element.label, protected: part.element.protected, options: part.element.options, previewText: part.element.previewText } }
  })
  const extra = parts.filter((part) => !widgets.some((widget) => widget.nativeElement?.key === part.element.key)
    && (!document.nativeReference!.elementKeys!.includes(part.element.key) || part.element.protected)).map((part) => sceneWidget(part, document))
  return { ...document, widgets: [...widgets, ...extra], nativeReference: { ...document.nativeReference,
    elementKeys: [...new Set([...document.nativeReference.elementKeys, ...parts.map((part) => part.element.key)])] } }
}

export function nativeReferenceFrame(document: DeviceGuiDocument): DevicePixelFrame {
  if (!document.nativeReference) throw new Error('Missing native reference')
  return renderFirmwareNativePage({ width: document.width, height: document.height, ...document.nativeReference,
    layoutRevision: 'v1', nativeUiScale: nativeReferenceScale(document.width, document.height),
    colorCount: document.colorCount === 2 ? 1 : 2, defaultMark: true, edition: document.nativeReference.edition ?? 'base' })
}

/** Called with current device-verified state, never a draft's claimed tier. */
export function applyNativeEdition(document: DeviceGuiDocument, edition: 'base' | 'pro') {
  return document.nativeReference ? refreshNativeElements({ ...document,
    nativeReference: { ...document.nativeReference, edition } }) : document
}

export function orderedVisibleWidgets(document: DeviceGuiDocument) {
  return document.widgets.filter((widget) => !widget.hidden || protectedNativeWidget(widget))
    .sort((a, b) => Number(protectedNativeWidget(a)) - Number(protectedNativeWidget(b)))
}

export function protectedNativeWidget(widget: DeviceGuiWidget) { return Boolean(widget.nativeElement?.protected) }
export function widgetLabel(widget: DeviceGuiWidget) { return widget.nativeElement?.label }
