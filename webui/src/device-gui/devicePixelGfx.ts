import { U8g2, U8g2Font } from '../vendor/u8g2-js/index.js'
import { deviceFontData } from './fonts.generated'
import type { DeviceGuiFontId } from './model'

export const DEVICE_WHITE = 0
export const DEVICE_BLACK = 1
export const DEVICE_RED = 2
export const DEVICE_YELLOW = 3
export const DEVICE_BLUE = 4
export const DEVICE_GREEN = 5
export const DEVICE_ORANGE = 6

export type DevicePixelColor =
  | typeof DEVICE_WHITE
  | typeof DEVICE_BLACK
  | typeof DEVICE_RED
  | typeof DEVICE_YELLOW
  | typeof DEVICE_BLUE
  | typeof DEVICE_GREEN
  | typeof DEVICE_ORANGE

export type DevicePixelRotation = 0 | 1 | 2 | 3

export interface DevicePixelFrame {
  width: number
  height: number
  /** Row-major device palette indices: white=0, black=1, red=2. */
  pixels: Uint8Array
}

interface U8g2Runtime extends U8g2 {
  drawColor: number
  fontDecode: {
    fg_color: number
    bg_color: number
    is_transparent: number
    dir: number
  }
  fontInfo: {
    ascent_A: number
    descent_g: number
    ascent_para: number
    descent_para: number
  }
  glyphXOffset: number
  setFontPosBaseline(): void
  fontSetupDecode(glyphData: number): void
  drawGlyph(x: number, y: number, encoding: number): number
  getGlyphWidth(encoding: number): number
  setFontDirection(direction: number): void
  drawHVLine(x: number, y: number, length: number, direction: number): void
}

const fonts = new Map<DeviceGuiFontId, U8g2Font>()

function firmwareFont(id: DeviceGuiFontId) {
  let font = fonts.get(id)
  if (!font) {
    font = U8g2Font.fromBase64(deviceFontData[id].base64)
    fonts.set(id, font)
  }
  return font
}

function scaleFloor(delta: number, scale: number) {
  const value = delta * scale
  return value >= 0 ? Math.trunc(value / 256) : -Math.trunc((-value + 255) / 256)
}

function scaleCeil(delta: number, scale: number) {
  const value = delta * scale
  return value >= 0 ? Math.trunc((value + 255) / 256) : -Math.trunc(-value / 256)
}

/**
 * Integer-only browser counterpart of firmware/common/gui/Adafruit_GFX.c.
 * It intentionally does not draw through Canvas: browser antialiasing,
 * fractional coordinates and font substitution would make pixel parity
 * impossible to enforce. The frame always uses raw panel coordinates while
 * width/height expose the current logical rotation/viewport.
 */
export class DevicePixelGfx {
  readonly frame: DevicePixelFrame
  tx = 0
  ty = 0
  fontScaleQ8 = 256
  /** V1 pixel-edge sampling; the default deliberately preserves frozen V0. */
  fontRasterMode: 'legacy' | 'nearest' | 'coverage' | 'vertical-coverage' | 'strokes' = 'legacy'
  foreground: DevicePixelColor = DEVICE_BLACK
  background: DevicePixelColor = DEVICE_WHITE
  private fontId: DeviceGuiFontId = 'wqy9-lunar'
  private readonly u8g2: U8g2Runtime
  private fontOriginX = 0
  private fontOriginY = 0
  private rotation: DevicePixelRotation = 0
  private mirrorX = false
  private mirrorY = false
  private viewportX = 0
  private viewportY = 0
  private viewportWidth: number
  private viewportHeight: number
  private logicalWidth: number
  private logicalHeight: number
  private windowX = 0
  private windowY = 0
  private windowWidth: number
  private windowHeight: number

