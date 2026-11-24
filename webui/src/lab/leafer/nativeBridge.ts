import { clone, type Document, type Widget, type NativeControl } from '../../../../packages/inktag-editor-core/src/index'
import { createNativeReference, nativeReferencePresets, nativeReferenceScene } from '../../device-gui/nativeReference'
import { DevicePixelGfx, type DevicePixelFrame } from '../../device-gui/devicePixelGfx'
import { drawNativeScenePart, nativeElementBounds, type NativeScene } from '../../device-gui/nativeScene'
import { deviceGuiFontOptions, type DeviceGuiFontId, type DeviceGuiDocument } from '../../device-gui/model'
import { previewDefaultTimestamp } from '../../design/layoutPreviewTime'
import { migrateText } from './migrateText'
import { richTextRaster } from './richTextRenderer'

const references = new Map<string, DeviceGuiDocument>()
const scenes = new Map<string, NativeScene>()
function boundedCache<T>(map: Map<string, T>, key: string, build: () => T): T {
  if (!map.has(key)) { if (map.size >= 24) map.delete(map.keys().next().value!); map.set(key, build()) }
  return map.get(key)!
}
export function reference(page: NativeControl['page'], width: number, height: number) {
  return boundedCache(references, `${page}:${width}:${height}`, () => createNativeReference(nativeReferencePresets.find(p => p.page === page)!, width, height, 3))
}
export function nativeDocument(page: NativeControl['page'], width: number, height: number): Document {
  const ref = reference(page, width, height)
  const scene = nativeReferenceScene(ref)
  return { version: 1, width, height, pageMode: 'dynamic', colorMode: 2, footerInset: 0, widgets: ref.widgets.filter(w => w.nativeElement).map(w => {
    const n = w.nativeElement!, x = Math.max(0, w.frame.x), y = Math.max(0, w.frame.y)
    const widget: Widget = { id: w.id, name: n.label, kind: 'native', x, y, width: Math.max(1, Math.min(w.frame.width, width - x)), height: Math.max(1, Math.min(w.frame.height, height - y)),
      cells: [[{ text: n.previewText || '' }]], style: { fontSize: 12, color: '#000000', bold: false },
      native: { page, key: n.key, role: n.type, source: { ...w.frame }, sample: { timestamp: previewDefaultTimestamp.v1, weekStart: 1 } } }
    const part=scene.parts.find(p=>p.element.key===n.key)
    return n.type==='text' && page!=='device-info' && part ? migrateText(widget,part) : widget
  }) }
}
function nativePart(w: Widget, doc: Document) {
  const n = w.native; if (!n) return
  const base = reference(n.page, doc.width, doc.height)
  const original = base.widgets.find(w => w.nativeElement?.key === n.key)
  if (!original?.nativeElement) return
  const resized = w.width !== n.source.width || w.height !== n.source.height
  const customized = Boolean(n.settings && Object.keys(n.settings).length) || (n.role === 'calendar' && resized)
  const key = JSON.stringify([n.page, doc.width, doc.height, n.sample, customized ? [n.key,n.settings,w.width,w.height] : null])
  const scene = boundedCache(scenes, key, () => {
    const document = clone(base), target = document.widgets.find(w => w.nativeElement?.key === n.key)!
    document.nativeReference = { ...document.nativeReference!, ...n.sample }
    target.nativeElement = { ...target.nativeElement!, settings: n.settings, resized }
    target.frame = { ...target.frame, width: w.width, height: w.height }
    return nativeReferenceScene(document)
  })
  return scene.parts.find(p => p.element.key === n.key)
}
export function refreshNativeWidget(w: Widget, doc: Document) {
  const n = w.native, part = nativePart(w,doc); if (!n || !part) return
  if (w.width !== n.source.width || w.height !== n.source.height) return // Keep an explicitly resized authoring box.
  const source = n.role === 'text' && (n.text !== undefined || n.fontId)
    ? nativeElementBounds(part,{ ...part.element, fontOverride: n.fontId as DeviceGuiFontId | undefined, fixedText: n.text !== undefined },n.text,doc.width,doc.height)
    : part.element.source
  w.x = Math.max(0,Math.min(doc.width - source.width,source.x + w.x - n.source.x))
  w.y = Math.max(0,Math.min(doc.height - source.height,source.y + w.y - n.source.y))
  w.width = Math.min(doc.width,source.width); w.height = Math.min(doc.height,source.height)
  n.source = { ...source }
}
export function nativeRaster(w: Widget, doc: Document): DevicePixelFrame | undefined {
  if(w.kind==='text' && w.cells[0][0].richText) return richTextRaster(w,w.cells[0][0].richText)
  const n = w.native, part = nativePart(w,doc); if (!n || !part) return
  const resized = w.width !== n.source.width || w.height !== n.source.height
  const gfx = new DevicePixelGfx(w.width, w.height)
  const font = deviceGuiFontOptions.some(f => f.id === n.fontId) ? n.fontId as DeviceGuiFontId : undefined
  const customText = n.role === 'text' && (font || n.text !== undefined)
  drawNativeScenePart(gfx, part, { x: customText ? part.element.source.x - n.source.x : 0, y: customText ? part.element.source.y - n.source.y : 0, width: w.width, height: w.height }, {
    ...part.element, resized: resized || (n.role === 'clock' && (part.element.source.width !== w.width || part.element.source.height !== w.height)), fontOverride: font, fixedText: n.text !== undefined,
  }, n.text)
  return gfx.frame
}
export function nativeSettings(w: Widget, doc: Document) {
  if (!w.native) return {}
  return reference(w.native.page, doc.width, doc.height).widgets.find(item => item.nativeElement?.key === w.native!.key)?.nativeElement?.options || {}
}
