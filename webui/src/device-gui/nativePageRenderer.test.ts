import { readFileSync } from 'node:fs'
import { DEVICE_BATTERY_RED_MAX_MV, drawDevicePowerStatus } from './deviceGuiComponents'
import { describe, expect, it } from 'vitest'
import golden from './nativePageGolden.generated.json'
import v1CandidateGolden from './layoutV1CandidateGolden.generated.json'
import deviceQrGolden from './deviceInfoQrGolden.generated.json'
import { firmwareSolarToLunar, firmwareTransformTime } from './lunarFirmware'
import { DevicePixelGfx } from './devicePixelGfx'
import { calendarTextRole, type NativeScene } from './nativeScene'
import { INKTAG_FIRMWARE_VERSION } from '../generated/version'
import {
  currentNativeLayoutRevision,
  nativeLayoutRevisions,
  renderFirmwareNativePage,
  v1LayoutPresetForSize,
  v1PowerStatusFrame,
  v1BoardDisplayName,
  v1DeviceInfoQrGeometry,
  v1BluetoothDisplayLabel,
  v1FactoryBluetoothName,
  type NativePageKind,
} from './nativePageRenderer'

const scaleEnumByPercent: Record<number, number> = {
  100: 1,
  125: 4,
  150: 2,
  175: 5,
  200: 3,
}

async function sha256(bytes: Uint8Array) {
  const copy = Uint8Array.from(bytes)
  const digest = await crypto.subtle.digest('SHA-256', copy.buffer)
  return Array.from(new Uint8Array(digest), (value) => value.toString(16).padStart(2, '0')).join('')
}

describe('browser/native C page renderer parity', () => {
  it('uses the firmware timestamp and lunar conversion semantics', () => {
    const time = firmwareTransformTime(golden.timestamp)
    expect(time).toEqual({ year: 2026, month: 9, day: 7, hour: 14, minute: 28, second: 0, weekday: 1 })
    expect(firmwareSolarToLunar(time.year, time.month, time.day)).toEqual({ year: 2026, month: 7, day: 26, isLeap: 0 })
  })
})

