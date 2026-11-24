import { describe, expect, it } from 'vitest'
import type { FirmwareActivation, ImageStoreInfo, ImageStoreItem, ScreenConfig } from '../types'
import { constrainedImageStoreInfo, findVisibleImageRecord, imageSlotAvailability, visibleImageRecords } from './imageQuota'

const editions: FirmwareActivation[] = [
  { phase: 'inactive', edition: 'base' }, { phase: 'active', edition: 'advanced' },
  { phase: 'unsupported', edition: 'base' },
]
const records = [14, 0, 10, 5, 254, 4, 21, 8, 7, 6, 2, 1].map(id => ({ id, compatible: true, contentType: 'image' }) as ImageStoreItem)
const info = { saved: 12, compatible: 12, playable: 12, incompatible: 0, fit: 11, contiguousFit: 9, freeSectors: 27 } as ImageStoreInfo

describe('physical image capacity for every edition', () => {
  for (const activation of editions) for (const screen of [null, { visibleWidth: 400, visibleHeight: 300 } as ScreenConfig]) {
    it(`preserves all records and backend capacity: ${activation.phase}/${screen?.visibleWidth}`, () => {
      const visible = visibleImageRecords(records, activation, screen)
      expect(visible.map(item => item.id)).toEqual([0, 1, 2, 4, 5, 6, 7, 8, 10, 14, 21, 254])
      expect(records[0].id).toBe(14)
      const actual = constrainedImageStoreInfo(info, records, activation, screen)
      expect(actual).toMatchObject(info)
      expect(actual.occupiedIds).toEqual(records.map(item => item.id))
      expect(imageSlotAvailability(actual, activation, screen)).toEqual({ free: 9, pro: null })
      expect(findVisibleImageRecord(visible, 254)?.id).toBe(254)
      for (const bad of [-1, 256, 1.5, NaN]) expect(findVisibleImageRecord(visible, bad)).toBeUndefined()
    })
  }
  it('preserves explicit fixed-slot occupancy and physical exhaustion', () => {
    const fixed = { ...info, fixedSlotCount: 61, occupiedIds: [0, 60], contiguousFit: 0, fit: 0 }
    expect(constrainedImageStoreInfo(fixed, records, editions[0], null).occupiedIds).toEqual([0, 60])
    expect(imageSlotAvailability(fixed, editions[0], null)).toEqual({ free: 0, pro: null })
    expect(constrainedImageStoreInfo(info, null, editions[0], null)).toMatchObject(info)
  })
})
