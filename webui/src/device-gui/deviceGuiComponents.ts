import {
  DEVICE_BLACK,
  DEVICE_RED,
  DEVICE_YELLOW,
  DEVICE_WHITE,
  DevicePixelGfx,
  type DevicePixelColor,
} from './devicePixelGfx'
import type {
  DeviceGuiAlign,
  DeviceGuiBorder,
  DeviceGuiFontId,
  DeviceGuiFrame,
  DeviceGuiVerticalAlign,
} from './model'
import { nativeSceneGroup } from './nativeScene'
import {drawSegmentNumber} from '../../../packages/inktag-editor-core/src/segmentDigits'

const div = (value: number, divisor: number) => Math.trunc(value / divisor)
const clamp = (value: number, minimum: number, maximum: number) => Math.max(minimum, Math.min(maximum, value))

export interface DeviceTextOptions {
  baselineY?: number
  fontId?: DeviceGuiFontId
  fontScaleQ8?: number
  foreground?: DevicePixelColor
  background?: DevicePixelColor
  transparent?: boolean
  align?: DeviceGuiAlign
  verticalAlign?: DeviceGuiVerticalAlign
  padding?: number
}

export interface FittedDeviceTextOptions extends DeviceTextOptions {
  minimumScaleQ8?: number
}

export interface DeviceBorderOptions {
  border?: DeviceGuiBorder
  width?: 0 | 1 | 2
  radius?: 0 | 2 | 4 | 8
  color?: DevicePixelColor
  dotLength?: number
  spaceLength?: number
}

export function insetDeviceFrame(frame: DeviceGuiFrame, padding: number): DeviceGuiFrame {
  padding = Math.max(0, Math.trunc(padding))
  return {
    x: Math.trunc(frame.x) + padding,
    y: Math.trunc(frame.y) + padding,
    width: Math.max(0, Math.trunc(frame.width) - padding * 2),
    height: Math.max(0, Math.trunc(frame.height) - padding * 2),
  }
}

export function drawCenteredDeviceText(gfx: DevicePixelGfx, x: number, baselineY: number, width: number, text: string) {
  gfx.setCursor(Math.trunc(x) + div(Math.trunc(width) - gfx.getUTF8Width(text), 2), Math.trunc(baselineY))
  gfx.print(text)
}

export function drawRightDeviceText(gfx: DevicePixelGfx, right: number, baselineY: number, text: string) {
  gfx.setCursor(Math.trunc(right) - gfx.getUTF8Width(text), Math.trunc(baselineY))
  gfx.print(text)
}

/** Draw one device-font line using only integer geometry. */
export function drawDeviceText(
  gfx: DevicePixelGfx,
  frame: DeviceGuiFrame,
  text: string,
  options: DeviceTextOptions = {},
) {
  const content = insetDeviceFrame(frame, options.padding ?? 0)
  gfx.setFont(options.fontId ?? 'wqy9-lunar')
  gfx.setFontScale(options.fontScaleQ8 ?? 256)
  gfx.setTextColor(options.foreground ?? DEVICE_BLACK, options.background ?? DEVICE_WHITE)
  gfx.setFontMode(options.transparent ?? true)
  const textWidth = gfx.getUTF8Width(text)
  const x = options.align === 'right'
    ? content.x + content.width - textWidth
    : options.align === 'center'
      ? content.x + div(content.width - textWidth, 2)
      : content.x
  const fontHeight = gfx.getFontHeight()
  const baselineY = options.baselineY ?? (options.verticalAlign === 'top'
    ? content.y + gfx.getFontAscent()
    : options.verticalAlign === 'bottom'
      ? content.y + content.height + gfx.getFontDescent()
      : content.y + div(content.height - fontHeight, 2) + gfx.getFontAscent())
  gfx.setCursor(x, baselineY)
  gfx.print(text)
  return { x, baselineY, width: textWidth, height: fontHeight }
}

