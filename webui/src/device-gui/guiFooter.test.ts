import { describe, expect, it } from 'vitest'
import { DevicePixelGfx, DEVICE_BLACK, DEVICE_RED, DEVICE_WHITE } from './devicePixelGfx'
import { drawInkTagEditionBadge } from './deviceGuiComponents'
import { editionBadgeGeometry } from './editionBadgeBitmap'
import { renderFirmwareNativePage } from './nativePageRenderer'
import { drawNativeScenePart, type NativeScene, type NativeTextCommand } from './nativeScene'

it('preserves transparent text when the current font is selected again, as C does', () => {
  const render = (reselect: boolean) => {
    const gfx = new DevicePixelGfx(20, 20)
    gfx.fillRect(0, 0, 20, 20, DEVICE_BLACK)
    gfx.setFontMode(1)
    if (reselect) gfx.setFont('wqy9-lunar')
    gfx.setCursor(3, 14); gfx.setTextColor(DEVICE_BLACK, DEVICE_WHITE); gfx.print('班')
    return gfx.frame.pixels
  }
  expect(render(true)).toEqual(render(false))
  expect(render(true).every(pixel => pixel === DEVICE_BLACK)).toBe(true)
})

it('locks WQY9 cap / bracket ink bounds used to center footer labels', () => {
  for (const scale of [256, 288, 320, 384]) {
    for (const text of ['DEVICEINFO', '[GR_1234]', '[OM_abcdef]', '星期五 八月十五']) {
      const gfx = new DevicePixelGfx(300, 50)
      gfx.fontRasterMode = 'nearest'; gfx.setFont('wqy9-lunar'); gfx.setFontScale(scale); gfx.setFontMode(1)
      gfx.setTextColor(DEVICE_BLACK, DEVICE_WHITE); gfx.setCursor(0, 25); gfx.print(text)
      const rows = [...new Set([...gfx.frame.pixels.entries()].filter(([, p]) => p).map(([i]) => Math.floor(i / 300) - 25))]
      expect(Math.min(...rows)).toBe(-Math.floor((text === 'DEVICEINFO' ? 8 : 10) * scale / 256))
      expect(Math.max(...rows)).toBe(text === 'DEVICEINFO' ? -1 : Math.ceil(scale / 256) - 1)
    }
  }
})

it('leaves a clear column between all badge glyphs at native and doubled sizes', () => {
  for (const scale of [1, 2]) for (const edition of ['base', 'pro'] as const) {
    const gfx = new DevicePixelGfx(132, 30)
    const badge = drawInkTagEditionBadge(gfx, { x: 0, y: 0, width: 66 * scale, height: 11 * scale }, { edition, colorCount: 2 })
    expect(badge.width).toBe(edition === 'base' ? 32 * scale + 7 : 49 * scale + 10)
    let x = badge.x + scale + 1
    for (const character of edition === 'base' ? 'INKTAG' : 'INKTAG PRO') {
      for (let y = badge.y + scale + 1; y < badge.y + scale + 1 + 7 * scale; y++) {
        if (character !== ' ') expect(gfx.frame.pixels[y * 132 + x + 5 * scale]).toBe(0)
      }
      x += character === ' ' ? 2 * scale : 5 * scale + 1
    }
  }
})

it('uses the exact calendar-footer badge geometry for image previews', () => {
  expect(editionBadgeGeometry(152, 152, 'base')).toMatchObject({x: 110, y: 138, width: 39, height: 11})
  expect(editionBadgeGeometry(200, 200, 'base')).toMatchObject({x: 158, y: 183, width: 39, height: 11})
  expect(editionBadgeGeometry(212, 104, 'base')).toMatchObject({x: 170, y: 90, width: 39, height: 11})
  expect(editionBadgeGeometry(400, 300, 'base')).toMatchObject({x: 351, y: 284, width: 39, height: 11})
  expect(editionBadgeGeometry(600, 448, 'base')).toMatchObject({x: 514, y: 423, width: 71, height: 20})
  expect(editionBadgeGeometry(400, 300, 'pro')).toMatchObject({x: 331, y: 284, width: 59, height: 11})
})

it('keeps final image overlays on the same pixels as square calendar and clock badges', () => {
  for(const side of [152,200,240]) for(const edition of ['base','pro'] as const) for(const page of ['calendar','clock'] as const) {
    const scene:NativeScene={parts:[]}
    renderFirmwareNativePage({width:side,height:side,page,edition,scene})
    const part=scene.parts.find(part=>part.element.key.startsWith('edition:'))!
    const gfx=new DevicePixelGfx(side,side)
    drawNativeScenePart(gfx,part,part.element.source,part.element)
    const points=[...gfx.frame.pixels.entries()].filter(([,pixel])=>pixel!==0).map(([offset])=>({x:offset%side,y:Math.floor(offset/side)}))
    const x=Math.min(...points.map(p=>p.x)),y=Math.min(...points.map(p=>p.y))
    const width=Math.max(...points.map(p=>p.x))-x+1,height=Math.max(...points.map(p=>p.y))-y+1
    expect(editionBadgeGeometry(side,side,edition)).toMatchObject({x,y,width,height})
  }
})

