import { describe, expect, it } from 'vitest'
import type { ImageStoreItem } from './types'
import { adjacentPlayableImage } from './imageSlotNavigation'

const item = (id: number, contentType: ImageStoreItem['contentType'] = 'image', compatible = true): ImageStoreItem => ({
  id,
  contentType,
  compatible,
  format: 1,
  segments: 2,
  width: 128,
  height: 250,
  bytes: 8000,
  createdAt: 0,
  name: `slot-${id}`,
})

describe('device image slot navigation', () => {
  const items = [item(0), item(1, 'page'), item(2), item(3, 'image', false), item(7)]

  it('follows the same compatible slideshow order and wraps in both directions', () => {
    expect(adjacentPlayableImage(items, 0, 1)?.id).toBe(2)
    expect(adjacentPlayableImage(items, 7, 1)?.id).toBe(0)
    expect(adjacentPlayableImage(items, 0, -1)?.id).toBe(7)
    expect(adjacentPlayableImage(items, 2, -1)?.id).toBe(0)
  })

  it('starts at the appropriate edge when the selected record is not playable', () => {
    expect(adjacentPlayableImage(items, 1, 1)?.id).toBe(0)
    expect(adjacentPlayableImage(items, 1, -1)?.id).toBe(7)
    expect(adjacentPlayableImage([], null, 1)).toBeNull()
  })
})
