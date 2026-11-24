import { widgetDefinition } from './catalog'
import { textWidgetDelivery } from './textDelivery'
import {
  deviceGuiFontOptions,
  type DeviceGuiCapabilitySet,
  type DeviceGuiDocument,
  type DeviceGuiFrame,
  type DeviceGuiSizeClass,
  type DeviceGuiValidationIssue,
  type DeviceGuiWidget,
  type DeviceGuiWidgetKind,
} from './model'

export function deviceGuiSizeClass(width: number, height: number): DeviceGuiSizeClass {
  const min = Math.min(width, height)
  const max = Math.max(width, height)
  if (max <= 384 && min <= 200) return 'compact'
  if (height >= 400 || width >= 640) return 'large'
  return 'normal'
}

export function defaultFrameForWidget(kind: DeviceGuiWidgetKind, variant: DeviceGuiSizeClass, width: number, height: number): DeviceGuiFrame {
  const preferred = widgetDefinition(kind).sizes[variant]
  const frameWidth = Math.min(preferred.width, Math.max(24, width - 8))
  const frameHeight = Math.min(preferred.height, Math.max(18, height - 8))
  return {
    x: Math.max(0, Math.round((width - frameWidth) / 2)),
    y: Math.max(0, Math.round((height - frameHeight) / 2)),
    width: frameWidth,
    height: frameHeight,
  }
}

export function clampDeviceGuiFrame(frame: DeviceGuiFrame, width: number, height: number): DeviceGuiFrame {
  const nextWidth = Math.max(16, Math.min(width, Math.round(frame.width)))
  const nextHeight = Math.max(12, Math.min(height, Math.round(frame.height)))
  return {
    x: Math.max(0, Math.min(width - nextWidth, Math.round(frame.x))),
    y: Math.max(0, Math.min(height - nextHeight, Math.round(frame.y))),
    width: nextWidth,
    height: nextHeight,
  }
}

export function estimateDeviceGuiBytes(document: DeviceGuiDocument) {
  const utf8 = new TextEncoder()
  const strings = document.widgets.reduce((bytes, widget) => bytes
    + utf8.encode(widget.data.text || '').length
    + utf8.encode(widget.data.titleText || '').length
    + utf8.encode(widget.data.subtitleText || '').length
    + utf8.encode(widget.data.countdownDate || '').length
    + utf8.encode(widget.data.footerText || '').length
    + utf8.encode(widget.data.qrValue || '').length, 0)
  // Proposed packed v1: 20-byte header, 28-byte widget records and a bounded UTF-8 pool.
  return 20 + document.widgets.length * 28 + strings
}