/** Fit one device-font line to an integer frame without browser typography. */
export function drawFittedDeviceText(
  gfx: DevicePixelGfx,
  frame: DeviceGuiFrame,
  text: string,
  options: FittedDeviceTextOptions = {},
) {
  const content = insetDeviceFrame(frame, options.padding ?? 0)
  const fontId = options.fontId ?? 'wqy9-lunar'
  const minimum = options.minimumScaleQ8 ?? 96
  let scale = Math.max(minimum, options.fontScaleQ8 ?? 256)
  gfx.setFont(fontId)
  gfx.setFontScale(scale)
  while (scale > minimum && (gfx.getUTF8Width(text) > content.width || gfx.getFontHeight() > content.height)) {
    scale = Math.max(minimum, scale - 16)
    gfx.setFontScale(scale)
  }
  return drawDeviceText(gfx, frame, text, { ...options, fontId, fontScaleQ8: scale })
}

export function drawDeviceBorder(gfx: DevicePixelGfx, frame: DeviceGuiFrame, options: DeviceBorderOptions = {}) {
  const border = options.border ?? 'solid'
  const width = options.width ?? 1
  const radius = options.radius ?? 0
  const color = options.color ?? DEVICE_BLACK
  if (border === 'none' || width === 0 || frame.width <= 0 || frame.height <= 0) return
  for (let inset = 0; inset < width; inset += 1) {
    const x = Math.trunc(frame.x) + inset
    const y = Math.trunc(frame.y) + inset
    const w = Math.trunc(frame.width) - inset * 2
    const h = Math.trunc(frame.height) - inset * 2
    if (w <= 0 || h <= 0) break
    if (border === 'dotted') {
      const dots = options.dotLength ?? 2
      const spaces = options.spaceLength ?? 2
      gfx.drawDottedLine(x, y, x + w - 1, y, color, dots, spaces)
      gfx.drawDottedLine(x + w - 1, y, x + w - 1, y + h - 1, color, dots, spaces)
      gfx.drawDottedLine(x + w - 1, y + h - 1, x, y + h - 1, color, dots, spaces)
      gfx.drawDottedLine(x, y + h - 1, x, y, color, dots, spaces)
    } else if (radius > 0) gfx.drawRoundRect(x, y, w, h, Math.min(radius, div(Math.min(w, h), 2)), color)
    else gfx.drawRect(x, y, w, h, color)
  }
}

/** Draw a row-major, MSB-first QR/module bitmap with a four-module quiet zone. */
export function drawDeviceQr(
  gfx: DevicePixelGfx,
  x: number,
  y: number,
  boxSide: number,
  data: ArrayLike<number>,
  modules: number,
  scale: number,
  foreground: DevicePixelColor = DEVICE_BLACK,
  background: DevicePixelColor = DEVICE_WHITE,
  fillBox = false,
) {
  x = Math.trunc(x); y = Math.trunc(y); boxSide = Math.trunc(boxSide); modules = Math.trunc(modules); scale = Math.trunc(scale)
  if (boxSide <= 0 || modules <= 0 || scale <= 0) return
  const outer = fillBox ? boxSide : (modules + 8) * scale
  gfx.fillRect(x, y, boxSide, boxSide, background)
  x += div(boxSide - outer, 2)
  y += div(boxSide - outer, 2)
  for (let row = 0; row < modules; row += 1) {
    for (let column = 0; column < modules; column += 1) {
      const bit = row * modules + column
      if (((data[bit >> 3] ?? 0) & (0x80 >> (bit & 7))) !== 0) {
        const left = div((column + 4) * outer, modules + 8)
        const right = div((column + 5) * outer, modules + 8)
        const top = div((row + 4) * outer, modules + 8)
        const bottom = div((row + 5) * outer, modules + 8)
        gfx.fillRect(x + left, y + top, right - left, bottom - top, foreground)
      }
    }
  }
}

