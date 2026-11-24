import { U8g2, U8g2Font } from '../vendor/u8g2-js/index.js'
import { deviceFontData } from './fonts.generated'
import type { DeviceGuiAlign, DeviceGuiFontId, DeviceGuiFrame } from './model'

export interface DeviceTextCommand {
  text: string
  x: number
  y: number
  width?: number
  align?: DeviceGuiAlign
}

const fonts = new Map<DeviceGuiFontId, U8g2Font>()
const coverageReaders = new Map<DeviceGuiFontId, U8g2>()

function firmwareFont(id: DeviceGuiFontId) {
  let font = fonts.get(id)
  if (!font) {
    font = U8g2Font.fromBase64(deviceFontData[id].base64)
    fonts.set(id, font)
  }
  return font
}

/** Text controls and coverage checks use the same whitespace convention. */
export function normalizeDeviceText(text: string) {
  return text.replace(/\r\n?/g, '\n').replace(/\t/g, '    ')
}

/** Read the actual generated firmware subset, not a Unicode range or a web font. */
export function missingDeviceGlyphs(fontId: DeviceGuiFontId, text: string): string[] {
  let reader = coverageReaders.get(fontId)
  if (!reader) {
    reader = new U8g2({ width: 8, height: 8, layout: 'vertical' })
    reader.setFont(firmwareFont(fontId))
    coverageReaders.set(fontId, reader)
  }
  return [...new Set(Array.from(normalizeDeviceText(text)))].filter((character) =>
    character !== '\n' && reader.getGlyphWidth(character.codePointAt(0)!) <= 0,
  )
}

export function drawDeviceTextMask(
  context: CanvasRenderingContext2D,
  frame: DeviceGuiFrame,
  fontId: DeviceGuiFontId,
  commands: readonly DeviceTextCommand[],
  color: string,
  fontScaleQ8 = 256,
) {
  if (frame.width < 1 || frame.height < 1 || commands.length === 0) return
  const scale = Math.max(128, Math.min(768, Math.round(fontScaleQ8)))
  const sourceWidth = Math.max(1, Math.ceil(frame.width * 256 / scale))
  const sourceHeight = Math.max(1, Math.ceil(frame.height * 256 / scale))
  const renderer = new U8g2({ width: sourceWidth, height: sourceHeight, layout: 'vertical' })
  renderer.setFont(firmwareFont(fontId))
  renderer.setFontMode(1)
  renderer.setFontPosTop()
  renderer.clearBuffer()
  for (const command of commands) {
    const textWidth = renderer.getUTF8Width(command.text)
    const commandX = Math.round(command.x * 256 / scale)
    const commandY = Math.round(command.y * 256 / scale)
    const available = Math.round((command.width ?? frame.width - command.x) * 256 / scale)
    const x = command.align === 'center'
      ? commandX + Math.round((available - textWidth) / 2)
      : command.align === 'right' ? commandX + available - textWidth : commandX
    renderer.drawUTF8(Math.round(x), commandY, command.text)
  }
  context.fillStyle = color
  const memory = renderer.displayMemory
  const tiles = renderer.displayInfo.tileWidth
  for (let y = 0; y < frame.height; y += 1) {
    const sourceY = Math.min(sourceHeight - 1, Math.floor(y * 256 / scale))
    const row = (sourceY & ~7) * tiles
    for (let x = 0; x < frame.width; x += 1) {
      const sourceX = Math.min(sourceWidth - 1, Math.floor(x * 256 / scale))
      if ((memory[row + sourceX] >> (sourceY & 7)) & 1) context.fillRect(frame.x + x, frame.y + y, 1, 1)
    }
  }
}

export function deviceFontBytes(id: DeviceGuiFontId) {
  return deviceFontData[id].bytes
}