describe('v1 browser layout candidate', () => {
  it('never restores lunar cells on 152x152 or 200x200 calendars', () => {
    for (const side of [152, 200]) for (const nativeUiScale of [1, 4, 2, 5, 3]) for (const weekStart of [0, 1]) {
      expect(v1LayoutPresetForSize(side, side).calendarLunarCells).toBe(false)
      const scene: NativeScene = { parts: [] }
      renderFirmwareNativePage({width:side,height:side,page:'calendar',nativeUiScale,weekStart,scene,
        timestamp:Date.UTC(2026,7,31,14,28)/1000})
      const grid=scene.parts.find(part=>part.element.type==='calendar')!
      expect(grid.element.options?.showLunar).toBe(false)
      const text=grid.commands.filter(command=>command.type==='text')
      expect(text.filter(command=>calendarTextRole(command.text)==='day')).toHaveLength(31)
      expect(text.some(command=>calendarTextRole(command.text)==='lunar')).toBe(false)
    }
    // Keep the existing rectangular detail threshold and every larger layout.
    expect(v1LayoutPresetForSize(250,128).calendarLunarCells).toBe(false)
    expect(v1LayoutPresetForSize(296,152).calendarLunarCells).toBe(true)
    expect(v1LayoutPresetForSize(400,300).calendarLunarCells).toBe(true)
  })
  it('retains real lunar cell labels on 240x240 at every scale and week start', () => {
    for (const nativeUiScale of [1, 4, 2, 5, 3]) for (const weekStart of [0, 1]) {
      expect(v1LayoutPresetForSize(240, 240)).toMatchObject({ family: 'compact-square', calendarLunarCells: true })
      const scene: NativeScene = { parts: [] }
      renderFirmwareNativePage({width:240,height:240,page:'calendar',nativeUiScale,weekStart,scene,
        timestamp:Date.UTC(2026,7,31,14,28)/1000})
      const grid=scene.parts.find(part=>part.element.type==='calendar')!
      expect(grid.element.options?.showLunar).toBe(true)
      expect(v1PowerStatusFrame({width:240,height:240,nativeUiScale,page:'calendar'}).height).toBe(27)
      const text=grid.commands.filter(command=>command.type==='text')
      expect(text.filter(command=>calendarTextRole(command.text)==='day')).toHaveLength(31)
      expect(text.filter(command=>calendarTextRole(command.text)==='lunar').length).toBeGreaterThan(20)
      expect(text.some(command=>command.text==='班'||command.text==='休')).toBe(false)
      expect(text.filter(command=>calendarTextRole(command.text)==='day').every(command=>command.font==='wqy9-lunar' && command.scale===256)).toBe(true)
    }
  })
  it('uses readable square calendar numerals and a proportional fixed clock on 200x200', () => {
    for (const month of [2,5,8,12]) for (const weekStart of [0,1]) for (const nativeUiScale of [1,4,2,5,3]) {
      const scene: NativeScene = {parts:[]}
      renderFirmwareNativePage({width:200,height:200,page:'calendar',scene,weekStart,nativeUiScale,
        timestamp:Date.UTC(2026,month-1,1,9,5)/1000})
      const grid=scene.parts.find(part=>part.element.type==='calendar')!
      const days=grid.commands.filter(command=>command.type==='text' && calendarTextRole(command.text)==='day')
      expect(days.length).toBeGreaterThanOrEqual(28)
      expect(days.every(command=>command.type==='text' && command.font==='wqy12-lunar' && command.scale===240)).toBe(true)
      const clock: NativeScene={parts:[]}
      renderFirmwareNativePage({width:200,height:200,page:'clock',scene:clock,weekStart,nativeUiScale})
      const box=clock.parts.find(part=>part.element.type==='clock')!.element.source
      expect(box.height).toBe(62)
      expect(box.x*2+box.width).toBe(200)
      expect(box.y+box.height).toBeLessThan(166)
    }
  })
  it('retains the unthickened native small calendar face at 152px',()=>{
    const scene:NativeScene={parts:[]}
    renderFirmwareNativePage({width:152,height:152,page:'calendar',scene})
    const grid=scene.parts.find(p=>p.element.type==='calendar')!
    const days=grid.commands.filter(c=>c.type==='text'&&calendarTextRole(c.text)==='day')
    expect(days).toHaveLength(30)
    expect(days.every(c=>c.type==='text'&&c.font==='wqy9-lunar'&&c.scale===256)).toBe(true)
  })
  it('retains square BLE brackets and all ID4 glyphs at native size',()=>{
    for(const side of [152,200])for(const page of ['calendar','clock'] as const){
      const scene:NativeScene={parts:[]}
      renderFirmwareNativePage({width:side,height:side,page,bluetoothName:'IKT_TL_0357',scene})
      const labels=scene.parts.flatMap(p=>p.commands).filter(c=>c.type==='text'&&c.text==='[0357]')
      expect(labels).toHaveLength(1)
      expect(labels.every(c=>c.type==='text'&&c.font==='wqy9-lunar'&&c.scale===256)).toBe(true)
      const fallback:NativeScene={parts:[]}
      renderFirmwareNativePage({width:side,height:side,page,bluetoothName:'IKT_TL_A3FF',scene:fallback})
      const latin=fallback.parts.flatMap(p=>p.commands).filter(c=>c.type==='text'&&c.text==='[A3FF]')
      expect(latin).toHaveLength(1)
      expect(latin.every(c=>c.type==='text'&&c.font==='wqy9-lunar'&&c.scale===256)).toBe(true)
    }
  })
  it('fits the exact BLE independently of Name and never substitutes a product alias', () => {
    for (const platform of ['GR', 'OM', 'TL', 'NRF', 'DA']) {
      const bluetoothName = `IKT_${platform}_6775`
      for (const [width, height] of [[152, 152], [200, 200], [212, 104], [250, 128], [296, 128], [296, 152], [400, 300]]) {
        const scene: NativeScene = { parts: [] }
        renderFirmwareNativePage({ width: width!, height: height!, page: 'device-info', layoutRevision: 'v1',
          deviceName: 'Kitchen', bluetoothName, scene })
        const text = (key: string) => scene.parts.find((part) => part.element.key === `info-value-${key}:0`)
          ?.commands.filter((command) => command.type === 'text').map((command) => command.text).join('')
        if (v1LayoutPresetForSize(width!,height!).family === 'compact-square') expect(text('BLE')).toBe('6775')
        else expect(bluetoothName.startsWith(text('BLE') || '\0')).toBe(true)
        expect(text('PCB')).toBeUndefined()
        expect(text('MCU')).toBe('--')
        if (width === 296 && height === 152 || width === 400) expect(text('NAME')).toBe('Kitchen')
      }
    }
  })

  it('uses the build firmware version by default and retains an explicitly supplied device version', () => {
    for (const firmwareVersion of [undefined, '0.2.0', '0.2.1', '0.3.0']) {
      const scene: NativeScene = { parts: [] }
      renderFirmwareNativePage({ width: 400, height: 300, page: 'device-info', layoutRevision: 'v1',
        firmwareVersion, scene })
      const value = scene.parts.find(part => part.element.key === 'info-value-FW:0')
        ?.commands.filter(command => command.type === 'text').map(command => command.text).join('')
      expect(value).toBe(`v${firmwareVersion ?? INKTAG_FIRMWARE_VERSION.version}`)
    }
  })

  it('requires both logical axes for taller fallback layouts', () => {
    for (const [width, height] of [[296, 128], [296, 151], [400, 128], [600, 128], [128, 600], [400, 151]]) {
      const preset = v1LayoutPresetForSize(width!, height!)
      expect(preset.family).toBe('compact-rect')
      expect(preset.calendarLunarCells).toBe(Math.min(width!, height!) >= 152)
    }
    expect(v1LayoutPresetForSize(296, 152).calendarLunarCells).toBe(true)
    expect(v1LayoutPresetForSize(400, 152).family).toBe('compact-rect')
  })

  it('uses ID4 only on compact factory labels, retaining exact wide/custom names', () => {
    const gfx = new DevicePixelGfx(152, 152)
    for (const platform of ['GR', 'OM', 'TL', 'NRF', 'DA']) for (const prefix of ['IKT', 'InkTag']) {
      const name = `${prefix}_${platform}_6775`
      expect(v1FactoryBluetoothName(name)).toBe(`${platform}_6775`)
      expect(v1BluetoothDisplayLabel(gfx, name, 200, 256, false, true)).toBe('6775')
      expect(v1BluetoothDisplayLabel(gfx, name, 200, 256, false, false)).toBe(name)
    }
    expect(v1BluetoothDisplayLabel(gfx, 'IKT_NRF_6775', 30, 256, false, true)).toBe('6775')
    for (const name of ['IKT_Home_6775', 'IKT_NRF_desk', 'IKT_NRF_6775_OTA', 'IKT_NRF_677', 'IKT_NRF_677Z', 'InkTag_Home_A1', 'ikt_NRF_6775', 'InkTag kitchen']) {
      expect(v1FactoryBluetoothName(name)).toBeNull()
      expect(v1BluetoothDisplayLabel(gfx, name, 200, 256, false, true)).toBe(name)
    }
  })

  it('uses the same board-name font/height fit as C', () => {
    const gfx = new DevicePixelGfx(152, 152)
    const name = 'nrf52832_priceetag'
    expect(v1BoardDisplayName(gfx, name, 'priceetag', 200, 24, 256, 256)).toBe(name)
    gfx.setFont('wqy9-lunar'); gfx.setFontScale(256)
    const width = gfx.getUTF8Width(name), height = gfx.getFontHeight()
    expect(v1BoardDisplayName(gfx, name, 'priceetag', width, height, 256, 256)).toBe(name)
    expect(v1BoardDisplayName(gfx, name, 'priceetag', width - 1, height, 256, 256)).toBe('priceetag')
    expect(v1BoardDisplayName(gfx, name, 'priceetag', width, height - 1, 256, 256)).toBe('priceetag')
    expect(v1BoardDisplayName(gfx, name, undefined, 1, 1)).toBe(name)
    expect(v1BoardDisplayName(gfx, '', undefined, 200, 24)).toBe('BOARD')
    const large = new DevicePixelGfx(800, 480)
    expect(v1BoardDisplayName(large, name, 'priceetag', 64, 24, 384, 160)).toBe('priceetag')
    expect(v1BoardDisplayName(large, name, 'priceetag', 400, 32, 384, 160)).toBe(name)
  })

  for (const fixture of deviceQrGolden.cases) {
    it(`${fixture.width}x${fixture.height}/${fixture.modules}-module QR matches shipping-C pixels`, () => {
      const { width, height, modules, divider, fnv } = fixture
      const qr = { size: modules, data: new Uint8Array(Math.ceil(modules * modules / 8)).fill(255) }
      const header = v1PowerStatusFrame({ width, height, nativeUiScale: 1 }).height
      const compact = v1LayoutPresetForSize(width, height).family.startsWith('compact-')
      const footer = compact ? 18 : width > 500 ? 31 : 21
      const geometry = v1DeviceInfoQrGeometry(width, height, header, height - header - footer, qr)
      expect(geometry.qrSectionWidth).toBe(divider)
      const fixedSquareSide = width === height && (width === 152 || width === 200)
        ? (width === 152 ? 66 : 99) : 0
      const outerSide = fixedSquareSide && modules + 8 <= fixedSquareSide
        ? fixedSquareSide : (modules + 8) * geometry.qrScale
      expect(geometry.qrSide).toBe(outerSide)
      expect(geometry.qrX).toBeGreaterThanOrEqual(0)
      expect(geometry.qrX + geometry.qrSide).toBeLessThanOrEqual(divider)
      const options = { width, height, page: 'device-info' as const, layoutRevision: 'v1' as const,
        bluetoothName: 'IKT_NRF_6775', boardName: 'nrf52832_priceetag', boardNameShort: 'priceetag' }
      const frame = renderFirmwareNativePage({ ...options, deviceInfoQr: qr })
      const reference = renderFirmwareNativePage(options)
      for (let y = header; y < height - footer; ++y) {
        expect(frame.pixels[y * width + divider]).toBe(1)
        expect(frame.pixels.slice(y * width + divider, (y + 1) * width)).toEqual(
          reference.pixels.slice(y * width + divider, (y + 1) * width))
      }
      for (let y = 0; y < geometry.qrSide; ++y) for (let x = 0; x < geometry.qrSide; ++x) {
        const quietStart = Math.floor(4 * outerSide / (modules + 8))
        const quietEnd = Math.floor((modules + 4) * outerSide / (modules + 8))
        const quiet = x < quietStart || y < quietStart || x >= quietEnd || y >= quietEnd
        expect(frame.pixels[(geometry.qrY + y) * width + geometry.qrX + x]).toBe(quiet ? 0 : 1)
      }
      let hash = 2166136261
      for (const pixel of frame.pixels) hash = Math.imul(hash ^ pixel, 16777619) >>> 0
      expect(hash).toBe(fnv)
    })
  }

  for (const fixture of deviceQrGolden.metadataCases) {
    it(`${fixture.width}x${fixture.height}/${fixture.bluetoothName} separates Name/BLE with shipping-C pixel parity`, () => {
      const frame = renderFirmwareNativePage({ ...fixture, page: 'device-info', layoutRevision: 'v1',
        boardName: 'nrf52832_priceetag', boardNameShort: 'priceetag' })
      let hash = 2166136261
      for (const pixel of frame.pixels) hash = Math.imul(hash ^ pixel, 16777619) >>> 0
      expect(hash).toBe(fixture.fnv)
    })
  }

  const screens = [
    [152, 152], [200, 200], [212, 104], [250, 128], [296, 152],
    [400, 300], [600, 448], [752, 568], [800, 480],
  ] as const
  const pages = ['calendar', 'clock', 'device-info'] as const
  const powerStatusPages = ['calendar', 'clock', 'device-info'] as const

  it('exposes only the current product revision and refuses retired layouts', () => {
    expect(() => renderFirmwareNativePage({width: 152, height: 152, page: 'low-battery'})).toThrow(/retired/)
    expect(nativeLayoutRevisions).toEqual(['v1'])
    expect(currentNativeLayoutRevision).toBe('v1')
    expect(() => renderFirmwareNativePage({width: 200, height: 200, page: 'calendar',
      layoutRevision: 'v0' as never})).toThrow('Unsupported native layout revision')
  })

  for (const [width, height] of screens) {
    for (const page of pages) {
      it(`${width}x${height}/${page} renders both edition treatments`, () => {
        const free = renderFirmwareNativePage({ width, height, page, edition: 'base', layoutRevision: 'v1' })
        const pro = renderFirmwareNativePage({ width, height, page, edition: 'pro', layoutRevision: 'v1' })
        const changedPower = renderFirmwareNativePage({
          width, height, page, edition: 'base', layoutRevision: 'v1', temperature: 35, voltage: 2600,
        })
        expect(free.pixels).toHaveLength(width * height)
        expect(pro.pixels).toHaveLength(width * height)
        expect(free.pixels.some((pixel) => pixel !== 0)).toBe(true)
        expect(pro.pixels.some((pixel) => pixel !== 0)).toBe(true)
        expect(free.pixels).not.toEqual(pro.pixels)
        expect(free.pixels).not.toEqual(changedPower.pixels)
        if (page === 'calendar') {
          const sundayFirst = renderFirmwareNativePage({
            width, height, page, edition: 'base', layoutRevision: 'v1', weekStart: 0,
          })
          expect(free.pixels).not.toEqual(sundayFirst.pixels)
        }
      })
    }
  }

  it('identifies the production v1 C oracle', () => {
    expect(v1CandidateGolden.layoutRevision).toBe('v1')
  })

  for (const fixture of v1CandidateGolden.boardNames) {
    it(`${fixture.width}x${fixture.height}/${fixture.boardName}/${fixture.edition} matches the C board-name oracle`, async () => {
      const frame = renderFirmwareNativePage({ ...fixture, page: 'device-info',
        edition: fixture.edition as 'base' | 'pro', layoutRevision: 'v1' })
      expect(await sha256(frame.pixels)).toBe(fixture.pixelSha256)
    })
  }

  it('routes all supported resolutions through exactly four layout families', () => {
    expect(screens.map(([width, height]) => v1LayoutPresetForSize(width, height).family)).toEqual([
      'compact-square', 'compact-square',
      'compact-rect', 'compact-rect', 'compact-rect',
      'normal',
      'large', 'large', 'large',
    ])
    expect(v1LayoutPresetForSize(296, 152).calendarLunarCells).toBe(true)
    expect(v1LayoutPresetForSize(500, 300).family).toBe('normal')
    expect(v1LayoutPresetForSize(501, 300).family).toBe('large')
  })

  it('uses one power-status frame on every page at the same resolution', () => {
    for (const [width, height] of screens) for (const nativeUiScale of [1, 4, 2, 5, 3]) {
      const frame = v1PowerStatusFrame({ width, height, nativeUiScale, page:'calendar' })
      const crops = powerStatusPages.map((page) => {
        const rendered = renderFirmwareNativePage({ width, height, page, nativeUiScale, edition: 'base', layoutRevision: 'v1' })
        const crop: number[] = []
        const pageFrame = v1PowerStatusFrame({width,height,nativeUiScale,page})
        const commonHeight = Math.min(...powerStatusPages.map(kind=>v1PowerStatusFrame({width,height,nativeUiScale,page:kind}).height))
        for (let offset = 2-commonHeight; offset < -1; offset += 1) {
          const y=pageFrame.height+offset
          crop.push(...rendered.pixels.slice(y * width + frame.x, y * width + frame.x + frame.width))
        }
        return crop
      })
      expect(crops[1], `${width}x${height} clock power status`).toEqual(crops[0])
      if(width!==200 || height!==200)expect(crops[2], `${width}x${height} device-info power status`).toEqual(crops[0])
    }
  })

  it('caps compact lunar-cell text at native size but retains larger wide-page scales', () => {
    const normal = renderFirmwareNativePage({ width: 296, height: 152, page: 'calendar', nativeUiScale: 1, layoutRevision: 'v1' })
    const enlarged = renderFirmwareNativePage({ width: 296, height: 152, page: 'calendar', nativeUiScale: 2, layoutRevision: 'v1' })
    expect(enlarged.pixels).toEqual(normal.pixels)
    // The 200px square uses a larger native numeral master, not bitmap zoom.
    expect(renderFirmwareNativePage({width:200,height:200,page:'calendar',nativeUiScale:2}).pixels)
      .toEqual(renderFirmwareNativePage({width:200,height:200,page:'calendar',nativeUiScale:1}).pixels)
    expect(renderFirmwareNativePage({width: 400, height: 300, page: 'calendar', nativeUiScale: 2}).pixels)
      .not.toEqual(renderFirmwareNativePage({width: 400, height: 300, page: 'calendar', nativeUiScale: 1}).pixels)
  })

  for (const asset of v1CandidateGolden.renders) {
    it(`${asset.width}x${asset.height}/${asset.page}/${asset.edition} is pixel-identical to the v1 C candidate`, async () => {
      const frame = renderFirmwareNativePage({
        width: asset.width,
        height: asset.height,
        page: asset.page as NativePageKind,
        nativeUiScale: scaleEnumByPercent[asset.scalePercent],
        edition: asset.edition as 'base' | 'pro',
        layoutRevision: 'v1',
      })
      expect(await sha256(frame.pixels), `${asset.width}x${asset.height}/${asset.page}/${asset.edition}`).toBe(asset.pixelSha256)
    })
  }
})