export function drawInkTagDeviceMark(
  gfx: DevicePixelGfx,
  frame: DeviceGuiFrame,
  colorCount: number,
  accent: DevicePixelColor = DEVICE_RED,
) {
  const x = Math.trunc(frame.x); const y = Math.trunc(frame.y)
  const width = Math.trunc(frame.width); const height = Math.trunc(frame.height)
  const padding = clamp(div(height, 5), 3, 8)
  const gap = height >= 20 ? 2 : 1
  const innerWidth = width - padding * 2
  const innerHeight = height - padding * 2
  const barCount = colorCount === 1 || colorCount === 2 || colorCount === 5 ? 2 : 3
  const usableWidth = innerWidth - gap * (barCount - 1)
  gfx.fillRect(x, y, width, height, DEVICE_WHITE)
  gfx.drawRect(x, y, width, height, DEVICE_BLACK)
  const colors = [
    DEVICE_BLACK,
    colorCount === 1 ? DEVICE_BLACK : colorCount === 5 ? DEVICE_YELLOW : accent,
    DEVICE_YELLOW,
  ] as const
  for (let index = 0; index < barCount; index += 1) {
    const left = x + div(usableWidth * index, barCount) + padding + index * gap
    const right = x + div(usableWidth * (index + 1), barCount) + padding + index * gap
    gfx.fillRect(left, y + padding, right - left, innerHeight, colors[index]!)
  }
}

export function deviceBatteryPercent(voltageMillivolts: number) {
  const sample = div(Math.trunc(voltageMillivolts) * 2047, 3600)
  if (sample > 1705) return 100
  if (sample > 1584) return 28 + div(div((sample - 1584) * 65536, 121) * 72, 65536)
  if (sample > 1360) return 4 + div(div((sample - 1360) * 65536, 224) * 24, 65536)
  if (sample > 1136) return div(div((sample - 1136) * 65536, 224) * 4, 65536)
  return 0
}

/** Returns the left edge so callers can place text immediately before it. */
export function drawDeviceBatteryIcon(
  gfx: DevicePixelGfx,
  right: number,
  y: number,
  iconWidth: number,
  voltageMillivolts: number,
  compact: boolean,
  color: DevicePixelColor = DEVICE_BLACK,
  height = compact ? 8 : 10,
) {
  const x = Math.trunc(right) - Math.trunc(iconWidth) - 2
  if (nativeSceneGroup(gfx, 'battery', '电池图标', 'graphic', { x, y, width: iconWidth + 2, height },
    () => drawDeviceBatteryIcon(gfx, right, y, iconWidth, voltageMillivolts, compact, color, height))) return x
  const level = clamp(deviceBatteryPercent(voltageMillivolts), 0, 100)
  gfx.fillRect(x, y, iconWidth + 2, height, DEVICE_WHITE)
  gfx.drawRect(x, y, iconWidth, height, color)
  gfx.fillRect(x + iconWidth, y + div(height - 2, 2), 2, 2, color)
  gfx.fillRect(x + 2, y + 2, div((iconWidth - 4) * level, 100), height - 4, color)
  return x
}

const compactWordGlyphs: Record<string, readonly string[]> = {
  O: ['01110', '10001', '10001', '10001', '10001', '10001', '01110'],
  P: ['11110', '10001', '10001', '11110', '10000', '10000', '10000'],
  R: ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
  A: ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
  G: ['01110', '10001', '10000', '10111', '10001', '10001', '01110'],
  I: ['11111', '00100', '00100', '00100', '00100', '00100', '11111'],
  K: ['10001', '10010', '10100', '11000', '10100', '10010', '10001'],
  N: ['10001', '11001', '11001', '10101', '10011', '10011', '10001'],
  T: ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
}

function drawCompactWord(gfx: DevicePixelGfx, x: number, y: number, scale: number, color: DevicePixelColor, text: string) {
  for (const character of text) {
    const glyph = compactWordGlyphs[character]
    glyph?.forEach((row, rowIndex) => {
      Array.from(row).forEach((pixel, columnIndex) => {
        if (pixel === '1') gfx.fillRect(x + columnIndex * scale, y + rowIndex * scale, scale, scale, color)
      })
    })
    x += character === ' ' ? 2 * scale : 5 * scale + 1
  }
}

function compactWordWidth(text: string, scale: number) {
  return Array.from(text).reduce((width, character) => width + (character === ' ' ? 2 * scale : 5 * scale + 1), 0) - 1
}

export interface InkTagEditionBadgeOptions {
  edition: 'base' | 'pro'
  colorCount: number
  align?: DeviceGuiAlign
}

