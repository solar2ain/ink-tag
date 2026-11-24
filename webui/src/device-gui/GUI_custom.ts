/** GUI_custom: fixed-resolution authoring renderer; not a firmware layout executor. */
import { drawDeviceTextMask, normalizeDeviceText, type DeviceTextCommand } from './fontRenderer'
import { widgetDefinition } from './catalog'
import { nativeReferenceScene, orderedVisibleWidgets } from './nativeReference'
import { DevicePixelGfx, paintDevicePixelFrame } from './devicePixelGfx'
import { drawNativeScenePart } from './nativeScene'
import { resolveTextTemplate, widgetPreviewSample } from './textVariables'
import { createQrMatrix, drawQrCode, INKTAG_RELEASE_URL, INKTAG_WEB_URL } from '../qr/qr'
import {
  rasterFontOptions,
  type DeviceGuiDocument,
  type DeviceGuiFrame,
  type DeviceGuiSampleData,
  type DeviceGuiStyle,
  type DeviceGuiWidget,
} from './model'

const holidays: Record<string, string> = {
  '1-1': '元旦', '5-1': '劳动节', '6-1': '儿童节', '8-1': '建军节', '10-1': '国庆节',
}

export function renderDeviceGuiDocument(canvas: HTMLCanvasElement, document: DeviceGuiDocument, sample: DeviceGuiSampleData, options: { includeBrand?: boolean; transparentBackground?: boolean } = {}) {
  canvas.width = document.width
  canvas.height = document.height
  const context = canvas.getContext('2d')!
  context.imageSmoothingEnabled = false
  context.fillStyle = '#ffffff'
  if (!options.transparentBackground) context.fillRect(0, 0, document.width, document.height)
  if (document.nativeReference?.elementKeys) {
    const scene = nativeReferenceScene(document)
    const gfx = new DevicePixelGfx(document.width, document.height)
    gfx.fontRasterMode = 'nearest'
    const widgets = orderedVisibleWidgets(document)
    for (const widget of widgets) {
      if (options.includeBrand === false && widget.nativeElement?.key.startsWith('edition:')) continue
      if (widget.nativeElement) {
        const part = scene.parts.find((item) => item.element.key === widget.nativeElement!.key)
        if (!part) continue
        const frame = { ...part.element.source, ...(widget.nativeElement.resized ? { width: widget.frame.width, height: widget.frame.height } : {}),
          x: part.element.source.x + widget.frame.x - widget.nativeElement.source.x,
          y: part.element.source.y + widget.frame.y - widget.nativeElement.source.y }
        drawNativeScenePart(gfx, part, frame, widget.nativeElement, resolveTextTemplate(widget.data.text || '', widget, sample))
      } else {
        paintDevicePixelFrame(context, gfx.frame)
        renderWidget(context, document, widget, sample)
        // Static overlays remain browser raster content. Read the composed page
        // back before later native layers (especially the protected edition mark).
        const pixels = context.getImageData(0, 0, document.width, document.height).data
        for (let i = 0; i < gfx.frame.pixels.length; i++) {
          const r = pixels[i * 4]!, g = pixels[i * 4 + 1]!, b = pixels[i * 4 + 2]!
          gfx.frame.pixels[i] = r > 160 && r > g * 1.5 ? 2 : r + g + b > 540 ? 0 : 1
        }
      }
    }
    for (const part of scene.parts) if (!document.nativeReference.elementKeys.includes(part.element.key)) {
      if (options.includeBrand === false && part.element.key.startsWith('edition:')) continue
      drawNativeScenePart(gfx, part, part.element.source, part.element)
    }
    paintDevicePixelFrame(context, gfx.frame)
    return
  }
  for (const widget of orderedVisibleWidgets(document)) renderWidget(context, document, widget, sample)
}

function renderWidget(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, sample: DeviceGuiSampleData) {
  sample = widgetPreviewSample(widget, sample)
  const { frame, style } = widget
  context.save()
  context.beginPath()
  context.rect(frame.x, frame.y, frame.width, frame.height)
  context.clip()
  const foreground = roleColor(style.foreground, document.colorCount)
  const background = roleColor(style.background, document.colorCount)
  if (style.background !== 'transparent') {
    context.fillStyle = background
    if (style.radius) roundRect(context, frame.x, frame.y, frame.width, frame.height, style.radius)
    else context.fillRect(frame.x, frame.y, frame.width, frame.height)
    if (style.radius) context.fill()
  }
  if (style.border !== 'none' && style.borderWidth > 0) {
    context.strokeStyle = foreground
    context.lineWidth = style.borderWidth
    context.setLineDash(style.border === 'dotted' ? [2, 3] : [])
    context.strokeRect(frame.x + style.borderWidth / 2, frame.y + style.borderWidth / 2, frame.width - style.borderWidth, frame.height - style.borderWidth)
  }
  const content = inset(frame, style.padding)

  switch (widget.kind) {
    case 'inktag-brand': renderInkTagBrand(context, document, widget); break
    case 'device-info-details': renderDeviceInfoInfo(context, document, widget, content, sample); break
    case 'device-info-footer': renderDeviceInfoFooter(context, document, widget, content); break
    case 'qr-code': renderQrWidget(context, widget, content); break
    case 'status-bar': renderStatusBar(context, document, widget, content, sample); break
    case 'date-header': renderDateHeader(context, document, widget, content, sample); break
    case 'clock-footer': renderClockFooter(context, document, widget, content, sample); break
    case 'activation-status': renderActivationStatus(context, document, widget, content, sample); break
    case 'calendar': renderCalendar(context, document, widget, content, sample); break
    case 'digital-clock': renderDigitalClock(context, content, sample.now, foreground, widget.data.timeFormat); break
    case 'analog-clock': renderAnalogClock(context, content, sample.now, foreground, roleColor('accent', document.colorCount)); break
    case 'date':
      if (widget.data.appearance === 'seven-segment') renderSevenSegmentValue(context, content, String(sample.now.getDate()), foreground)
      else renderTextWidget(context, document, widget, content, sample)
      break
    case 'countdown': renderCountdown(context, document, widget, content, sample); break
    case 'weekday-strip': renderWeekdayStrip(context, document, widget, content); break
    case 'agenda-list': renderAgendaList(context, document, widget, content); break
    case 'schedule-table': renderScheduleTable(context, document, widget, content); break
    case 'divider': renderDivider(context, content, foreground, widget.data.orientation); break
    case 'raster-text': renderRasterText(context, document, { ...widget, data: { ...widget.data, text: resolveTextTemplate(widget.data.text || '', widget, sample) } }, content); break
    case 'battery': renderBattery(context, document, widget, content, sample); break
    case 'image-panel': renderImagePanel(context, content, foreground, roleColor('accent', document.colorCount)); break
    case 'canvas-region': renderCanvasRegion(context, content, foreground); break
    default: renderTextWidget(context, document, widget, content, sample)
  }
  context.restore()
}