  constructor(width: number, height: number) {
    if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0) {
      throw new RangeError('device pixel frame dimensions must be positive integers')
    }
    this.frame = { width, height, pixels: new Uint8Array(width * height) }
    this.viewportWidth = width
    this.viewportHeight = height
    this.logicalWidth = width
    this.logicalHeight = height
    this.windowWidth = width
    this.windowHeight = height
    const runtime = new U8g2({ width: 8, height: 8, layout: 'vertical' }) as U8g2Runtime
    runtime.setFont(firmwareFont(this.fontId))
    runtime.setFontPosBaseline()
    runtime.setFontMode(0)
    const setupDecode = runtime.fontSetupDecode.bind(runtime)
    runtime.fontSetupDecode = (glyphData) => {
      setupDecode(glyphData)
      runtime.fontDecode.fg_color = this.foreground
      runtime.fontDecode.bg_color = this.background
    }
    runtime.drawHVLine = (x, y, length, direction) => {
      this.drawFontRun(x, y, length, direction, runtime.drawColor as DevicePixelColor)
    }
    this.u8g2 = runtime
  }

  get width() { return this.logicalWidth }
  get height() { return this.logicalHeight }
  get rawWidth() { return this.frame.width }
  get rawHeight() { return this.frame.height }
  get currentRotation() { return this.rotation }
  get currentFontId() { return this.fontId }

  setRotation(rotation: DevicePixelRotation) {
    this.rotation = rotation
    if (rotation === 0 || rotation === 2) {
      this.logicalWidth = this.viewportWidth
      this.logicalHeight = this.viewportHeight
    } else {
      this.logicalWidth = this.viewportHeight
      this.logicalHeight = this.viewportWidth
    }
  }

  setRawViewport(x: number, y: number, width: number, height: number) {
    x = Math.trunc(x); y = Math.trunc(y); width = Math.trunc(width); height = Math.trunc(height)
    if (x < 0) { width += x; x = 0 }
    if (y < 0) { height += y; y = 0 }
    x = Math.min(x, this.rawWidth)
    y = Math.min(y, this.rawHeight)
    width = Math.max(0, Math.min(width, this.rawWidth - x))
    height = Math.max(0, Math.min(height, this.rawHeight - y))
    this.viewportX = x
    this.viewportY = y
    this.viewportWidth = width
    this.viewportHeight = height
    this.setRotation(this.rotation)
  }

  setMirrorX(mirror: boolean) { this.mirrorX = mirror }
  setMirrorY(mirror: boolean) { this.mirrorY = mirror }

  setWindow(x: number, y: number, width: number, height: number) {
    let px = Math.min(Math.max(0, Math.trunc(x)), this.logicalWidth)
    let py = Math.min(Math.max(0, Math.trunc(y)), this.logicalHeight)
    let pw = Math.min(Math.max(0, Math.trunc(width)), this.logicalWidth - px)
    let ph = Math.min(Math.max(0, Math.trunc(height)), this.logicalHeight - py)
    if (this.rotation === 1) {
      [px, py] = [py, px]; [pw, ph] = [ph, pw]
      px = this.rawWidth - px - pw
    } else if (this.rotation === 2) {
      px = this.rawWidth - px - pw
      py = this.rawHeight - py - ph
    } else if (this.rotation === 3) {
      [px, py] = [py, px]; [pw, ph] = [ph, pw]
      py = this.rawHeight - py - ph
    }
    pw += px % 8
    if (pw % 8 > 0) pw += 8 - (pw % 8)
    px -= px % 8
    this.windowX = px
    this.windowY = py
    this.windowWidth = pw
    this.windowHeight = ph
  }

  resetWindow() {
    this.windowX = 0
    this.windowY = 0
    this.windowWidth = this.rawWidth
    this.windowHeight = this.rawHeight
  }

  fillScreen(color: DevicePixelColor) {
    this.frame.pixels.fill(color)
  }

  drawPixel(x: number, y: number, color: DevicePixelColor) {
    x = Math.trunc(x)
    y = Math.trunc(y)
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) return
    if (this.rotation === 0) {
      x += this.viewportX; y += this.viewportY
    } else if (this.rotation === 1) {
      [x, y] = [y, x]
      x = this.viewportX + this.viewportWidth - x - 1
      y += this.viewportY
    } else if (this.rotation === 2) {
      x = this.viewportX + this.viewportWidth - x - 1
      y = this.viewportY + this.viewportHeight - y - 1
    } else {
      [x, y] = [y, x]
      x += this.viewportX
      y = this.viewportY + this.viewportHeight - y - 1
    }
    if (this.mirrorX) x = this.viewportX + this.viewportWidth - (x - this.viewportX) - 1
    if (this.mirrorY) y = this.viewportY + this.viewportHeight - (y - this.viewportY) - 1
    if (
      x < this.windowX || x >= this.windowX + this.windowWidth ||
      y < this.windowY || y >= this.windowY + this.windowHeight ||
      x < 0 || y < 0 || x >= this.rawWidth || y >= this.rawHeight
    ) return
    this.frame.pixels[y * this.rawWidth + x] = color
  }

  drawFastVLine(x: number, y: number, height: number, color: DevicePixelColor) {
    x = Math.trunc(x); y = Math.trunc(y); height = Math.trunc(height)
    if (height <= 0 || x < 0 || x >= this.width) return
    if (y < 0) { height += y; y = 0 }
    if (y + height > this.height) height = this.height - y
    if (height <= 0) return
    for (let row = y; row < y + height; row += 1) this.drawPixel(x, row, color)
  }

  drawFastHLine(x: number, y: number, width: number, color: DevicePixelColor) {
    x = Math.trunc(x); y = Math.trunc(y); width = Math.trunc(width)
    if (width <= 0 || y < 0 || y >= this.height) return
    if (x < 0) { width += x; x = 0 }
    if (x + width > this.width) width = this.width - x
    if (width <= 0) return
    for (let column = x; column < x + width; column += 1) this.drawPixel(column, y, color)
  }

  fillRect(x: number, y: number, width: number, height: number, color: DevicePixelColor) {
    x = Math.trunc(x); y = Math.trunc(y); width = Math.trunc(width); height = Math.trunc(height)
    for (let column = x; column < x + width; column += 1) this.drawFastVLine(column, y, height, color)
  }

  drawRect(x: number, y: number, width: number, height: number, color: DevicePixelColor) {
    this.drawFastHLine(x, y, width, color)
    this.drawFastHLine(x, y + height - 1, width, color)
    this.drawFastVLine(x, y, height, color)
    this.drawFastVLine(x + width - 1, y, height, color)
  }

  drawLine(x0: number, y0: number, x1: number, y1: number, color: DevicePixelColor) {
    x0 = Math.trunc(x0); y0 = Math.trunc(y0); x1 = Math.trunc(x1); y1 = Math.trunc(y1)
    const steep = Math.abs(y1 - y0) > Math.abs(x1 - x0)
    if (steep) { [x0, y0] = [y0, x0]; [x1, y1] = [y1, x1] }
    if (x0 > x1) { [x0, x1] = [x1, x0]; [y0, y1] = [y1, y0] }
    const dx = x1 - x0
    const dy = Math.abs(y1 - y0)
    let error = Math.trunc(dx / 2)
    const yStep = y0 < y1 ? 1 : -1
    for (; x0 <= x1; x0 += 1) {
      this.drawPixel(steep ? y0 : x0, steep ? x0 : y0, color)
      error -= dy
      if (error < 0) { y0 += yStep; error += dx }
    }
  }

  drawDottedLine(
    x0: number, y0: number, x1: number, y1: number,
    color: DevicePixelColor, dotLength: number, spaceLength: number,
  ) {
    let steep = Math.abs(y1 - y0) > Math.abs(x1 - x0)
    if (steep) { [x0, y0] = [y0, x0]; [x1, y1] = [y1, x1] }
    if (x0 > x1) { [x0, x1] = [x1, x0]; [y0, y1] = [y1, y0] }
    const dx = x1 - x0
    const dy = Math.abs(y1 - y0)
    let error = Math.trunc(dx / 2)
    const yStep = y0 < y1 ? 1 : -1
    let draw = true
    let length = 0
    for (; x0 <= x1; x0 += 1) {
      if (draw) {
        this.drawPixel(steep ? y0 : x0, steep ? x0 : y0, color)
        length += 1
        if (length >= dotLength) { length = 0; draw = false }
      } else {
        length += 1
        if (length >= spaceLength) { length = 0; draw = true }
      }
      error -= dy
      if (error < 0) { y0 += yStep; error += dx }
    }
  }

  drawCircle(x0: number, y0: number, radius: number, color: DevicePixelColor) {
    let f = 1 - radius
    let deltaX = 1
    let deltaY = -2 * radius
    let x = 0
    let y = radius
    this.drawPixel(x0, y0 + radius, color)
    this.drawPixel(x0, y0 - radius, color)
    this.drawPixel(x0 + radius, y0, color)
    this.drawPixel(x0 - radius, y0, color)
    while (x < y) {
      if (f >= 0) { y -= 1; deltaY += 2; f += deltaY }
      x += 1; deltaX += 2; f += deltaX
      this.drawPixel(x0 + x, y0 + y, color); this.drawPixel(x0 - x, y0 + y, color)
      this.drawPixel(x0 + x, y0 - y, color); this.drawPixel(x0 - x, y0 - y, color)
      this.drawPixel(x0 + y, y0 + x, color); this.drawPixel(x0 - y, y0 + x, color)
      this.drawPixel(x0 + y, y0 - x, color); this.drawPixel(x0 - y, y0 - x, color)
    }
  }

  drawCircleHelper(x0: number, y0: number, radius: number, corners: number, color: DevicePixelColor) {
    let f = 1 - radius
    let deltaX = 1
    let deltaY = -2 * radius
    let x = 0
    let y = radius
    while (x < y) {
      if (f >= 0) { y -= 1; deltaY += 2; f += deltaY }
      x += 1; deltaX += 2; f += deltaX
      if (corners & 4) { this.drawPixel(x0 + x, y0 + y, color); this.drawPixel(x0 + y, y0 + x, color) }
      if (corners & 2) { this.drawPixel(x0 + x, y0 - y, color); this.drawPixel(x0 + y, y0 - x, color) }
      if (corners & 8) { this.drawPixel(x0 - y, y0 + x, color); this.drawPixel(x0 - x, y0 + y, color) }
      if (corners & 1) { this.drawPixel(x0 - y, y0 - x, color); this.drawPixel(x0 - x, y0 - y, color) }
    }
  }

  fillCircle(x0: number, y0: number, radius: number, color: DevicePixelColor) {
    this.drawFastVLine(x0, y0 - radius, 2 * radius + 1, color)
    this.fillCircleHelper(x0, y0, radius, 3, 0, color)
  }

  fillCircleHelper(
    x0: number, y0: number, radius: number, corners: number, delta: number, color: DevicePixelColor,
  ) {
    let f = 1 - radius
    let deltaX = 1
    let deltaY = -2 * radius
    let x = 0
    let y = radius
    let previousX = x
    let previousY = y
    delta += 1
    while (x < y) {
      if (f >= 0) { y -= 1; deltaY += 2; f += deltaY }
      x += 1; deltaX += 2; f += deltaX
      if (x < y + 1) {
        if (corners & 1) this.drawFastVLine(x0 + x, y0 - y, 2 * y + delta, color)
        if (corners & 2) this.drawFastVLine(x0 - x, y0 - y, 2 * y + delta, color)
      }
      if (y !== previousY) {
        if (corners & 1) this.drawFastVLine(x0 + previousY, y0 - previousX, 2 * previousX + delta, color)
        if (corners & 2) this.drawFastVLine(x0 - previousY, y0 - previousX, 2 * previousX + delta, color)
        previousY = y
      }
      previousX = x
    }
  }

  drawRoundRect(x: number, y: number, width: number, height: number, radius: number, color: DevicePixelColor) {
    radius = Math.min(radius, Math.trunc(Math.min(width, height) / 2))
    this.drawFastHLine(x + radius, y, width - 2 * radius, color)
    this.drawFastHLine(x + radius, y + height - 1, width - 2 * radius, color)
    this.drawFastVLine(x, y + radius, height - 2 * radius, color)
    this.drawFastVLine(x + width - 1, y + radius, height - 2 * radius, color)
    this.drawCircleHelper(x + radius, y + radius, radius, 1, color)
    this.drawCircleHelper(x + width - radius - 1, y + radius, radius, 2, color)
    this.drawCircleHelper(x + width - radius - 1, y + height - radius - 1, radius, 4, color)
    this.drawCircleHelper(x + radius, y + height - radius - 1, radius, 8, color)
  }

  fillRoundRect(x: number, y: number, width: number, height: number, radius: number, color: DevicePixelColor) {
    radius = Math.min(radius, Math.trunc(Math.min(width, height) / 2))
    this.fillRect(x + radius, y, width - 2 * radius, height, color)
    this.fillCircleHelper(x + width - radius - 1, y + radius, radius, 1, height - 2 * radius - 1, color)
    this.fillCircleHelper(x + radius, y + radius, radius, 2, height - 2 * radius - 1, color)
  }

  drawEllipse(x0: number, y0: number, radiusX: number, radiusY: number, color: DevicePixelColor) {
    x0 = Math.trunc(x0); y0 = Math.trunc(y0); radiusX = Math.trunc(radiusX); radiusY = Math.trunc(radiusY)
    let x = 0
    let y = radiusY
    const radiusX2 = radiusX * radiusX
    const radiusY2 = radiusY * radiusY
    const twiceRadiusX2 = 2 * radiusX2
    const twiceRadiusY2 = 2 * radiusY2
    let decision = radiusY2 - radiusX2 * radiusY + Math.trunc(radiusX2 / 4)
    while (twiceRadiusY2 * x < twiceRadiusX2 * y) {
      this.drawPixel(x0 + x, y0 + y, color); this.drawPixel(x0 - x, y0 + y, color)
      this.drawPixel(x0 + x, y0 - y, color); this.drawPixel(x0 - x, y0 - y, color)
      x += 1
      if (decision < 0) decision += radiusY2 + twiceRadiusY2 * x
      else { decision += radiusY2 + twiceRadiusY2 * x - twiceRadiusX2 * y; y -= 1 }
    }
    decision = Math.trunc(radiusY2 * (2 * x + 1) * (2 * x + 1) / 4) + radiusX2 * (y - 1) * (y - 1) - radiusX2 * radiusY2
    while (y >= 0) {
      this.drawPixel(x0 + x, y0 + y, color); this.drawPixel(x0 - x, y0 + y, color)
      this.drawPixel(x0 + x, y0 - y, color); this.drawPixel(x0 - x, y0 - y, color)
      y -= 1
      if (decision > 0) decision += radiusX2 - twiceRadiusX2 * y
      else { decision += radiusX2 + twiceRadiusY2 * x - twiceRadiusX2 * y; x += 1 }
    }
  }

  fillEllipse(x0: number, y0: number, radiusX: number, radiusY: number, color: DevicePixelColor) {
    x0 = Math.trunc(x0); y0 = Math.trunc(y0); radiusX = Math.trunc(radiusX); radiusY = Math.trunc(radiusY)
    let x = 0
    let y = radiusY
    const radiusX2 = radiusX * radiusX
    const radiusY2 = radiusY * radiusY
    const twiceRadiusX2 = 2 * radiusX2
    const twiceRadiusY2 = 2 * radiusY2
    let decision = radiusY2 - radiusX2 * radiusY + Math.trunc(radiusX2 / 4)
    while (twiceRadiusY2 * x < twiceRadiusX2 * y) {
      x += 1
      if (decision < 0) decision += radiusY2 + twiceRadiusY2 * x
      else {
        decision += radiusY2 + twiceRadiusY2 * x - twiceRadiusX2 * y
        this.drawFastHLine(x0 - (x - 1), y0 + y, 2 * (x - 1) + 1, color)
        this.drawFastHLine(x0 - (x - 1), y0 - y, 2 * (x - 1) + 1, color)
        y -= 1
      }
    }
    decision = Math.trunc(radiusY2 * (2 * x + 1) * (2 * x + 1) / 4) + radiusX2 * (y - 1) * (y - 1) - radiusX2 * radiusY2
    while (y >= 0) {
      this.drawFastHLine(x0 - x, y0 + y, 2 * x + 1, color)
      this.drawFastHLine(x0 - x, y0 - y, 2 * x + 1, color)
      y -= 1
      if (decision > 0) decision += radiusX2 - twiceRadiusX2 * y
      else { decision += radiusX2 + twiceRadiusY2 * x - twiceRadiusX2 * y; x += 1 }
    }
  }

  drawTriangle(
    x0: number, y0: number, x1: number, y1: number, x2: number, y2: number,
    color: DevicePixelColor,
  ) {
    this.drawLine(x0, y0, x1, y1, color)
    this.drawLine(x1, y1, x2, y2, color)
    this.drawLine(x2, y2, x0, y0, color)
  }

  fillTriangle(
    x0: number, y0: number, x1: number, y1: number, x2: number, y2: number,
    color: DevicePixelColor,
  ) {
    x0 = Math.trunc(x0); y0 = Math.trunc(y0); x1 = Math.trunc(x1); y1 = Math.trunc(y1); x2 = Math.trunc(x2); y2 = Math.trunc(y2)
    if (y0 > y1) { [y0, y1] = [y1, y0]; [x0, x1] = [x1, x0] }
    if (y1 > y2) { [y1, y2] = [y2, y1]; [x1, x2] = [x2, x1] }
    if (y0 > y1) { [y0, y1] = [y1, y0]; [x0, x1] = [x1, x0] }
    if (y0 === y2) {
      const minimum = Math.min(x0, x1, x2)
      const maximum = Math.max(x0, x1, x2)
      this.drawFastHLine(minimum, y0, maximum - minimum + 1, color)
      return
    }
    const dx01 = x1 - x0; const dy01 = y1 - y0
    const dx02 = x2 - x0; const dy02 = y2 - y0
    const dx12 = x2 - x1; const dy12 = y2 - y1
    let sumA = 0
    let sumB = 0
    const last = y1 === y2 ? y1 : y1 - 1
    let y = y0
    for (; y <= last; y += 1) {
      let a = x0 + Math.trunc(sumA / dy01)
      let b = x0 + Math.trunc(sumB / dy02)
      sumA += dx01; sumB += dx02
      if (a > b) [a, b] = [b, a]
      this.drawFastHLine(a, y, b - a + 1, color)
    }
    sumA = dx12 * (y - y1)
    sumB = dx02 * (y - y0)
    for (; y <= y2; y += 1) {
      let a = x1 + Math.trunc(sumA / dy12)
      let b = x0 + Math.trunc(sumB / dy02)
      sumA += dx12; sumB += dx02
      if (a > b) [a, b] = [b, a]
      this.drawFastHLine(a, y, b - a + 1, color)
    }
  }

  drawBitmap(
    x: number, y: number, bitmap: ArrayLike<number>, width: number, height: number,
    color: DevicePixelColor, invert = false,
  ) {
    x = Math.trunc(x); y = Math.trunc(y); width = Math.trunc(width); height = Math.trunc(height)
    const byteWidth = Math.trunc((width + 7) / 8)
    for (let row = 0; row < height; row += 1) {
      let byte = 0
      for (let column = 0; column < width; column += 1) {
        if ((column & 7) !== 0) byte = (byte << 1) & 0xff
        else byte = bitmap[row * byteWidth + Math.trunc(column / 8)] ?? 0
        if (((byte & 0x80) === 0x80) !== invert) this.drawPixel(x + column, y + row, color)
      }
    }
  }

  setCursor(x: number, y: number) { this.tx = Math.trunc(x); this.ty = Math.trunc(y) }

  setFont(id: DeviceGuiFontId) {
    // The C engine preserves transparency when selecting the same font again.
    if (id === this.fontId) return
    this.fontId = id
    this.u8g2.setFont(firmwareFont(id))
    // u8g2_SetFont() in the firmware resets glyph drawing to opaque mode
    // whenever the font pointer changes.
    this.u8g2.setFontMode(0)
  }

  setFontScale(scaleQ8: number) { this.fontScaleQ8 = scaleQ8 || 256 }
  setFontMode(transparent: boolean | number) { this.u8g2.setFontMode(transparent ? 1 : 0) }
  setFontDirection(direction: DevicePixelRotation) { this.u8g2.setFontDirection(direction) }

  setTextColor(foreground: DevicePixelColor, background: DevicePixelColor) {
    this.foreground = foreground
    this.background = background
    // The bundled JS decoder follows u8g2's public draw-color API and copies
    // drawColor into the glyph decoder at the start of every glyph. Keep it
    // synchronized with Adafruit_GFX's explicit text foreground.
    this.u8g2.drawColor = foreground
    this.u8g2.fontDecode.fg_color = foreground
    this.u8g2.fontDecode.bg_color = background
  }

  getFontAscent() { return Math.trunc(this.u8g2.fontInfo.ascent_A * this.fontScaleQ8 / 256) }
  getFontDescent() { return Math.trunc(this.u8g2.fontInfo.descent_g * this.fontScaleQ8 / 256) }
  getFontHeight() { return Math.trunc((this.u8g2.fontInfo.ascent_A - this.u8g2.fontInfo.descent_g) * this.fontScaleQ8 / 256) }
  supportsText(text: string) { return Array.from(text).every((character) => this.u8g2.getGlyphWidth(character.codePointAt(0) ?? 0) > 0) }
  getUTF8Width(text: string) {
    if (this.fontRasterMode !== 'legacy') {
      return Array.from(text).reduce((sum, character) => sum + Math.trunc(
        this.u8g2.getGlyphWidth(character.codePointAt(0) ?? 0) * this.fontScaleQ8 / 256,
      ), 0)
    }
    return Math.trunc(this.u8g2.getUTF8Width(text) * this.fontScaleQ8 / 256)
  }

  drawGlyph(x: number, y: number, encoding: number) {
    const width = this.u8g2.getGlyphWidth(encoding)
    this.fontOriginX = Math.trunc(x)
    this.fontOriginY = Math.trunc(y)
    this.u8g2.drawGlyph(this.fontOriginX, this.fontOriginY, encoding)
    return Math.trunc(width * this.fontScaleQ8 / 256)
  }

  drawStr(x: number, y: number, text: string) {
    let sum = 0
    for (const character of text) {
      const encoding = character.codePointAt(0) ?? 0
      const delta = this.drawGlyph(x, y, encoding)
      if (this.u8g2.fontDecode.dir === 0) x += delta
      else if (this.u8g2.fontDecode.dir === 1) y += delta
      else if (this.u8g2.fontDecode.dir === 2) x -= delta
      else y -= delta
      sum += delta
    }
    return sum
  }

  drawUTF8(x: number, y: number, text: string) { return this.drawStr(x, y, text) }

  print(text: string) {
    for (const character of text) {
      if (character === '\n') {
        this.tx = 0
        this.ty += Math.trunc(
          (this.u8g2.fontInfo.ascent_para - this.u8g2.fontInfo.descent_para) * this.fontScaleQ8 / 256,
        )
        continue
      }
      if (character === '\r') { this.tx = 0; continue }
      const delta = this.drawGlyph(this.tx, this.ty, character.codePointAt(0) ?? 0)
      if (this.u8g2.fontDecode.dir === 0) this.tx += delta
      else if (this.u8g2.fontDecode.dir === 1) this.ty += delta
      else if (this.u8g2.fontDecode.dir === 2) this.tx -= delta
      else this.ty -= delta
    }
  }

  private drawFontRun(x: number, y: number, length: number, direction: number, color: DevicePixelColor) {
    // u8g2_font_decode_len() in the C renderer suppresses zero-length runs.
    // The JS decoder forwards them; if scaled, floor/ceil expansion would turn
    // such an empty run into real pixels at the glyph's trailing edge.
    if (length <= 0) return
    const scale = this.fontScaleQ8 || 256
    if (scale !== 256) {
      let x0 = x; let y0 = y; let x1 = x + 1; let y1 = y + 1
      if (direction === 0) x1 = x + length
      else if (direction === 1) y1 = y + length
      else if (direction === 2) x0 = x - length + 1
      else y0 = y - length + 1
      if (this.fontRasterMode === 'strokes') {
        const round = (value:number) => Math.floor((value * scale + 128) / 256)
        const left=this.fontOriginX+round(x0-this.fontOriginX), top=this.fontOriginY+round(y0-this.fontOriginY)
        this.fillRect(left,top,Math.max(1,this.fontOriginX+round(x1-this.fontOriginX)-left),
          Math.max(1,this.fontOriginY+round(y1-this.fontOriginY)-top),color)
        return
      }
      const lowerX = this.fontRasterMode === 'nearest' || this.fontRasterMode === 'vertical-coverage' ? scaleCeil : scaleFloor
      const lowerY = this.fontRasterMode === 'nearest' ? scaleCeil : scaleFloor
      x0 = this.fontOriginX + lowerX(x0 - this.fontOriginX, scale)
      y0 = this.fontOriginY + lowerY(y0 - this.fontOriginY, scale)
      x1 = this.fontOriginX + scaleCeil(x1 - this.fontOriginX, scale)
      y1 = this.fontOriginY + scaleCeil(y1 - this.fontOriginY, scale)
      this.fillRect(x0, y0, x1 - x0, y1 - y0, color)
      return
    }
    if (direction === 0) this.drawFastHLine(x, y, length, color)
    else if (direction === 1) this.drawFastVLine(x, y, length, color)
    else if (direction === 2) this.drawFastHLine(x - length + 1, y, length, color)
    else this.drawFastVLine(x, y - length + 1, length, color)
  }
}

export function paintDevicePixelFrame(
  context: CanvasRenderingContext2D,
  frame: DevicePixelFrame,
  x = 0,
  y = 0,
) {
  const image = context.createImageData(frame.width, frame.height)
  const palette = [
    [255, 255, 255],
    [23, 26, 24],
    [200, 45, 40],
    [226, 180, 24],
    [42, 91, 168],
    [56, 133, 76],
    [230, 119, 35],
  ] as const
  for (let index = 0; index < frame.pixels.length; index += 1) {
    const [red, green, blue] = palette[frame.pixels[index] ?? 0]
    const offset = index * 4
    image.data[offset] = red
    image.data[offset + 1] = green
    image.data[offset + 2] = blue
    image.data[offset + 3] = 255
  }
  context.putImageData(image, x, y)
}