/** Shared bottom-right InkTag edition mark used by every native page. */
export function drawInkTagEditionBadge(
  gfx: DevicePixelGfx,
  frame: DeviceGuiFrame,
  options: InkTagEditionBadgeOptions,
) {
  const label = options.edition === 'base' ? 'INKTAG' : 'INKTAG PRO'
  const blockScale = frame.width >= 122 && frame.height >= 22 ? 2 : 1
  const padding = blockScale + 1
  const badgeWidth = compactWordWidth(label, blockScale) + padding * 2
  const badgeHeight = Math.min(frame.height, 7 * blockScale + padding * 2)
  const align = options.align ?? 'right'
  const x = align === 'left'
    ? frame.x
    : align === 'center'
      ? frame.x + Math.max(0, div(frame.width - badgeWidth, 2))
      : frame.x + Math.max(0, frame.width - badgeWidth)
  const y = frame.y + div(frame.height - badgeHeight, 2)
  const redCapable = options.colorCount === 2 || options.colorCount === 3 ||
    options.colorCount === 10 || options.colorCount === 11
  const background = options.edition === 'base' && redCapable ? DEVICE_RED : DEVICE_BLACK
  gfx.fillRect(x, y, badgeWidth, badgeHeight, DEVICE_WHITE)
  gfx.drawRoundRect(x, y, badgeWidth, badgeHeight, clamp(div(badgeHeight, 3), 2, 6), background)
  drawCompactWord(gfx, x + padding, y + padding, blockScale, background, label)
  return { x, y, width: badgeWidth, height: badgeHeight }
}

/** Shared InkTag mark + wordmark header. The mark is deliberately size-capped. */
export function drawInkTagBrandTitle(gfx: DevicePixelGfx, frame: DeviceGuiFrame, colorCount: number) {
  const markHeight = clamp(div(frame.height * 2, 3), 10, 28)
  const markWidth = div(markHeight * 6, 5)
  const markY = frame.y + div(frame.height - markHeight, 2)
  const blockScale = frame.height >= 34 && frame.width >= 120 ? 2 : 1
  drawInkTagDeviceMark(gfx, { x: frame.x, y: markY, width: markWidth, height: markHeight }, colorCount)
  drawCompactWord(
    gfx,
    frame.x + markWidth + blockScale * 2,
    frame.y + div(frame.height - 7 * blockScale, 2),
    blockScale,
    DEVICE_BLACK,
    'INKTAG',
  )
}

// Icon ink only; kept in sync with GUI_BATTERY_RED_MAX_MV, not power admission.
export const DEVICE_BATTERY_RED_MAX_MV = 2600

export interface DevicePowerStatusOptions {
  baselineY?: number
  labelGap?: number
  temperature: number
  voltageMillivolts: number
  fontScaleQ8: number
  showVoltage?: boolean
  temperatureFont?: DeviceGuiFontId
  batteryWarningColor?: DevicePixelColor
}