it('allows only Pro to hide the badge on every native page', () => {
  for (const page of ['calendar', 'clock', 'device-info'] as const) {
    const render = (activated: boolean, brandHidden: boolean) => {
      const scene: NativeScene = { parts: [] }
      renderFirmwareNativePage({width: 400, height: 300, page, layoutRevision: 'v1',
        edition: activated ? 'pro' : 'base', activated, brandHidden, scene,
        timestamp: Date.UTC(2026, 9, 6, 12) / 1000})
      return scene.parts.filter(part => part.element.key.startsWith('edition:'))
    }
    expect(render(false, true)).toEqual(render(false, false))
    expect(render(false, true)).toHaveLength(1)
    expect(render(true, false)).toHaveLength(1)
    expect(render(true, true)).toHaveLength(0)
  }
})

it('renders base red only when the screen has red and keeps Pro black', () => {
  const colors = (edition: 'base' | 'pro', color: number) => {
    const gfx = new DevicePixelGfx(82, 16)
    drawInkTagEditionBadge(gfx, {x: 0, y: 0, width: 82, height: 16}, {edition, colorCount: color})
    return new Set(gfx.frame.pixels)
  }
  expect(colors('base', 2)).toContain(DEVICE_RED)
  expect(colors('base', 4)).not.toContain(DEVICE_RED)
  expect(colors('base', 4)).toContain(DEVICE_BLACK)
  expect(colors('pro', 2)).not.toContain(DEVICE_RED)
  expect(colors('pro', 2)).toContain(DEVICE_BLACK)
})

it('scales the DEVICE INFO word gap on large screens without spreading its letters', () => {
  for (const [width, height, nativeUiScale, gap] of [[152, 152, 1, 2], [400, 300, 1, 2],
    [600, 448, 4, 4], [752, 568, 2, 5], [800, 480, 2, 5]] as const) {
    const scene: NativeScene = { parts: [] }
    renderFirmwareNativePage({ width, height, nativeUiScale, page: 'device-info', layoutRevision: 'v1', scene })
    const runs = scene.parts.find(part => part.element.key.startsWith('device-caption:'))!.commands
      .filter((command): command is NativeTextCommand => command.type === 'text')
    expect(runs.map(run => run.text)).toEqual(['DEVICE', 'INFO'])
    const gfx = new DevicePixelGfx(width, height)
    gfx.fontRasterMode = 'nearest'
    gfx.setFont(runs[0]!.font); gfx.setFontScale(runs[0]!.scale)
    expect(runs[1]!.x - runs[0]!.x - gfx.getUTF8Width('DEVICE')).toBe(gap)
    expect(runs[1]!.y).toBe(runs[0]!.y)
  }
})

describe('footer ink is centered, with a two-pixel minimum safe margin', () => {
  const screens = [[152, 152, 1], [200, 200, 1], [212, 104, 1], [250, 128, 1], [296, 152, 1],
    [400, 300, 1], [600, 448, 4], [752, 568, 2], [800, 480, 2]] as const
  for (const [width, height, nativeUiScale] of screens) for (const page of ['calendar', 'clock', 'device-info'] as const) {
    it(`${width}x${height} ${page}: edition and left caption share the status-bar center`, () => {
      const scene: NativeScene = { parts: [] }
      renderFirmwareNativePage({ width, height, nativeUiScale, page, layoutRevision: 'v1', scene,
        timestamp: Date.UTC(2026, 8, 20, 6, 45, 12) / 1000 })
      const footer = width === 200 && height === 200 && page !== 'device-info'
        ? 23 : width < 300 ? 18 : width > 500 ? 31 : 21
      const top = height - footer + 1
      const parts = scene.parts.filter(({ element, commands }) => element.key.startsWith('edition:')
        || element.key.startsWith('device-caption:') || commands.some(c => c.type === 'text' && /^\[/.test(c.text) && c.y > top))
      expect(parts).toHaveLength(2)
      for (const part of parts) {
        const gfx = new DevicePixelGfx(width, height)
        gfx.fontRasterMode = 'nearest'
        drawNativeScenePart(gfx, part, part.element.source, part.element)
        const rows = [...new Set([...gfx.frame.pixels.entries()].filter(([, p]) => p).map(([i]) => Math.floor(i / width)))]
        const above = Math.min(...rows) - top, below = height - 1 - Math.max(...rows)
        expect(Math.min(above, below), part.element.key).toBeGreaterThanOrEqual(2)
        expect(Math.abs(above - below), `${part.element.key}: ${above}/${below}`).toBeLessThanOrEqual(1)
      }
    })
  }
})
