export const DEFAULT_SLIDESHOW_MINUTES = 60
export const NORMAL_SLIDESHOW_MINUTES = 30
export const TEST_SLIDESHOW_MINUTES = 1
export const MAX_SLIDESHOW_MINUTES = 24 * 60

export function normalizeSlideshowMinutes(value: number, testMode: boolean) {
  const minimum = testMode ? TEST_SLIDESHOW_MINUTES : NORMAL_SLIDESHOW_MINUTES
  const rounded = Number.isFinite(value) ? Math.round(value) : DEFAULT_SLIDESHOW_MINUTES
  return Math.max(minimum, Math.min(MAX_SLIDESHOW_MINUTES, rounded))
}

export function slideshowIntervalSeconds(value: number, testMode: boolean) {
  return normalizeSlideshowMinutes(value, testMode) * 60
}