/** Shared top-right temperature + battery status used by every v1 page. */
export function drawDevicePowerStatus(
  gfx: DevicePixelGfx,
  frame: DeviceGuiFrame,
  options: DevicePowerStatusOptions,
) {
  gfx.fillRect(frame.x, frame.y, frame.width, frame.height, DEVICE_WHITE)
  const compact = frame.height <= 20
  const iconWidth = compact ? 12 : frame.height >= 36 ? 32 : clamp(div(frame.width, 4), 16, 24)
  const iconHeight = compact ? 8 : frame.height >= 36 ? 14 : 10
  const iconX = frame.x + frame.width - iconWidth - 2
  const voltageTenths = div(options.voltageMillivolts + 50, 100)
  const voltage = `${div(voltageTenths, 10)}.${voltageTenths % 10}V`
  const temperature = options.temperature > -128 ? `${options.temperature}℃` : ''
  const minimumScale = frame.height < 36 ? 256 : 160
  let preferredScale = 256
  const temperatureFont = options.temperatureFont ?? (frame.height>20 ? 'wqy12-lunar' : 'wqy9-lunar')
  gfx.setFont(temperatureFont)
  gfx.setFontScale(preferredScale)
  const batteryGap = () => clamp(div(preferredScale * 2 + 128, 256), 2, 4)
  const labelGap = () => options.labelGap ?? clamp(div(preferredScale * 4 + 128, 256), 4, 7)
  const requiredWidth = () => {
    gfx.setFont(temperatureFont)
    const temperatureWidth=gfx.getUTF8Width(temperature)
    gfx.setFont('wqy9-lunar')
    const voltageWidth=gfx.getUTF8Width(voltage)
    gfx.setFont(temperatureFont)
    const labels = temperatureWidth + (options.showVoltage ? labelGap() + voltageWidth : 0)
    return labels + batteryGap() + (frame.x + frame.width - iconX)
  }
  while (preferredScale > minimumScale && (requiredWidth() > frame.width || gfx.getFontHeight() > frame.height)) {
    preferredScale = Math.max(minimumScale, preferredScale - 16)
    gfx.setFontScale(preferredScale)
  }
  const baseline = options.baselineY ?? frame.y + div(frame.height - gfx.getFontHeight(), 2) + gfx.getFontAscent()
  drawDeviceBatteryIcon(gfx, frame.x + frame.width,
    baseline + 1 - div(gfx.getFontAscent() + iconHeight, 2) - 2, iconWidth, options.voltageMillivolts, compact,
    options.voltageMillivolts > 0 && options.voltageMillivolts <= DEVICE_BATTERY_RED_MAX_MV
      ? options.batteryWarningColor ?? DEVICE_RED : DEVICE_BLACK, iconHeight)
  const textRight = iconX - batteryGap()
  gfx.setFont('wqy9-lunar')
  const voltageWidth = options.showVoltage ? gfx.getUTF8Width(voltage) : 0
  const voltageX = textRight - voltageWidth
  if (options.showVoltage) drawFittedDeviceText(gfx, {
    x: voltageX,
    y: frame.y,
    width: voltageWidth,
    height: frame.height,
  }, voltage, {
    baselineY: options.baselineY,
    align: 'right',
    fontScaleQ8: preferredScale,
    minimumScaleQ8: minimumScale,
    fontId: 'wqy9-lunar',
  })
  const temperatureWidth = Math.max(0, (options.showVoltage ? voltageX - labelGap() : textRight) - frame.x)
  if (temperatureWidth > 0) drawFittedDeviceText(gfx, {
    x: frame.x,
    y: frame.y,
    width: temperatureWidth,
    height: frame.height,
  }, temperature, {
    baselineY: options.baselineY,
    align: options.showVoltage ? 'right' : 'left',
    fontScaleQ8: preferredScale,
    minimumScaleQ8: minimumScale,
    fontId: temperatureFont,
  })
}

export interface DeviceActivationBadgeOptions {
  label: string
  foreground?: DevicePixelColor
  background?: DevicePixelColor
  radius?: number
  fontId?: DeviceGuiFontId
  fontScaleQ8?: number
}

export function drawDeviceActivationBadge(
  gfx: DevicePixelGfx,
  frame: DeviceGuiFrame,
  options: DeviceActivationBadgeOptions,
) {
  const foreground = options.foreground ?? DEVICE_RED
  const background = options.background ?? DEVICE_WHITE
  gfx.drawRoundRect(frame.x, frame.y, frame.width, frame.height, options.radius ?? 2, foreground)
  gfx.setFont(options.fontId ?? 'wqy9-lunar')
  gfx.setFontScale(options.fontScaleQ8 ?? 256)
  gfx.setTextColor(foreground, background)
  drawCenteredDeviceText(gfx, frame.x, frame.y + div(frame.height + 7, 2), frame.width, options.label)
}

export function drawSevenSegmentNumber(
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
  drawSegmentNumber(gfx,number,xLocation,yLocation,characterSize,foreground,background,digitCount,heightSize)
}