function renderTextWidget(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const text = normalizeDeviceText(widgetText(widget, sample))
  const naturalFontHeight = widget.style.fontId.includes('18') ? 22 : widget.style.fontId.includes('14') ? 17 : widget.style.fontId.includes('12') ? 16 : 12
  const fontScaleQ8 = widgetFontScale(widget)
  const fontHeight = Math.max(1, Math.round(naturalFontHeight * fontScaleQ8 / 256))
  const lines = text.split('\n').slice(0, Math.max(1, Math.floor(frame.height / fontHeight)))
  const blockHeight = lines.length * fontHeight
  const startY = widget.style.verticalAlign === 'top' ? 0
    : widget.style.verticalAlign === 'bottom' ? Math.max(0, frame.height - blockHeight)
      : Math.max(0, Math.round((frame.height - blockHeight) / 2))
  const commands = lines.map((lineText, index) => ({
    text: lineText,
    x: 0,
    y: startY + index * fontHeight,
    width: frame.width,
    align: widget.style.align,
  }))
  drawDeviceTextMask(context, frame, widget.style.fontId, commands, roleColor(widget.style.foreground, document.colorCount), fontScaleQ8)
}

function widgetFontScale(widget: DeviceGuiWidget) {
  return Number.isInteger(widget.style.fontScaleQ8) ? widget.style.fontScaleQ8 : 256
}

function widgetText(widget: DeviceGuiWidget, sample: DeviceGuiSampleData) {
  const now = sample.now
  switch (widget.kind) {
    case 'voltage': return `${(sample.voltage ?? 3.01).toFixed(2)}V`
    case 'temperature': return `${Math.round(sample.temperature ?? 24)}℃`
    case 'device-name': return sample.deviceName || 'InkTag Desk'
    case 'bluetooth-name': return sample.bluetoothName || 'IKT_42VM'
    case 'board-name': return sample.boardName || 'GR551X_ZKC42VM'
    case 'year': return String(now.getFullYear())
    case 'date': {
      if (widget.data.dateFormat === 'md') return `${now.getMonth() + 1}月${now.getDate()}日`
      if (widget.data.dateFormat === 'day') return String(now.getDate())
      if (widget.data.dateFormat === 'weekday') return '星期' + '日一二三四五六'[now.getDay()]
      return `${now.getFullYear()}年${now.getMonth() + 1}月${now.getDate()}日`
    }
    case 'lunar': {
      if (widget.data.lunarFormat === 'zodiac') return zodiacText(now)
      if (widget.data.lunarFormat === 'full') return zodiacText(now) + ' · ' + lunarText(now)
      return lunarText(now)
    }
    case 'week-number': return `第${isoWeek(now)}周`
    case 'countdown': {
      const target = parseLocalDate(widget.data.countdownDate || '2027-01-01')
      const days = Math.max(0, Math.ceil((target.getTime() - startOfDay(now).getTime()) / 86400000))
      return `离目标还有${days}天`
    }
    case 'text': return resolveTextTemplate(widget.data.text ?? 'InkTag', widget, sample)
    default: return widgetDefinition(widget.kind).label
  }
}

