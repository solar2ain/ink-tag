import { describe, expect, it } from 'vitest'
import { imageSlotContentTypeIds, imageSlotContentTypeLabels } from './types'

describe('device image-slot content types', () => {
  it('keeps ordinary slideshow images at the legacy zero value', () => {
    expect(imageSlotContentTypeIds.image).toBe(0)
    expect(imageSlotContentTypeLabels.image).toBe('轮播图片')
  })

  it('gives structured pages stable non-slideshow values', () => {
    expect(imageSlotContentTypeIds).toEqual({
      image: 0, schedule: 1, wifi: 2, todo: 3, business: 4, page: 5,
    })
  })
})
