import { describe, expect, it } from 'vitest'
import { canSafelyReplaceImage, canSafelyStoreImage, shouldRefreshAfterImageSend } from './sendMode'

describe('image send mode', () => {
  it('requires free capacity for appending', () => {
    expect(canSafelyStoreImage(undefined)).toBe(true)
    expect(canSafelyStoreImage(2)).toBe(true)
    expect(canSafelyStoreImage(0)).toBe(false)
  })

  it('allows selecting existing slots independently of append capacity', () => {
    expect(canSafelyReplaceImage(2)).toBe(true)
    expect(canSafelyReplaceImage(0)).toBe(false)
  })

  it('always refreshes a direct display transfer', () => {
    expect(shouldRefreshAfterImageSend('display', false)).toBe(true)
  })

  it('lets stored images remain in the slot without refreshing the panel', () => {
    expect(shouldRefreshAfterImageSend('append', false)).toBe(false)
    expect(shouldRefreshAfterImageSend('replace', false)).toBe(false)
  })

  it('preserves the existing save-and-display behavior when selected', () => {
    expect(shouldRefreshAfterImageSend('append', true)).toBe(true)
    expect(shouldRefreshAfterImageSend('replace', true)).toBe(true)
  })
})
