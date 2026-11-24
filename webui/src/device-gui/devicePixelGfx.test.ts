import { describe, expect, it } from 'vitest'
import golden from './devicePixelGfxGolden.generated.json'
import {
  DEVICE_BLACK,
  DEVICE_RED,
  DEVICE_WHITE,
  DevicePixelGfx,
  type DevicePixelRotation,
} from './devicePixelGfx'

const fixtureBitmap = Uint8Array.from([
  0x81, 0x00, 0x42, 0x00, 0x24, 0x00, 0x18, 0x00,
  0x18, 0x00, 0x24, 0x00, 0x42, 0x00, 0x81, 0x00,
])

function drawOrientationFixture(gfx: DevicePixelGfx) {
  gfx.drawRect(2, 2, gfx.width - 4, gfx.height - 4, DEVICE_BLACK)
  gfx.fillRect(5, 6, 13, 9, DEVICE_RED)
  gfx.drawLine(0, 0, gfx.width - 1, gfx.height - 1, DEVICE_BLACK)
  gfx.drawCircle(gfx.width - 13, 13, 7, DEVICE_RED)
  gfx.drawBitmap(24, gfx.height - 13, fixtureBitmap, 9, 8, DEVICE_BLACK)
}

function drawFixture(scenario: string) {
  const gfx = new DevicePixelGfx(96, 72)
  gfx.fillScreen(DEVICE_WHITE)
  if (scenario === 'primitives') {
    gfx.drawPixel(1, 1, DEVICE_BLACK)
    gfx.drawLine(-4, 8, 34, 21, DEVICE_BLACK)
    gfx.drawDottedLine(2, 24, 48, 3, DEVICE_RED, 3, 2)
    gfx.drawFastHLine(-3, 27, 28, DEVICE_BLACK)
    gfx.drawFastVLine(28, -4, 34, DEVICE_RED)
    gfx.drawRect(34, 3, 20, 14, DEVICE_BLACK)
    gfx.fillRect(58, 4, 13, 11, DEVICE_RED)
    gfx.drawCircle(13, 43, 10, DEVICE_BLACK)
    gfx.drawCircleHelper(37, 42, 10, 1 | 4, DEVICE_RED)
    gfx.fillCircle(58, 40, 9, DEVICE_BLACK)
    gfx.fillCircleHelper(79, 39, 9, 3, 2, DEVICE_RED)
    gfx.drawEllipse(16, 61, 13, 6, DEVICE_RED)
    gfx.fillEllipse(43, 61, 10, 6, DEVICE_BLACK)
    gfx.drawTriangle(57, 52, 72, 68, 83, 50, DEVICE_RED)
    gfx.fillTriangle(75, 69, 86, 54, 94, 68, DEVICE_BLACK)
    gfx.drawRoundRect(73, 2, 20, 17, 5, DEVICE_BLACK)
    gfx.fillRoundRect(4, 52, 20, 17, 4, DEVICE_RED)
    gfx.drawBitmap(31, 52, fixtureBitmap, 9, 8, DEVICE_RED)
    gfx.drawBitmap(50, 53, fixtureBitmap, 9, 8, DEVICE_BLACK, true)
    return gfx.frame
  }
  if (scenario.startsWith('rotation-')) gfx.setRotation(Number(scenario.slice(9)) / 90 as DevicePixelRotation)
  else if (scenario === 'mirror-x') gfx.setMirrorX(true)
  else if (scenario === 'mirror-y') gfx.setMirrorY(true)
  else if (scenario === 'mirror-xy') { gfx.setMirrorX(true); gfx.setMirrorY(true) }
  else if (scenario === 'viewport') {
    gfx.setRawViewport(11, 7, 61, 45)
    gfx.setRotation(1)
  } else if (scenario === 'window') {
    gfx.setWindow(11, 9, 27, 23)
    gfx.drawRect(0, 0, gfx.width, gfx.height, DEVICE_BLACK)
    gfx.fillRect(7, 5, 41, 37, DEVICE_RED)
    gfx.drawLine(0, gfx.height - 1, gfx.width - 1, 0, DEVICE_BLACK)
    return gfx.frame
  } else if (scenario === 'text') {
    gfx.setFont('wqy9-lunar')
    gfx.setTextColor(DEVICE_BLACK, DEVICE_WHITE)
    gfx.setCursor(3, 18)
    gfx.print('InkTag 0.2')
    gfx.setFontScale(384)
    gfx.setTextColor(DEVICE_RED, DEVICE_WHITE)
    gfx.drawUTF8(4, 39, '九月7日')
    gfx.setFontScale(256)
    gfx.setFontDirection(1)
    gfx.drawStr(82, 8, 'ABC')
    return gfx.frame
  } else throw new Error(`unknown GFX fixture: ${scenario}`)
  drawOrientationFixture(gfx)
  return gfx.frame
}

function decodeBase64(value: string) {
  return Uint8Array.from(atob(value), (character) => character.charCodeAt(0))
}

function mismatchMessage(actual: Uint8Array, expected: Uint8Array, width: number) {
  let count = 0
  let minX = width
  let minY = Math.trunc(expected.length / width)
  let maxX = -1
  let maxY = -1
  for (let index = 0; index < expected.length; index += 1) {
    if (actual[index] === expected[index]) continue
    count += 1
    const x = index % width
    const y = Math.trunc(index / width)
    minX = Math.min(minX, x); minY = Math.min(minY, y)
    maxX = Math.max(maxX, x); maxY = Math.max(maxY, y)
  }
  return `${count} differing pixels in (${minX},${minY})-(${maxX},${maxY})`
}

describe('DevicePixelGfx C parity', () => {
  for (const vector of golden.vectors) {
    it(`${vector.scenario} matches Adafruit_GFX.c`, () => {
      const frame = drawFixture(vector.scenario)
      const expected = decodeBase64(vector.pixelsBase64)
      expect(frame.width).toBe(vector.width)
      expect(frame.height).toBe(vector.height)
      if (frame.pixels.some((pixel, index) => pixel !== expected[index])) {
        throw new Error(mismatchMessage(frame.pixels, expected, frame.width))
      }
      expect(frame.pixels).toEqual(expected)
    })
  }
})
