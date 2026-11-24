import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'
import type { ImageStoreInfo, ImageStoreItem } from '../types'
import { ImageSlotTargetPicker } from './ImageSlotTargetPicker'

const item = (id: number, name: string): ImageStoreItem => ({
  id, name, width: 400, height: 300, bytes: 30_000, format: 2, segments: 2,
  createdAt: 1, compatible: true, contentType: 'image',
})

const info = (occupiedIds: number[]): ImageStoreInfo => ({
  width: 400, height: 300, bytesPerImage: 30000, requiredSectors: 4, totalSectors: 40,
  freeSectors: 32, largestFreeRun: 32, fit: 8, contiguousFit: 8, occupiedIds,
  saved: occupiedIds.length, compatible: occupiedIds.length, playable: occupiedIds.length,
  incompatible: 0, slideshow: false, intervalSeconds: 3600,
})
const render = (options: Partial<Parameters<typeof ImageSlotTargetPicker>[0]> = {}) => renderToStaticMarkup(
  <ImageSlotTargetPicker items={[item(8, 'second'), item(2, 'first')]} storeInfo={info([2, 8])}
    availableCount={4} selectedId={8} newSlotSelected={false}
    onSelectExisting={() => undefined} onSelectNew={() => undefined} {...options} />,
)

describe('image-slot target picker', () => {
  it('lists every slot from one through the capacity, preserving sparse occupancy', () => {
    const html = render()
    expect((html.match(/role="option"/g) || [])).toHaveLength(10)
    for (let number = 1; number <= 10; number++) expect(html).toContain(`aria-label="槽 ${number}，`)
    expect(html).not.toContain('aria-label="槽 0，')
    expect(html.indexOf('槽 3，已使用')).toBeLessThan(html.indexOf('槽 9，已使用'))
    expect(html).toContain('槽 2，空槽')
    expect(html).toContain('image-slot-target occupied selected')
    expect(html).toContain('image-slot-target empty')
    expect(html).toContain('空槽不重排')
  })

  it('selects only the real automatic new-slot target on existing firmware', () => {
    const html = render({selectedId: null, newSlotSelected: true})
    expect(html).toMatch(/aria-label="槽 1，空槽"[^>]*class="image-slot-target empty selected"/)
    expect(html).toMatch(/aria-label="槽 2，空槽"[^>]*disabled=""/)
    expect(html).toContain('当前固件自动分配新槽，请先使用槽 1')
  })

  it('still lists empty slots when capacity or the base allowance prevents appending', () => {
    const html = render({availableCount: 0, newSlotSelected: true})
    expect((html.match(/role="option"/g) || [])).toHaveLength(10)
    expect(html).toMatch(/aria-label="槽 1，空槽"[^>]*disabled=""/)
    expect(html).not.toContain('empty selected')
    expect(html).toContain('当前容量或版本额度已满')
  })

  it('does not mislabel hidden occupied records as empty or allow replacing them', () => {
    const html = render({items: [item(2, 'visible')], storeInfo: info([2, 8])})
    expect(html).toMatch(/aria-label="槽 9，已使用，当前版本不可使用"[^>]*disabled=""/)
    expect(html).not.toContain('槽 9，空槽')
  })

  it('keeps an occupied high handle visible even beyond current-screen estimated capacity', () => {
    const html = render({items: [item(14, 'historical')], storeInfo: info([14])})
    expect((html.match(/role="option"/g) || [])).toHaveLength(15)
    expect(html).toContain('槽 15，已使用，historical')
  })

  it('never invents empty occupancy while the snapshot is unknown', () => {
    const html = render({storeInfo: {...info([2, 8]), occupiedIds: undefined}})
    expect(html).toContain('槽 1，尚未读取')
    expect(html).not.toContain('槽 1，空槽')
    expect(html).toContain('image-slot-target unknown')
  })

  it('permits any reported fixed empty slot without compacting holes', () => {
    const html = render({storeInfo: {...info([2, 8]), fixedSlotCount: 10}, selectedId: 9, newSlotSelected: true})
    expect((html.match(/role="option"/g) || [])).toHaveLength(10)
    expect(html).not.toContain('disabled=""')
    expect(html).toMatch(/aria-label="槽 10，空槽"[^>]*class="image-slot-target empty selected"/)
    expect(html).toContain('当前屏幕共 10 个槽，可指定空槽保存')
  })

  it('caps capacity at 255 and never exposes the automatic sentinel as slot 256', () => {
    const html = render({storeInfo: {...info([]), fixedSlotCount: 255}, items: []})
    expect((html.match(/role="option"/g) || [])).toHaveLength(255)
    expect(html).toContain('槽 255，空槽')
    expect(html).not.toContain('槽 256，')
  })

  it('uses exact fixed count rather than combining leftover sectors across Flash regions', () => {
    const html = render({storeInfo: {...info([]), fixedSlotCount: 8}, items: []})
    expect((html.match(/role="option"/g) || [])).toHaveLength(8)
  })
})