describe('ordinary low-battery status icon', () => {
  it('uses the GUI-only threshold independently of power admission', () => {
    const gui = readFileSync(new URL('../../../firmware/common/gui/inktag_gui.h', import.meta.url), 'utf8')
    const policy = readFileSync(new URL('../../../firmware/common/power/inktag_power.h', import.meta.url), 'utf8')
    expect(gui).toMatch(new RegExp(`#define\\s+GUI_BATTERY_RED_MAX_MV\\s+${DEVICE_BATTERY_RED_MAX_MV}u`))
    expect(policy).toMatch(/#define\s+INKTAG_POWER_POLICY_WARNING_MV\s+2400u/)
    expect(policy).toMatch(/#define\s+INKTAG_POWER_POLICY_FLASH_MIN_MV\s+2400u/)
    expect(policy).toMatch(/#define\s+INKTAG_POWER_POLICY_OTA_MIN_MV\s+2600u/)
  })

  it('changes only icon ink, preserving geometry and unknown-voltage appearance', () => {
    for (const voltage of [0, 2399, 2400, DEVICE_BATTERY_RED_MAX_MV - 1,
      DEVICE_BATTERY_RED_MAX_MV, DEVICE_BATTERY_RED_MAX_MV + 1, 3300]) {
      const paint = (batteryWarningColor: 1 | 2) => {
        const gfx = new DevicePixelGfx(100, 30)
        drawDevicePowerStatus(gfx, {x: 0, y: 0, width: 100, height: 30}, {
          temperature: -128, voltageMillivolts: voltage, fontScaleQ8: 256,
          batteryWarningColor,
        })
        return gfx.frame.pixels
      }
      const colour = paint(2), monochrome = paint(1)
      expect(colour.some(pixel => pixel !== 0)).toBe(true)
      expect(Uint8Array.from(colour, pixel => pixel === 2 ? 1 : pixel)).toEqual(monochrome)
      expect(colour.some(pixel => pixel === 2)).toBe(voltage > 0 && voltage <= DEVICE_BATTERY_RED_MAX_MV)
      expect(monochrome.some(pixel => pixel === 2)).toBe(false)
    }
  })
})