const blockGlyphs: Record<string, number[]> = {
  A: [0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11],
  C: [0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e],
  E: [0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f],
  G: [0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f],
  I: [0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f],
  K: [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
  L: [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f],
  M: [0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11],
  N: [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
  O: [0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e],
  T: [0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
  W: [0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11],
}

export function deviceGuiInkTagBrandGeometry(document: DeviceGuiDocument, widget: DeviceGuiWidget) {
  const frame = widget.frame
  const padding = Math.max(0, widget.style.padding)
  const compactWide = document.sizeClass === 'compact' && document.width * 5 >= document.height * 8
  const stacked = !compactWide && document.width * 4 <= document.height * 5
  let markHeight: number
  let gap: number
  let preferredScale: number
  let markY: number
  let textY: number

  if (compactWide) {
    markHeight = Math.max(14, Math.min(20, Math.floor(document.height / 6)))
    gap = 4
    preferredScale = 1
    markY = frame.y + padding
    textY = frame.y + padding + Math.floor((markHeight - 7) / 2)
  } else if (stacked) {
    markHeight = Math.max(18, Math.min(38, frame.height - padding * 2))
    gap = padding
    preferredScale = Math.max(1, Math.min(3, Math.floor((document.width - padding * 3 - Math.floor(markHeight * 6 / 5)) / 36)))
    markY = frame.y + Math.floor((frame.height - markHeight) / 2)
    textY = frame.y + Math.floor((frame.height - 7 * preferredScale) / 2)
  } else {
    markHeight = Math.max(24, Math.min(44, Math.floor(frame.height * 2 / 3)))
    gap = padding
    preferredScale = Math.max(2, Math.min(4, Math.floor(frame.height / 18)))
    markY = frame.y + Math.floor((frame.height - markHeight) / 2)
    textY = frame.y + Math.floor((frame.height - 7 * preferredScale) / 2)
  }

  const markWidth = Math.floor(markHeight * 6 / 5)
  const usableWidth = Math.max(1, frame.width - padding * 2)
  const scale = Math.max(1, Math.min(preferredScale, Math.floor((usableWidth - markWidth - gap) / 36)))
  if (scale !== preferredScale && !compactWide) textY = frame.y + Math.floor((frame.height - 7 * scale) / 2)
  const groupWidth = markWidth + gap + 35 * scale
  const freeWidth = Math.max(0, usableWidth - groupWidth)
  const left = frame.x + padding + (widget.style.align === 'right' ? freeWidth : widget.style.align === 'center' ? Math.round(freeWidth / 2) : 0)
  return {
    mark: { x: left, y: markY, width: markWidth, height: markHeight },
    text: { x: left + markWidth + gap, y: textY, scale },
  }
}

function renderInkTagBrand(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget) {
  const dark = widget.style.background === 'black'
  const geometry = deviceGuiInkTagBrandGeometry(document, widget)
  drawInkTagMark(context, document.colorCount, geometry.mark.x, geometry.mark.y, geometry.mark.width, geometry.mark.height)
  drawBlockText(context, 'INKTAG', geometry.text.x, geometry.text.y, geometry.text.scale, dark ? '#ffffff' : roleColor(widget.style.foreground, document.colorCount))
}

function drawInkTagMark(context: CanvasRenderingContext2D, colorCount: number, x: number, y: number, width: number, height: number) {
  const colors = colorCount <= 2
    ? ['#171a18', '#171a18']
    : colorCount === 3
      ? ['#171a18', '#c82d28']
      : colorCount === 4
        ? ['#171a18', '#c82d28', '#c3a321']
        : ['#171a18', '#666b68', '#b8bcba']
  const padding = Math.max(2, Math.min(8, Math.round(height / 5)))
  const gap = height >= 20 ? 2 : 1
  const innerWidth = Math.max(1, width - padding * 2 - gap * (colors.length - 1))
  context.fillStyle = '#ffffff'
  context.fillRect(x, y, width, height)
  context.strokeStyle = '#171a18'
  context.lineWidth = 1
  context.strokeRect(x + .5, y + .5, width - 1, height - 1)
  colors.forEach((color, index) => {
    const left = x + padding + Math.floor(innerWidth * index / colors.length) + gap * index
    const right = x + padding + Math.floor(innerWidth * (index + 1) / colors.length) + gap * index
    context.fillStyle = color
    context.fillRect(left, y + padding, Math.max(1, right - left), Math.max(1, height - padding * 2))
  })
}

function drawBlockText(context: CanvasRenderingContext2D, text: string, x: number, y: number, scale: number, color: string) {
  context.fillStyle = color
  for (const character of text) {
    const glyph = blockGlyphs[character]
    if (glyph) glyph.forEach((row, rowIndex) => {
      for (let column = 0; column < 5; column += 1) {
        if (row & (0x10 >> column)) context.fillRect(x + column * scale, y + rowIndex * scale, scale, scale)
      }
    })
    x += 6 * scale
  }
}

function renderDeviceInfoInfo(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const dark = widget.style.background === 'black' || (widget.style.background === 'transparent' && widget.style.foreground === 'white')
  const foreground = dark ? '#ffffff' : roleColor(widget.style.foreground, document.colorCount)
  const accent = document.colorCount > 2 ? roleColor('accent', document.colorCount) : foreground
  let y = frame.y
  if (widget.data.showTitle !== false && frame.height >= 14) {
    const scale = Math.max(1, Math.min(widget.variant === 'large' ? 4 : 2, Math.floor(frame.width / 42), Math.floor(frame.height / 28) || 1))
    const titleWidth = 41 * scale
    const titleX = widget.style.align === 'center' ? frame.x + Math.round((frame.width - titleWidth) / 2) : frame.x
    drawBlockText(context, 'DEVICE', titleX, y, scale, dark && document.colorCount <= 2 ? foreground : accent)
    y += 7 * scale + Math.max(4, scale * 3)
  }
  const remaining = frame.y + frame.height - y
  const showBluetooth = widget.data.showBluetooth !== false && remaining >= 14
  const showScreen = widget.data.showScreen !== false && remaining >= (showBluetooth ? 28 : 14)
  const showActivation = widget.data.showActivation !== false && remaining >= (showBluetooth ? 46 : 28)
  const commands: DeviceTextCommand[] = []
  const lineHeight = widget.variant === 'large' ? 18 : 14
  if (showBluetooth) {
    if (!dark && frame.width >= 100) {
      commands.push({ text: 'BLUETOOTH', x: 0, y: y - frame.y, width: frame.width, align: widget.style.align })
      y += lineHeight
    }
    commands.push({ text: stripInkTagPrefix(sample.bluetoothName || 'IKT_42VM'), x: 0, y: y - frame.y, width: frame.width, align: widget.style.align })
    y += lineHeight
  }
  if (showScreen) {
    if (!dark && frame.width >= 100) {
      commands.push({ text: 'DISPLAY', x: 0, y: y - frame.y, width: frame.width, align: widget.style.align })
      y += lineHeight
    }
    const screenInfo = sample.screenInfo || '400x300 / BWR'
    if (dark && frame.width < 100 && screenInfo.includes('/')) {
      const [resolution, color] = screenInfo.split('/').map((part) => part.trim())
      commands.push({ text: resolution, x: 0, y: y - frame.y, width: frame.width, align: widget.style.align })
      y += lineHeight
      commands.push({ text: color, x: 0, y: y - frame.y, width: frame.width, align: widget.style.align })
    } else {
      commands.push({ text: screenInfo, x: 0, y: y - frame.y, width: frame.width, align: widget.style.align })
    }
  }
  drawDeviceTextMask(context, frame, 'wqy9-lunar', commands, foreground, widgetFontScale(widget))
  if (showActivation) {
    const label = sample.activated ? 'PRO' : 'InkTag'
    const badgeWidth = Math.max(38, Math.min(96, Math.round(frame.width * .62)))
    const badgeHeight = Math.max(13, Math.min(22, Math.round(frame.height * .16)))
    const badgeX = widget.style.align === 'center' ? frame.x + Math.round((frame.width - badgeWidth) / 2) : frame.x
    const badgeY = frame.y + frame.height - badgeHeight
    context.strokeStyle = dark ? foreground : accent
    context.lineWidth = 1
    context.beginPath()
    context.roundRect(badgeX + .5, badgeY + .5, badgeWidth - 1, badgeHeight - 1, 2)
    context.stroke()
    drawDeviceTextMask(context, { x: badgeX, y: badgeY, width: badgeWidth, height: badgeHeight }, 'wqy9-lunar', [{ text: label, x: 0, y: Math.max(0, Math.round((badgeHeight - 12) / 2)), width: badgeWidth, align: 'center' }], dark ? foreground : accent, widgetFontScale(widget))
  }
}

function renderQrWidget(context: CanvasRenderingContext2D, widget: DeviceGuiWidget, frame: DeviceGuiFrame) {
  const purpose = widget.data.qrPurpose || 'firmware'
  const label = purpose === 'firmware' ? (frame.width < 58 ? 'FW' : 'FIRMWARE') : purpose === 'webui' ? (frame.width < 58 ? 'WEB' : 'WEBUI') : (widget.data.titleText || 'QR')
  const value = purpose === 'firmware' ? INKTAG_RELEASE_URL : purpose === 'webui' ? INKTAG_WEB_URL : (widget.data.qrValue || INKTAG_WEB_URL)
  const labelHeight = widget.data.showLabel === false ? 0 : Math.min(14, Math.max(10, Math.round(frame.height * .12)))
  if (labelHeight) drawDeviceTextMask(context, { x: frame.x, y: frame.y, width: frame.width, height: labelHeight }, 'wqy9-lunar', [{ text: label, x: 0, y: 0, width: frame.width, align: 'center' }], '#171a18')
  const boxSize = Math.max(1, Math.min(frame.width, frame.height - labelHeight))
  const commonModules = purpose === 'custom' ? 21 : Math.max(createQrMatrix(INKTAG_RELEASE_URL).size, createQrMatrix(INKTAG_WEB_URL).size)
  drawQrCode(context, value, frame.x + Math.round((frame.width - boxSize) / 2), frame.y + labelHeight + Math.round((frame.height - labelHeight - boxSize) / 2), boxSize, '#111714', '#ffffff', commonModules)
}

function renderActivationStatus(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const color = widget.style.background === 'black' || (widget.style.background === 'transparent' && widget.style.foreground === 'white')
    ? '#ffffff'
    : roleColor('accent', document.colorCount)
  context.strokeStyle = color
  context.lineWidth = 1
  context.beginPath()
  context.roundRect(frame.x + .5, frame.y + .5, frame.width - 1, frame.height - 1, Math.min(2, frame.height / 4))
  context.stroke()
  drawDeviceTextMask(context, frame, 'wqy9-lunar', [{ text: sample.activated ? 'PRO' : 'InkTag', x: 0, y: Math.max(0, Math.round((frame.height - 12) / 2)), width: frame.width, align: 'center' }], color, widgetFontScale(widget))
}

function renderDeviceInfoFooter(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame) {
  const colorName = document.colorCount <= 2
    ? 'BW'
    : document.colorCount === 3
      ? 'BWR'
      : document.colorCount === 4
        ? 'BWRY'
        : `GRAY${document.colorCount}`
  drawDeviceTextMask(context, frame, widget.style.fontId, [
    { text: 'INKTAG E-PAPER DISPLAY', x: 0, y: Math.max(0, Math.round((frame.height - 12) / 2)), width: frame.width, align: 'left' },
    { text: colorName, x: 0, y: Math.max(0, Math.round((frame.height - 12) / 2)), width: frame.width, align: 'right' },
  ], roleColor(widget.style.foreground, document.colorCount), widgetFontScale(widget))
}

function renderStatusBar(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const now = sample.now
  const date = widget.data.dateFormat === 'md'
    ? `${now.getFullYear()}年${now.getMonth() + 1}月`
    : `${now.getFullYear()}年${now.getMonth() + 1}月${now.getDate()}日`
  const power = `温${Math.round(sample.temperature ?? 24)}℃ ${(sample.voltage ?? 3.01).toFixed(1)}V`
  const textFrame = { x: frame.x + 4, y: frame.y, width: Math.max(1, frame.width - 8), height: Math.max(1, frame.height - 3) }
  drawDeviceTextMask(context, textFrame, 'wqy9-lunar', [
    { text: date, x: 0, y: Math.max(0, Math.round((textFrame.height - 12) / 2)), width: textFrame.width * .58, align: 'left' },
    { text: power, x: textFrame.width * .48, y: Math.max(0, Math.round((textFrame.height - 12) / 2)), width: textFrame.width * .52, align: 'right' },
  ], roleColor(widget.style.foreground, document.colorCount), widgetFontScale(widget))
  context.strokeStyle = roleColor('black', document.colorCount)
  context.lineWidth = 1
  line(context, frame.x, frame.y + frame.height - 1, frame.x + frame.width, frame.y + frame.height - 1)
}

function renderDateHeader(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const now = sample.now
  const accent = roleColor('accent', document.colorCount)
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const topHeight = Math.max(16, Math.round(frame.height * .56))
  const topFrame = { x: frame.x, y: frame.y, width: frame.width, height: topHeight }
  const detailFrame = { x: frame.x, y: frame.y + topHeight, width: frame.width, height: Math.max(12, frame.height - topHeight) }
  const month = `${now.getFullYear()}年${now.getMonth() + 1}月`
  const detail = `${zodiacText(now)}  ${lunarText(now)}  [${isoWeek(now)}周]`
  const power = `${Math.round(sample.temperature ?? 24)}℃ ${(sample.voltage ?? 3.01).toFixed(1)}V`
  drawDeviceTextMask(context, topFrame, widget.variant === 'compact' ? 'wqy9-lunar' : 'wqy12-lunar', [
    { text: month, x: 0, y: Math.max(0, Math.round((topHeight - 16) / 2)), width: frame.width * .54, align: 'left' },
  ], accent, widgetFontScale(widget))
  drawDeviceTextMask(context, topFrame, 'wqy9-lunar', [
    { text: power, x: frame.width * .62, y: Math.max(0, Math.round((topHeight - 12) / 2)), width: frame.width * .38, align: 'right' },
  ], foreground, widgetFontScale(widget))
  drawDeviceTextMask(context, detailFrame, 'wqy9-lunar', [{ text: detail, x: 0, y: 0, width: detailFrame.width, align: 'left' }], foreground, widgetFontScale(widget))
}

function renderClockFooter(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const accent = roleColor('accent', document.colorCount)
  const now = sample.now
  const lunar = `星期${'日一二三四五六'[now.getDay()]} ${lunarText(now)}`
  if (widget.data.appearance === 'pill') {
    context.strokeStyle = foreground
    line(context, frame.x, frame.y + 1, frame.x + frame.width, frame.y + 1)
    const pillHeight = Math.max(16, Math.min(22, frame.height - 6))
    const pillY = frame.y + Math.round((frame.height - pillHeight) / 2) + 1
    context.fillStyle = accent
    context.beginPath()
    context.roundRect(frame.x + 6, pillY, Math.max(1, frame.width - 12), pillHeight, Math.min(6, pillHeight / 2))
    context.fill()
    drawDeviceTextMask(context, { x: frame.x + 6, y: pillY, width: frame.width - 12, height: pillHeight }, 'wqy9-lunar', [{ text: lunar, x: 0, y: Math.max(0, Math.round((pillHeight - 12) / 2)), width: frame.width - 12, align: 'center' }], '#ffffff', widgetFontScale(widget))
    return
  }
  const left = `${zodiacText(now)}  ${isoWeek(now)}周`
  const right = `离小暑还有${deviceGuiCountdownDays(widget.data.countdownDate || '2026-10-14', now)}天`
  const lineY = Math.max(0, Math.round((frame.height - 30) / 2))
  drawDeviceTextMask(context, frame, 'wqy12-lunar', [{ text: left, x: 0, y: lineY, width: frame.width * .5, align: 'left' }], foreground, widgetFontScale(widget))
  drawDeviceTextMask(context, frame, 'wqy9-lunar', [
    { text: `${now.getFullYear()}年`, x: 0, y: lineY + 18, width: frame.width * .5, align: 'left' },
    { text: right, x: frame.width * .5, y: lineY + 18, width: frame.width * .5, align: 'right' },
  ], foreground, widgetFontScale(widget))
  drawDeviceTextMask(context, frame, 'wqy12-lunar', [{ text: '小暑', x: frame.width * .62, y: lineY, width: frame.width * .38, align: 'right' }], accent, widgetFontScale(widget))
}

function renderCalendar(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const now = sample.now
  const weekStart = widget.data.weekStart ?? 1
  const minimal = widget.data.appearance === 'minimal'
  const headerHeight = widget.data.showWeekday === false ? 0 : widget.variant === 'compact' ? 16 : widget.variant === 'large' ? 28 : 22
  const rows = 6
  const cellWidth = frame.width / 7
  const cellHeight = (frame.height - headerHeight) / rows
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const accent = roleColor('accent', document.colorCount)
  context.strokeStyle = foreground
  context.lineWidth = 1
  if (!minimal) {
    for (let column = 1; column < 7; column += 1) line(context, frame.x + column * cellWidth, frame.y + headerHeight, frame.x + column * cellWidth, frame.y + frame.height)
    for (let row = 0; row <= rows; row += 1) line(context, frame.x, frame.y + headerHeight + row * cellHeight, frame.x + frame.width, frame.y + headerHeight + row * cellHeight)
  }

  const textFrame = { x: frame.x, y: frame.y, width: frame.width, height: frame.height }
  const headerBlack: DeviceTextCommand[] = []
  const headerRed: DeviceTextCommand[] = []
  const numberBlack: DeviceTextCommand[] = []
  const numberRed: DeviceTextCommand[] = []
  const numberWhite: DeviceTextCommand[] = []
  const smallBlack: DeviceTextCommand[] = []
  const smallRed: DeviceTextCommand[] = []
  const smallWhite: DeviceTextCommand[] = []
  const weekNames = '日一二三四五六'
  if (headerHeight > 0) {
    for (let index = 0; index < 7; index += 1) {
      const day = (weekStart + index) % 7
      const command = { text: '周' + weekNames[day], x: index * cellWidth, y: 1, width: cellWidth, align: 'center' as const }
      ;(day === 0 || day === 6 ? headerRed : headerBlack).push(command)
    }
  }
  const first = new Date(now.getFullYear(), now.getMonth(), 1)
  const offset = (first.getDay() - weekStart + 7) % 7
  const days = new Date(now.getFullYear(), now.getMonth() + 1, 0).getDate()
  for (let day = 1; day <= days; day += 1) {
    const slot = offset + day - 1
    const column = slot % 7
    const row = Math.floor(slot / 7)
    const y = headerHeight + row * cellHeight + 2
    const weekend = ((weekStart + column) % 7 === 0 || (weekStart + column) % 7 === 6)
    const command = { text: String(day), x: column * cellWidth + 2, y, width: cellWidth - 4, align: 'left' as const }
    const today = day === now.getDate()
    if (minimal && today) {
      context.fillStyle = accent
      const markerWidth = Math.max(10, cellWidth - 4)
      const markerHeight = Math.max(12, Math.min(cellHeight - 2, widget.variant === 'compact' ? 15 : 25))
      roundRect(context, frame.x + column * cellWidth + 2, frame.y + y - 1, markerWidth, markerHeight, Math.min(8, markerHeight / 2))
      context.fill()
      numberWhite.push(command)
    } else {
      ;(weekend ? numberRed : numberBlack).push(command)
    }
    if (widget.variant !== 'compact') {
      const holiday = holidays[`${now.getMonth() + 1}-${day}`]
      const smallCommand = { text: holiday && widget.data.showHolidays !== false ? holiday : widget.data.showLunar !== false ? day % 15 === 1 ? '初一' : day % 15 === 0 ? '十五' : '' : '', x: column * cellWidth + 2, y: y + 17, width: cellWidth - 4, align: 'center' as const }
      if (smallCommand.text) (today ? smallWhite : holiday ? smallRed : smallBlack).push(smallCommand)
    }
  }
  const headerFont = widget.variant === 'large' ? 'wqy12-lunar' : 'wqy9-lunar'
  const numberFont = widget.variant === 'large' ? 'helv-bold-18-num' : widget.variant === 'compact' ? 'wqy9-lunar' : 'helv-bold-14-num'
  const fontScaleQ8 = widgetFontScale(widget)
  drawDeviceTextMask(context, textFrame, headerFont, headerBlack, foreground, fontScaleQ8)
  drawDeviceTextMask(context, textFrame, headerFont, headerRed, accent, fontScaleQ8)
  drawDeviceTextMask(context, textFrame, numberFont, numberBlack, foreground, fontScaleQ8)
  drawDeviceTextMask(context, textFrame, numberFont, numberRed, accent, fontScaleQ8)
  drawDeviceTextMask(context, textFrame, numberFont, numberWhite, '#ffffff', fontScaleQ8)
  drawDeviceTextMask(context, textFrame, widget.variant === 'large' ? 'wqy12-lunar' : 'wqy9-lunar', smallBlack, foreground, fontScaleQ8)
  drawDeviceTextMask(context, textFrame, widget.variant === 'large' ? 'wqy12-lunar' : 'wqy9-lunar', smallRed, accent, fontScaleQ8)
  drawDeviceTextMask(context, textFrame, widget.variant === 'large' ? 'wqy12-lunar' : 'wqy9-lunar', smallWhite, '#ffffff', fontScaleQ8)
}

function renderDigitalClock(context: CanvasRenderingContext2D, frame: DeviceGuiFrame, now: Date, color: string, timeFormat?: string) {
  const hour = timeFormat === '12h' ? now.getHours() % 12 || 12 : now.getHours()
  const text = `${String(hour).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}`
  const digitWidth = 5
  const gap = 1.4
  const totalUnits = digitWidth * 4 + gap * 3 + 2
  const scale = Math.max(1, Math.floor(Math.min(frame.height / 9, frame.width / totalUnits)))
  let x = frame.x + Math.round((frame.width - totalUnits * scale) / 2)
  const y = frame.y + Math.round((frame.height - 9 * scale) / 2)
  for (const char of text) {
    if (char === ':') {
      context.fillStyle = color
      context.fillRect(x, y + 2 * scale, scale, scale)
      context.fillRect(x, y + 6 * scale, scale, scale)
      x += 2 * scale
    } else {
      drawSevenSegment(context, Number(char), x, y, scale, color)
      x += (digitWidth + gap) * scale
    }
  }
}

function renderSevenSegmentValue(context: CanvasRenderingContext2D, frame: DeviceGuiFrame, value: string, color: string) {
  const digits = value.replace(/\D/g, '')
  if (!digits) return
  const units = digits.length * 5 + Math.max(0, digits.length - 1) * 1.4
  const scale = Math.max(1, Math.floor(Math.min(frame.height / 9, frame.width / units)))
  let x = frame.x + Math.round((frame.width - units * scale) / 2)
  const y = frame.y + Math.round((frame.height - 9 * scale) / 2)
  for (const char of digits) {
    drawSevenSegment(context, Number(char), x, y, scale, color)
    x += 6.4 * scale
  }
}

function renderCountdown(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const days = deviceGuiCountdownDays(widget.data.countdownDate || '2027-01-01', sample.now)
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  if (widget.data.appearance !== 'seven-segment') {
    const label = '离目标还有' + days + '天'
    drawDeviceTextMask(context, frame, widget.style.fontId, [{ text: label, x: 0, y: Math.max(0, (frame.height - 16) / 2), width: frame.width, align: widget.style.align }], foreground, widgetFontScale(widget))
    return
  }

  const digits = String(days)
  const units = digits.length * 5 + Math.max(0, digits.length - 1) * 1.4
  const numberHeight = Math.max(12, Math.round(frame.height * .72))
  const scale = Math.max(1, Math.floor(Math.min(numberHeight / 9, frame.width / units)))
  let x = frame.x + Math.round((frame.width - units * scale) / 2)
  const y = frame.y + Math.max(0, Math.round((numberHeight - 9 * scale) / 2))
  for (const char of digits) {
    drawSevenSegment(context, Number(char), x, y, scale, foreground)
    x += 6.4 * scale
  }
  const footer = widget.data.footerText || '天'
  const footerFrame = { x: frame.x, y: frame.y + numberHeight, width: frame.width, height: Math.max(1, frame.height - numberHeight) }
  drawDeviceTextMask(context, footerFrame, 'wqy9-lunar', [{ text: footer, x: 0, y: 0, width: footerFrame.width, align: 'center' }], foreground, widgetFontScale(widget))
}

function renderWeekdayStrip(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame) {
  const weekStart = widget.data.weekStart ?? 0
  const cellWidth = frame.width / 7
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const accent = roleColor('accent', document.colorCount)
  const dark = roleColor('black', document.colorCount)
  const black: DeviceTextCommand[] = []
  const red: DeviceTextCommand[] = []
  const white: DeviceTextCommand[] = []
  const weekNames = '日一二三四五六'
  for (let index = 0; index < 7; index += 1) {
    const day = (weekStart + index) % 7
    const weekend = day === 0 || day === 6
    const command = { text: weekNames[day], x: index * cellWidth, y: Math.max(0, (frame.height - 12) / 2), width: cellWidth, align: 'center' as const }
    if (widget.data.appearance === 'band') {
      context.fillStyle = weekend ? accent : dark
      context.fillRect(frame.x + index * cellWidth, frame.y, Math.ceil(cellWidth), frame.height)
      white.push(command)
    } else {
      ;(weekend ? red : black).push(command)
    }
  }
  drawDeviceTextMask(context, frame, 'wqy9-lunar', black, foreground, widgetFontScale(widget))
  drawDeviceTextMask(context, frame, 'wqy9-lunar', red, accent, widgetFontScale(widget))
  drawDeviceTextMask(context, frame, 'wqy9-lunar', white, '#ffffff', widgetFontScale(widget))
}

function renderAgendaList(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame) {
  const rows = (widget.data.text || '').split('\n').map((row) => row.trim()).filter(Boolean).slice(0, 8)
  if (!rows.length) return
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const accent = roleColor('accent', document.colorCount)
  const title = widget.data.titleText || ''
  const subtitle = widget.data.subtitleText || ''
  const footer = widget.data.footerText || ''
  const headerHeight = title ? Math.max(30, Math.min(62, frame.height * .22)) : 0
  const footerHeight = footer ? Math.max(24, Math.min(46, frame.height * .16)) : 0
  const listHeight = Math.max(1, frame.height - headerHeight - footerHeight - 4)
  const rowHeight = listHeight / rows.length
  if (headerHeight) {
    context.fillStyle = accent
    context.fillRect(frame.x, frame.y, frame.width, headerHeight)
    context.fillStyle = '#ffffff'
    context.textAlign = 'center'
    context.textBaseline = 'middle'
    context.font = '700 ' + Math.max(12, Math.floor(headerHeight * .34)) + 'px sans-serif'
    context.fillText(title, frame.x + frame.width / 2, frame.y + headerHeight * .35, frame.width - 8)
    context.font = '600 ' + Math.max(9, Math.floor(headerHeight * .22)) + 'px sans-serif'
    context.fillText(subtitle, frame.x + frame.width / 2, frame.y + headerHeight * .72, frame.width - 8)
  }
  const fontSize = Math.max(9, Math.min(22, Math.floor(rowHeight * .42)))
  context.fillStyle = foreground
  context.strokeStyle = foreground
  context.lineWidth = 1
  context.font = '700 ' + fontSize + 'px sans-serif'
  context.textBaseline = 'middle'
  context.textAlign = 'left'
  for (let index = 0; index < rows.length; index += 1) {
    const parts = rows[index].split('|')
    const content = parts.length > 1 ? parts[0] + '  ' + parts.slice(1).join(' ') : parts[0]
    const y = frame.y + headerHeight + 4 + index * rowHeight
    const prefixWidth = widget.data.appearance === 'checkbox' ? Math.min(18, rowHeight * .36) : 0
    if (widget.data.appearance === 'checkbox') context.strokeRect(frame.x + 1, y + (rowHeight - prefixWidth) / 2, prefixWidth, prefixWidth)
    const label = widget.data.appearance === 'numbered' ? String(index + 1) + '. ' + content : content
    context.fillText(label, frame.x + (prefixWidth ? prefixWidth + 7 : 2), y + rowHeight * .43, Math.max(1, frame.width - prefixWidth - 8))
    context.setLineDash([2, 2])
    line(context, frame.x + (prefixWidth ? prefixWidth + 7 : 2), y + rowHeight - 3, frame.x + frame.width, y + rowHeight - 3)
    context.setLineDash([])
  }
  if (footerHeight) {
    context.fillStyle = roleColor('black', document.colorCount)
    context.fillRect(frame.x, frame.y + frame.height - footerHeight, frame.width, footerHeight)
    context.fillStyle = '#ffffff'
    context.textAlign = 'center'
    context.font = '700 ' + Math.max(10, Math.floor(footerHeight * .42)) + 'px sans-serif'
    context.fillText(footer, frame.x + frame.width / 2, frame.y + frame.height - footerHeight / 2, frame.width - 8)
  }
}

function renderScheduleTable(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame) {
  const rows = (widget.data.text || '').split('\n').map((row) => row.split('|').slice(0, 8)).filter((row) => row.some(Boolean)).slice(0, 8)
  if (!rows.length) return
  const columns = Math.max(1, Math.min(8, widget.data.columns || Math.max(...rows.map((row) => row.length))))
  const titleHeight = widget.data.titleText ? Math.max(22, Math.min(48, frame.height * .14)) : 0
  const tableFrame = { x: frame.x, y: frame.y + titleHeight, width: frame.width, height: Math.max(1, frame.height - titleHeight) }
  const cellWidth = frame.width / columns
  const cellHeight = tableFrame.height / rows.length
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const accent = roleColor('accent', document.colorCount)
  context.strokeStyle = foreground
  context.lineWidth = 1
  if (titleHeight) {
    context.fillStyle = foreground
    context.textAlign = 'center'
    context.textBaseline = 'middle'
    context.font = '700 ' + Math.max(11, Math.floor(titleHeight * .42)) + 'px sans-serif'
    context.fillText(widget.data.titleText || '', frame.x + frame.width / 2, frame.y + titleHeight / 2, frame.width - 8)
  }
  context.strokeRect(tableFrame.x + .5, tableFrame.y + .5, tableFrame.width - 1, tableFrame.height - 1)
  for (let column = 1; column < columns; column += 1) line(context, tableFrame.x + column * cellWidth, tableFrame.y, tableFrame.x + column * cellWidth, tableFrame.y + tableFrame.height)
  for (let row = 1; row < rows.length; row += 1) line(context, tableFrame.x, tableFrame.y + row * cellHeight, tableFrame.x + tableFrame.width, tableFrame.y + row * cellHeight)
  const fontSize = Math.max(7, Math.min(18, Math.floor(Math.min(cellHeight * .42, cellWidth * .24))))
  context.textAlign = 'center'
  context.textBaseline = 'middle'
  context.font = '600 ' + fontSize + 'px sans-serif'
  const headerRows = Math.max(0, Math.min(rows.length, widget.data.headerRows || 0))
  rows.forEach((row, rowIndex) => {
    row.forEach((text, columnIndex) => {
      context.fillStyle = rowIndex < headerRows || (columnIndex === 0 && rowIndex > 0) ? accent : foreground
      context.fillText(text, tableFrame.x + (columnIndex + .5) * cellWidth, tableFrame.y + (rowIndex + .5) * cellHeight, Math.max(1, cellWidth - 4))
    })
  })
}

function renderDivider(context: CanvasRenderingContext2D, frame: DeviceGuiFrame, color: string, orientation?: string) {
  context.strokeStyle = color
  context.lineWidth = 1
  if (orientation === 'vertical') line(context, frame.x + frame.width / 2, frame.y, frame.x + frame.width / 2, frame.y + frame.height)
  else line(context, frame.x, frame.y + frame.height / 2, frame.x + frame.width, frame.y + frame.height / 2)
}

function renderRasterText(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame) {
  const lines = (widget.data.text || '').split('\n').slice(0, 4)
  const lineHeight = Math.max(10, Math.min(42, Math.floor(frame.height / Math.max(1, lines.length))))
  const scale = widgetFontScale(widget) / 256
  const rasterFont = rasterFontOptions.find((font) => font.id === widget.data.rasterFontId) || rasterFontOptions[0]
  const x = widget.style.align === 'left' ? frame.x : widget.style.align === 'right' ? frame.x + frame.width : frame.x + frame.width / 2
  context.fillStyle = roleColor(widget.style.foreground, document.colorCount)
  context.textAlign = widget.style.align
  context.textBaseline = 'middle'
  context.font = (widget.data.rasterFontId === 'ma-shan-zheng' ? '400 ' : '700 ') + Math.max(6, Math.floor(lineHeight * .58 * scale)) + 'px ' + rasterFont.family
  lines.forEach((text, index) => context.fillText(text, x, frame.y + (index + .5) * lineHeight, frame.width))
}

const segmentDigits = ['abcdef', 'bc', 'abdeg', 'abcdg', 'bcfg', 'acdfg', 'acdefg', 'abc', 'abcdefg', 'abcdfg']

function drawSevenSegment(context: CanvasRenderingContext2D, digit: number, x: number, y: number, scale: number, color: string) {
  const segments: Record<string, [number, number, number, number]> = {
    a: [1, 0, 3, 1], b: [4, 1, 1, 3], c: [4, 5, 1, 3], d: [1, 8, 3, 1],
    e: [0, 5, 1, 3], f: [0, 1, 1, 3], g: [1, 4, 3, 1],
  }
  context.fillStyle = color
  for (const id of segmentDigits[digit]) {
    const part = segments[id]
    context.fillRect(x + part[0] * scale, y + part[1] * scale, part[2] * scale, part[3] * scale)
  }
}

function renderAnalogClock(context: CanvasRenderingContext2D, frame: DeviceGuiFrame, now: Date, color: string, accent: string) {
  const radius = Math.max(10, Math.min(frame.width, frame.height) / 2 - 3)
  const cx = frame.x + frame.width / 2
  const cy = frame.y + frame.height / 2
  context.strokeStyle = color
  context.lineWidth = Math.max(1, radius / 35)
  context.beginPath()
  context.arc(cx, cy, radius, 0, Math.PI * 2)
  context.stroke()
  for (let index = 0; index < 12; index += 1) {
    const angle = index * Math.PI / 6 - Math.PI / 2
    const length = index % 3 === 0 ? radius * .14 : radius * .08
    line(context, cx + Math.cos(angle) * (radius - length), cy + Math.sin(angle) * (radius - length), cx + Math.cos(angle) * (radius - 3), cy + Math.sin(angle) * (radius - 3))
  }
  const minute = now.getMinutes()
  const hourAngle = ((now.getHours() % 12) + minute / 60) * Math.PI / 6 - Math.PI / 2
  const minuteAngle = minute * Math.PI / 30 - Math.PI / 2
  hand(context, cx, cy, hourAngle, radius * .52, Math.max(2, radius / 15), color)
  hand(context, cx, cy, minuteAngle, radius * .75, Math.max(1, radius / 24), accent)
  context.fillStyle = color
  context.beginPath()
  context.arc(cx, cy, Math.max(2, radius / 22), 0, Math.PI * 2)
  context.fill()
}

function renderBattery(context: CanvasRenderingContext2D, document: DeviceGuiDocument, widget: DeviceGuiWidget, frame: DeviceGuiFrame, sample: DeviceGuiSampleData) {
  const foreground = roleColor(widget.style.foreground, document.colorCount)
  const showValue = widget.data.showValue !== false
  const bodyWidth = Math.min(frame.width * (showValue ? .48 : .82), frame.height * 1.8)
  const bodyHeight = Math.min(frame.height - 6, bodyWidth * .5)
  const x = frame.x
  const y = frame.y + (frame.height - bodyHeight) / 2
  context.strokeStyle = foreground
  context.lineWidth = 1
  context.strokeRect(x, y, bodyWidth, bodyHeight)
  context.fillStyle = foreground
  context.fillRect(x + bodyWidth, y + bodyHeight * .3, 3, bodyHeight * .4)
  context.fillRect(x + 2, y + 2, Math.max(0, (bodyWidth - 4) * Math.max(0, Math.min(100, sample.batteryPercent)) / 100), bodyHeight - 4)
  if (!showValue) return
  const textFrame = { x: x + bodyWidth + 6, y: frame.y, width: Math.max(1, frame.width - bodyWidth - 6), height: frame.height }
  drawDeviceTextMask(context, textFrame, 'wqy9-lunar', [{ text: `${sample.batteryPercent}%`, x: 0, y: Math.max(0, (frame.height - 12) / 2) }], foreground)
}

function renderImagePanel(context: CanvasRenderingContext2D, frame: DeviceGuiFrame, color: string, accent: string) {
  context.strokeStyle = color
  context.strokeRect(frame.x, frame.y, frame.width, frame.height)
  context.fillStyle = accent
  context.beginPath()
  context.arc(frame.x + frame.width * .72, frame.y + frame.height * .28, Math.max(2, Math.min(frame.width, frame.height) * .08), 0, Math.PI * 2)
  context.fill()
  context.fillStyle = color
  context.beginPath()
  context.moveTo(frame.x + 2, frame.y + frame.height - 2)
  context.lineTo(frame.x + frame.width * .38, frame.y + frame.height * .45)
  context.lineTo(frame.x + frame.width * .58, frame.y + frame.height * .68)
  context.lineTo(frame.x + frame.width - 2, frame.y + frame.height * .34)
  context.lineTo(frame.x + frame.width - 2, frame.y + frame.height - 2)
  context.fill()
}

function renderCanvasRegion(context: CanvasRenderingContext2D, frame: DeviceGuiFrame, color: string) {
  context.strokeStyle = color
  context.setLineDash([3, 3])
  context.strokeRect(frame.x + .5, frame.y + .5, frame.width - 1, frame.height - 1)
  context.setLineDash([])
  line(context, frame.x, frame.y, frame.x + frame.width, frame.y + frame.height)
  line(context, frame.x + frame.width, frame.y, frame.x, frame.y + frame.height)
}

// Layouts are authored as BWR. A BW target collapses the red accent to black without changing geometry or stored styles.
export function deviceGuiRoleColor(role: DeviceGuiStyle['foreground'], colorCount: number) {
  if (role === 'white') return '#ffffff'
  if (role === 'accent') return colorCount > 2 ? '#c82d28' : '#222222'
  if (role === 'transparent') return 'rgba(0,0,0,0)'
  return '#171a18'
}

const roleColor = deviceGuiRoleColor

function inset(frame: DeviceGuiFrame, padding: number): DeviceGuiFrame {
  return {
    x: frame.x + padding,
    y: frame.y + padding,
    width: Math.max(1, frame.width - padding * 2),
    height: Math.max(1, frame.height - padding * 2),
  }
}

function line(context: CanvasRenderingContext2D, x1: number, y1: number, x2: number, y2: number) {
  context.beginPath()
  context.moveTo(Math.round(x1) + .5, Math.round(y1) + .5)
  context.lineTo(Math.round(x2) + .5, Math.round(y2) + .5)
  context.stroke()
}

function hand(context: CanvasRenderingContext2D, cx: number, cy: number, angle: number, length: number, width: number, color: string) {
  context.strokeStyle = color
  context.lineWidth = width
  context.lineCap = 'round'
  context.beginPath()
  context.moveTo(cx, cy)
  context.lineTo(cx + Math.cos(angle) * length, cy + Math.sin(angle) * length)
  context.stroke()
  context.lineCap = 'butt'
}

function roundRect(context: CanvasRenderingContext2D, x: number, y: number, width: number, height: number, radius: number) {
  context.beginPath()
  context.roundRect(x, y, width, height, radius)
}

function lunarText(date: Date) {
  try {
    return new Intl.DateTimeFormat('zh-CN-u-ca-chinese', { month: 'long', day: 'numeric' }).format(date)
  } catch {
    return '农历七月十三'
  }
}

function zodiacText(date: Date) {
  const animals = '鼠牛虎兔龙蛇马羊猴鸡狗猪'
  return animals[(date.getFullYear() - 4) % 12] + '年'
}

function stripInkTagPrefix(value: string) {
  return value.replace(/^(?:IKT|InkTag)_/i, '')
}

function isoWeek(date: Date) {
  const target = new Date(Date.UTC(date.getFullYear(), date.getMonth(), date.getDate()))
  const day = target.getUTCDay() || 7
  target.setUTCDate(target.getUTCDate() + 4 - day)
  const yearStart = new Date(Date.UTC(target.getUTCFullYear(), 0, 1))
  return Math.ceil((((target.getTime() - yearStart.getTime()) / 86400000) + 1) / 7)
}

function startOfDay(date: Date) {
  return new Date(date.getFullYear(), date.getMonth(), date.getDate())
}

export function deviceGuiCountdownDays(targetDate: string, now: Date) {
  const target = parseLocalDate(targetDate)
  return Math.max(0, Math.ceil((target.getTime() - startOfDay(now).getTime()) / 86400000))
}

function parseLocalDate(value: string) {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value)
  return match ? new Date(Number(match[1]), Number(match[2]) - 1, Number(match[3])) : new Date(value)
}
