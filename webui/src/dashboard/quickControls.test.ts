import { describe, expect, it } from 'vitest'
import { calendarCountdownSupported, canStartSlideshow, clockPartialRefreshAvailable, dashboardStoredPages, dashboardTemplates, slideshowImageCount } from './quickControls'
import type { DeviceCapabilities, FirmwareActivation, ImageStoreInfo, ImageStoreItem, ScreenConfig } from '../types'
import { useDeviceStore } from '../store/device'
import { CapabilityV2 } from '../protocol/generatedCommandsV2'
import { decodeCapabilitiesV2 } from '../../../packages/inktag-client-core/src/decoders'

it('requires the runtime clock capability and verified Pro, not the controller or version',()=>{
  for(const supported of [undefined,false,true])for(const phase of ['active','inactive','invalid','unavailable','unsupported'] as const)
    for(const edition of ['base','advanced'] as const){
      expect(clockPartialRefreshAvailable({clockPartialRefresh:supported} as DeviceCapabilities,
        {phase,edition} as FirmwareActivation)).toBe(supported===true&&phase==='active'&&edition==='advanced')
    }
  useDeviceStore.getState().setCapabilities({clockPartialRefresh:true})
  useDeviceStore.getState().resetDevice()
  expect(useDeviceStore.getState().capabilities.clockPartialRefresh).toBe(false)
  const response=new Uint8Array(17);response[0]=1
  new DataView(response.buffer).setUint32(1,CapabilityV2.CLOCK_PARTIAL_REFRESH,true)
  expect(decodeCapabilitiesV2(response)!.mask).toBe(0x80000000)
})

const info = (playable: number) => ({ playable } as ImageStoreInfo)

describe('dashboard slideshow routing', () => {
  it('starts only when the device has a compatible stored image', () => {
    expect(canStartSlideshow(true, info(2))).toBe(true)
    expect(canStartSlideshow(true, info(0))).toBe(false)
  })

  it('routes to image upload when storage is unavailable or unknown', () => {
    expect(canStartSlideshow(false, info(2))).toBe(false)
    expect(canStartSlideshow(true, null)).toBe(false)
  })

  it('counts only compatible ordinary images for the slideshow label', () => {
    const items = [
      { id: 1, compatible: true, contentType: 'image' },
      { id: 2, compatible: false, contentType: 'image' },
      { id: 3, compatible: true, contentType: 'schedule' },
    ] as ImageStoreItem[]
    expect(slideshowImageCount(items)).toBe(1)
  })

  it('numbers every compatible structured page by its type', () => {
    const items = [
      { id: 1, compatible: true, contentType: 'schedule' },
      { id: 2, compatible: true, contentType: 'wifi' },
      { id: 3, compatible: true, contentType: 'schedule' },
      { id: 4, compatible: false, contentType: 'todo' },
      { id: 5, compatible: true, contentType: 'image' },
    ] as ImageStoreItem[]
    expect(dashboardStoredPages(items).map(({ item, label }) => [item.id, label])).toEqual([
      [1, '课表 1'], [2, 'Wi-Fi 1'], [3, '课表 2'],
    ])
  })
})

const screen = (width: number, height: number, transform = 0) => ({
  width, height, visibleWidth: width, visibleHeight: height, visibleX: 0,
  visibleY: 0, transform, driver: 0x24, recipe: 0, color: 2,
} as ScreenConfig)

describe('dashboard screen-aware controls', () => {
  it('offers custom countdowns only for 4.2-inch-class and larger layouts', () => {
    expect(calendarCountdownSupported(screen(296, 152))).toBe(false)
    expect(calendarCountdownSupported(screen(400, 300))).toBe(true)
    expect(calendarCountdownSupported(screen(480, 800, 1))).toBe(true)
  })

  it('filters connected-device templates to its logical resolution', () => {
    const items = dashboardTemplates(screen(400, 300))
    expect(items).toHaveLength(3)
    expect(items.every((item) => item.width === 400 && item.height === 300)).toBe(true)
  })

  it('shows three different static template types while disconnected', () => {
    const templates = dashboardTemplates(null)
    expect(templates).toHaveLength(3)
    expect(new Set(templates.map(template => template.kind)).size).toBe(3)
    expect(new Set(templates.map(template => template.id)).size).toBe(3)
  })

  it('backfills common templates after excluding native and host dynamic pages', () => {
    for (const config of [null, screen(250, 128), screen(400, 300)]) {
      const templates = dashboardTemplates(config)
      expect(templates).toHaveLength(3)
      expect(templates.every(template => !['calendar', 'clock', 'weather', 'date-card'].includes(template.kind))).toBe(true)
    }
  })
})