export function validateDeviceGuiDocument(document: DeviceGuiDocument, capabilities?: DeviceGuiCapabilitySet | null): DeviceGuiValidationIssue[] {
  const issues: DeviceGuiValidationIssue[] = []
  for (const widget of document.widgets) {
    if (!inside(widget.frame, document.width, document.height)) {
      issues.push({ level: 'error', code: 'bounds', widgetId: widget.id, message: `${widgetDefinition(widget.kind).label}超出屏幕范围` })
    }
    const font = deviceGuiFontOptions.find((item) => item.id === (widget.nativeElement?.fontOverride || widget.style.fontId))
    const delivery = textWidgetDelivery(widget)
    if (delivery?.reason === 'missing-glyphs') {
      issues.push({ level: 'warning', code: 'glyph-coverage', widgetId: widget.id,
        message: `${font?.shortLabel || widget.style.fontId} 不包含「${delivery.missingGlyphs.join('、')}」，请修改文字或手动选择静态字体；不会自动转换底图` })
    }
    if (!Number.isInteger(widget.style.fontScaleQ8) || widget.style.fontScaleQ8 < 128 || widget.style.fontScaleQ8 > 768) {
      issues.push({ level: 'error', code: 'limit', widgetId: widget.id, message: `${widgetDefinition(widget.kind).label}字体缩放必须在 50%..300%` })
    }
    if (!widget.nativeElement && !delivery && font?.glyphs === 'numeric' && !widgetDefinition(widget.kind).numericOnly) {
      issues.push({ level: 'error', code: 'glyph-coverage', widgetId: widget.id, message: `${widgetDefinition(widget.kind).label}包含文字，不能使用纯数字字库` })
    }
    const textBytes = new TextEncoder().encode([
      widget.data.titleText,
      widget.data.subtitleText,
      widget.data.text,
      widget.data.footerText,
      widget.data.qrValue,
    ].filter(Boolean).join('\n')).length
    if (textBytes > 384) {
      issues.push({ level: 'error', code: 'content-limit', widgetId: widget.id, message: widgetDefinition(widget.kind).label + '固定内容超过 384 B' })
    }
    if (widget.kind === 'agenda-list' && (widget.data.text || '').split('\n').filter(Boolean).length > 8) {
      issues.push({ level: 'error', code: 'content-limit', widgetId: widget.id, message: '待办列表最多 8 项' })
    }
    if (widget.kind === 'schedule-table') {
      const rows = (widget.data.text || '').split('\n').filter(Boolean)
      const columns = Math.max(1, Math.min(8, widget.data.columns || 6))
      if (rows.length > 8 || rows.some((row) => row.split('|').length > columns)) {
        issues.push({ level: 'error', code: 'content-limit', widgetId: widget.id, message: '课表最多 8 行 × 8 列' })
      }
    }
    if (capabilities && !capabilities.widgets.includes(widget.kind)) {
      issues.push({ level: 'error', code: 'unsupported-widget', widgetId: widget.id, message: `设备不支持${widgetDefinition(widget.kind).label}控件` })
    }
    if (capabilities && !capabilities.fonts.includes(widget.style.fontId)) {
      issues.push({ level: 'error', code: 'unsupported-font', widgetId: widget.id, message: `设备不支持${font?.shortLabel || widget.style.fontId}` })
    }
  }
  for (let index = 0; index < document.widgets.length; index += 1) {
    const left = document.widgets[index]
    if (left.hidden) continue
    for (let other = index + 1; other < document.widgets.length; other += 1) {
      const right = document.widgets[other]
      if (!right.hidden && overlap(left, right)) {
        issues.push({ level: 'warning', code: 'overlap', widgetId: right.id, message: `${widgetDefinition(left.kind).label}与${widgetDefinition(right.kind).label}发生重叠` })
      }
    }
  }
  if (capabilities && document.widgets.length > capabilities.maxWidgets) {
    issues.push({ level: 'error', code: 'limit', message: `控件数 ${document.widgets.length} 超过设备上限 ${capabilities.maxWidgets}` })
  }
  if (capabilities && estimateDeviceGuiBytes(document) > capabilities.maxLayoutBytes) {
    issues.push({ level: 'error', code: 'limit', message: `布局约 ${estimateDeviceGuiBytes(document)} B，超过设备上限 ${capabilities.maxLayoutBytes} B` })
  }
  return issues
}

function inside(frame: DeviceGuiFrame, width: number, height: number) {
  return frame.x >= 0 && frame.y >= 0 && frame.width >= 1 && frame.height >= 1 && frame.x + frame.width <= width && frame.y + frame.height <= height
}

function overlap(left: DeviceGuiWidget, right: DeviceGuiWidget) {
  if (left.kind === 'divider' || right.kind === 'divider') return false
  if ((left.kind === 'inktag-brand' && left.data.appearance === 'band') || (right.kind === 'inktag-brand' && right.data.appearance === 'band')) return false
  const a = left.frame
  const b = right.frame
  const areaWidth = Math.min(a.x + a.width, b.x + b.width) - Math.max(a.x, b.x)
  const areaHeight = Math.min(a.y + a.height, b.y + b.height) - Math.max(a.y, b.y)
  if (areaWidth <= 0 || areaHeight <= 0) return false
  const smallerArea = Math.min(a.width * a.height, b.width * b.height)
  return areaWidth * areaHeight > smallerArea * .12
}