export function drawSevenSegmentTime(
  gfx: DevicePixelGfx,
  hour: number,
  minute: number,
  x: number,
  y: number,
  characterSize: number,
  digitCount: number,
  digitColor: DevicePixelColor = DEVICE_BLACK,
  colonColor: DevicePixelColor = digitColor,
) {
  drawSevenSegmentNumber(gfx, hour, x, y, characterSize, digitColor, DEVICE_WHITE, digitCount)
  x += digitCount * (11 * characterSize + 2)
  gfx.fillRect(x, y + div(9 * characterSize, 2) + 1, 2 * characterSize, 2 * characterSize, colonColor)
  gfx.fillRect(x, y + div(27 * characterSize, 2) + 3, 2 * characterSize, 2 * characterSize, colonColor)
  x += 4 * characterSize
  drawSevenSegmentNumber(gfx, minute, x, y, characterSize, digitColor, DEVICE_WHITE, digitCount)
}

export interface DeviceLowBatteryOptions {
  width: number
  height: number
  voltageMillivolts: number
  fontScaleQ8?: number
  colorCount?: number
  edition?: 'base' | 'pro'
}

/** Exact browser counterpart of inktag_gui.c::DrawLowBattery. */
export function drawDeviceLowBattery(gfx: DevicePixelGfx, options: DeviceLowBatteryOptions) {
  const width = Math.trunc(options.width)
  const height = Math.trunc(options.height)
  const accent = options.colorCount === 1 ? DEVICE_BLACK : DEVICE_RED
  gfx.setFontScale(options.fontScaleQ8 ?? 256)
  const margin = width < 180 ? 8 : 16
  let cardWidth = width - margin * 2
  let cardHeight = height < 160 ? height - 16 : height < 260 ? 132 : 164
  if (cardWidth > 320) cardWidth = 320
  if (cardHeight > height - 12) cardHeight = height - 12
  const cardX = div(width - cardWidth, 2)
  const cardY = div(height - cardHeight, 2)
  const iconWidth = cardWidth < 180 ? 44 : 58
  const iconHeight = cardWidth < 180 ? 22 : 28
  const iconX = cardX + div(cardWidth - iconWidth, 2)
  const iconY = cardY + 15
  gfx.fillRoundRect(cardX, cardY, cardWidth, cardHeight, 8, DEVICE_WHITE)
  gfx.drawRoundRect(cardX, cardY, cardWidth, cardHeight, 8, accent)
  gfx.drawRoundRect(cardX + 1, cardY + 1, cardWidth - 2, cardHeight - 2, 7, accent)
  gfx.drawRect(iconX, iconY, iconWidth, iconHeight, accent)
  gfx.fillRect(iconX + iconWidth, iconY + div(iconHeight, 3), 4, div(iconHeight, 3), accent)
  gfx.fillRect(iconX + 4, iconY + 4, div(iconWidth - 8, 4), iconHeight - 8, accent)
  gfx.setTextColor(accent, DEVICE_WHITE)
  gfx.setFont(width < 180 ? 'wqy9-lunar' : 'wqy12-lunar')
  drawCenteredDeviceText(gfx, cardX, iconY + iconHeight + 25, cardWidth, '电量低')
  gfx.setTextColor(DEVICE_BLACK, DEVICE_WHITE)
  gfx.setFont('wqy9-lunar')
  const voltage = `${div(options.voltageMillivolts, 1000)}.${div(options.voltageMillivolts % 1000, 10).toString().padStart(2, '0')} V`
  drawCenteredDeviceText(gfx, cardX, iconY + iconHeight + 48, cardWidth, voltage)
  if (cardHeight >= 145) {
    drawCenteredDeviceText(gfx, cardX, iconY + iconHeight + 69, cardWidth, '请尽快更换电池')
  }
  if (options.edition) {
    const badgeWidth = width > 500 ? 122 : 66
    const badgeHeight = width > 500 ? 22 : 13
    const badgeMargin = width > 500 ? 4 : 2
    drawInkTagEditionBadge(gfx, {
      x: width - badgeMargin - badgeWidth,
      y: badgeMargin,
      width: badgeWidth,
      height: badgeHeight,
    }, {
      edition: options.edition,
      colorCount: options.colorCount ?? 1,
      align: 'right',
    })
  }
}
