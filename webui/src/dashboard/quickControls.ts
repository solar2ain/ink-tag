import type { PageTemplate } from '../data/templates'
import { pageTemplates, templatesForResolution } from '../data/templates'
import { canvasSizeForScreen } from '../protocol/screen'
import { templatePageMode } from '../data/pageMode'
import { imageSlotContentTypeLabels, type DeviceCapabilities, type FirmwareActivation, type ImageStoreInfo, type ImageStoreItem, type ScreenConfig } from '../types'

/** Firmware reports current backend/profile eligibility, not a controller-name guess. */
export function clockPartialRefreshAvailable(capabilities: DeviceCapabilities, activation: FirmwareActivation) {
  return capabilities.clockPartialRefresh === true && activation.phase === 'active' && activation.edition === 'advanced'
}

export function canStartSlideshow(imageStoreSupported: boolean, info: ImageStoreInfo | null) {
  return imageStoreSupported && Boolean(info && info.playable > 0)
}

export function slideshowImageCount(items: ImageStoreItem[]) {
  return items.filter((item) => item.compatible && item.contentType === 'image').length
}

export function dashboardStoredPages(items: ImageStoreItem[]) {
  const counts = new Map<string, number>()
  return items.filter((item) => item.compatible && item.contentType !== 'image').map((item) => {
    const index = (counts.get(item.contentType) || 0) + 1
    counts.set(item.contentType, index)
    return { item, label: `${imageSlotContentTypeLabels[item.contentType]} ${index}` }
  })
}

export function calendarCountdownSupported(screen: ScreenConfig | null) {
  if (!screen) return false
  const size = canvasSizeForScreen(screen)
  return Math.max(size.width, size.height) >= 400
}

/** Keep the disconnected dashboard useful, but never suggest the wrong size
 * once a device has reported its exact logical viewport. */
export function dashboardTemplates(screen: ScreenConfig | null): PageTemplate[] {
  const size = screen ? canvasSizeForScreen(screen) : null
  const templates = size ? templatesForResolution(size.width, size.height) : pageTemplates
  const staticTemplates = templates.filter(template => templatePageMode(template) === 'static')
  if (size) return staticTemplates.slice(0, 3)
  const preview: PageTemplate[] = []
  for (const template of staticTemplates) {
    if (preview.some(item => item.kind === template.kind)) continue
    preview.push(template)
    if (preview.length === 3) break
  }
  return preview
}
