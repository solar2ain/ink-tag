import { describe, expect, it } from 'vitest'
import {
  DEFAULT_SLIDESHOW_MINUTES,
  MAX_SLIDESHOW_MINUTES,
  normalizeSlideshowMinutes,
  slideshowIntervalSeconds,
} from './interval'

describe('slideshow interval', () => {
  it('uses a 30 minute minimum during normal use', () => {
    expect(normalizeSlideshowMinutes(1, false)).toBe(30)
    expect(slideshowIntervalSeconds(60, false)).toBe(3600)
  })

  it('allows a one minute interval only in test mode', () => {
    expect(normalizeSlideshowMinutes(1, true)).toBe(1)
    expect(slideshowIntervalSeconds(1, true)).toBe(60)
  })

  it('rounds and bounds invalid input', () => {
    expect(normalizeSlideshowMinutes(Number.NaN, false)).toBe(DEFAULT_SLIDESHOW_MINUTES)
    expect(normalizeSlideshowMinutes(MAX_SLIDESHOW_MINUTES + 1, true)).toBe(MAX_SLIDESHOW_MINUTES)
    expect(normalizeSlideshowMinutes(45.6, false)).toBe(46)
  })
})
