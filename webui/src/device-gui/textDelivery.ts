import { missingDeviceGlyphs } from './fontRenderer'
import { parseTextTemplate, resolveTextTemplate } from './textVariables'
import { deviceGuiFontOptions, rasterFontOptions, type DeviceGuiFontId, type DeviceGuiWidget } from './model'

/** Authoring policy only; this is not the reserved device layout protocol. */
export type TextPart =
  | { type: 'literal'; text: string }
  | { type: 'variable'; id: string; possibleCharacters?: string }

export interface TextDelivery {
  mode: 'device-text' | 'background' | 'split-required' | 'blocked'
  missingGlyphs: string[]
  reason: 'supported' | 'missing-glyphs' | 'web-font' | 'unbounded-variable' | 'variable-glyphs'
}

/**
 * Check all possible variable characters, never just today's sample. Missing
 * glyphs are reported, never an implicit font switch or raster conversion.
 */
export function planTextDelivery(fontId: DeviceGuiFontId, parts: readonly TextPart[]): TextDelivery {
  const variables = parts.filter((part) => part.type === 'variable')
  const literals = parts.filter((part) => part.type === 'literal').map((part) => part.text).join('')
  const missingGlyphs = missingDeviceGlyphs(fontId, literals)
  if (variables.some((part) => part.possibleCharacters === undefined)) {
    return { mode: 'blocked', missingGlyphs, reason: 'unbounded-variable' }
  }
  const variableGlyphs = missingDeviceGlyphs(fontId, variables.map((part) => part.possibleCharacters).join(''))
  if (variableGlyphs.length) {
    return { mode: 'blocked', missingGlyphs: [...new Set([...missingGlyphs, ...variableGlyphs])], reason: 'variable-glyphs' }
  }
  return { mode: missingGlyphs.length ? variables.length ? 'split-required' : 'blocked' : 'device-text',
    missingGlyphs, reason: missingGlyphs.length ? 'missing-glyphs' : 'supported' }
}

/** Recomputed for render/export, edits, undo and draft restore. Never trust a saved native/raster flag. */
export function textWidgetDelivery(widget: DeviceGuiWidget): TextDelivery | null {
  if (widget.kind !== 'text' && widget.kind !== 'raster-text') return null
  if (widget.kind === 'raster-text') return { mode: 'background', missingGlyphs: [], reason: 'web-font' }
  if (widget.nativeElement) {
    // The preset may mix numeric and WQY runs; a single stored font is not its repertoire.
    if (!widget.nativeElement.fontOverride) return null
    if (!widget.nativeElement.fixedText) return planTextDelivery(widget.nativeElement.fontOverride,
      [{ type: 'literal', text: widget.nativeElement.previewText || '' }])
  }
  const font = widget.nativeElement?.fontOverride ?? widget.style.fontId
  const text = widget.data.text ?? 'InkTag'
  const delivery = planTextDelivery(font, parseTextTemplate(text))
  // Unrestricted names need a future device policy, but current preview glyphs
  // must still be reported when the selected WQY subset cannot display them.
  return { ...delivery, missingGlyphs: [...new Set([...delivery.missingGlyphs,
    ...missingDeviceGlyphs(font, resolveTextTemplate(text, widget))])] }
}

export function textDeliveryLabel(delivery: TextDelivery) {
  if (delivery.mode === 'device-text') return '设备字库可绘制'
  if (delivery.mode === 'background') return '静态底图文字'
  if (delivery.reason === 'missing-glyphs') return '所选字库缺字'
  if (delivery.mode === 'split-required') return '需拆分固定文字与变量'
  return '动态字库覆盖未满足'
}

/** Only an explicit font choice changes how a fixed text box is rendered. */
export function selectTextWidgetFont(widget: DeviceGuiWidget, id: string): DeviceGuiWidget {
  if (widget.kind !== 'text' && widget.kind !== 'raster-text') return widget
  const deviceFont = deviceGuiFontOptions.find((font) => font.id === id)
  if (deviceFont) return { ...widget, kind: 'text', style: { ...widget.style, fontId: deviceFont.id } }
  const webFont = rasterFontOptions.find((font) => font.id === id)
  if (webFont) return { ...widget, kind: 'raster-text', data: { ...widget.data, rasterFontId: webFont.id } }
  return widget
}
