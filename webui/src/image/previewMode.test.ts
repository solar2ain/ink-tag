import { describe, expect, it } from 'vitest'
import { imageCompareLabel, resolveImagePreviewLayer } from './previewMode'

describe('image preview comparison', () => {
  it('shows the adjusted image and holds back to the original', () => {
    expect(resolveImagePreviewLayer('adjusted', false, false)).toBe('adjusted')
    expect(resolveImagePreviewLayer('adjusted', true, false)).toBe('original')
    expect(imageCompareLabel()).toBe('按住看原图')
  })

  it('compares the dithered tab with the original image', () => {
    expect(resolveImagePreviewLayer('dithered', false, false)).toBe('dithered')
    expect(resolveImagePreviewLayer('dithered', true, false)).toBe('original')
    expect(imageCompareLabel()).toBe('按住看原图')
  })

  it('always shows the original while composing the crop', () => {
    expect(resolveImagePreviewLayer('adjusted', true, true)).toBe('original')
    expect(resolveImagePreviewLayer('dithered', false, true)).toBe('original')
  })
})
