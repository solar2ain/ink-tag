import { drawInkTagEditionBadge } from './deviceGuiComponents'
import { DevicePixelGfx, paintDevicePixelFrame } from './devicePixelGfx'

export interface EditionBadgeGeometry {
  x: number
  y: number
  width: number
  height: number
  areaWidth: number
  areaHeight: number
}

function compactWordWidth(text: string, scale: number) {
  return Array.from(text).reduce((width, character) => width +
    (character === ' ' ? 2 * scale : 5 * scale + 1), 0) - 1
}

/** Canonical bottom-right V1 calendar-footer geometry shared by image and
 * editor previews. The firmware's final-output overlay uses the same rules. */
export function editionBadgeGeometry(
  logicalWidth: number,
  logicalHeight: number,
  edition: 'base' | 'pro',
): EditionBadgeGeometry {
  const compact = logicalWidth < 300 && logicalHeight < 300
  const large = logicalWidth > 500
  const scale = large ? 2 : 1
  const areaWidth = compact ? 66 : large ? 122 : 82
  const footerHeight = logicalWidth===200 && logicalHeight===200 ? 23 : compact ? 18 : large ? 31 : 21
  const areaHeight = footerHeight - 5
  const margin = compact ? 3 : Math.max(10, Math.min(20,
    Math.trunc((Math.min(logicalWidth * 3, logicalHeight * 4) + 60) / 120)))
  const label = edition === 'base' ? 'INKTAG' : 'INKTAG PRO'
  const padding = scale + 1
  const width = compactWordWidth(label, scale) + padding * 2
  const height = 7 * scale + padding * 2
  const areaX = logicalWidth - margin - areaWidth
  const areaY = logicalHeight - footerHeight + 3
  return {
    x: areaX + areaWidth - width,
    y: areaY + Math.trunc((areaHeight - height) / 2),
    width, height, areaWidth, areaHeight,
  }
}

/** Project the device-enforced badge back into a photo-oriented editor canvas.
 * The sender turns an opposite-orientation canvas clockwise, so preview uses
 * the inverse turn. Do not invent a second upright badge at another position. */
export function editionBadgePlacement(canvasWidth:number,canvasHeight:number,screenWidth:number,screenHeight:number,edition:'base'|'pro',readingOrientation=false) {
  if (readingOrientation) return {...editionBadgeGeometry(canvasWidth,canvasHeight,edition),rotation:0}
  const badge=editionBadgeGeometry(screenWidth,screenHeight,edition)
  const turned=canvasWidth!==canvasHeight && canvasWidth===screenHeight && canvasHeight===screenWidth
  return {...badge,x:turned?badge.y:badge.x,y:turned?screenWidth-badge.x:badge.y,rotation:turned?-90:0}
}

/** Build the same integer-pixel badge used by the device renderer. */
export function createEditionBadgeFrame(
  edition: 'base' | 'pro',
  colorCount: number,
  logicalWidth: number,
  logicalHeight: number,
) {
  const geometry = editionBadgeGeometry(logicalWidth, logicalHeight, edition)
  const gfx = new DevicePixelGfx(geometry.areaWidth, geometry.areaHeight)
  const bounds = drawInkTagEditionBadge(
    gfx,
    { x: 0, y: 0, width: geometry.areaWidth, height: geometry.areaHeight },
    { edition, colorCount, align: 'right' },
  )
  const pixels = new Uint8Array(bounds.width * bounds.height)
  for (let y = 0; y < bounds.height; y++) {
    pixels.set(
      gfx.frame.pixels.subarray(
        (y + bounds.y) * geometry.areaWidth + bounds.x,
        (y + bounds.y) * geometry.areaWidth + bounds.x + bounds.width,
      ),
      y * bounds.width,
    )
  }
  return { width: bounds.width, height: bounds.height, pixels }
}

export function createEditionBadgeBitmap(edition: 'base' | 'pro', colorCount: number, logicalWidth: number, logicalHeight: number) {
  const frame = createEditionBadgeFrame(edition,colorCount,logicalWidth,logicalHeight)
  const bitmap = document.createElement('canvas')
  bitmap.width=frame.width; bitmap.height=frame.height
  paintDevicePixelFrame(bitmap.getContext('2d')!, frame)
  return bitmap
}
