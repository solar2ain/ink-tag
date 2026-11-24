import type { FirmwareActivation, ImageStoreInfo, ImageStoreItem, ScreenConfig } from '../types'

export function isProActivation(activation: FirmwareActivation) {
  return activation.phase === 'active' && activation.edition === 'advanced'
}

export function imageSlotAvailability(info: ImageStoreInfo, _activation: FirmwareActivation, _screen: ScreenConfig | null) {
  return { free: info.physicalContiguousFit ?? info.contiguousFit, pro: null }
}

/** List every valid device record; capacity is independent of activation. */
export function visibleImageRecords(
  items: readonly ImageStoreItem[],
  _activation: FirmwareActivation,
  _screen: ScreenConfig | null,
) {
  return [...items].sort((left, right) => left.id - right.id)
}

export function findVisibleImageRecord(items: readonly ImageStoreItem[], id: number) {
  if (!Number.isInteger(id) || id < 0 || id > 0xff) return undefined
  return items.find((item) => item.id === id)
}

export function constrainedImageStoreInfo(
  info: ImageStoreInfo,
  items: readonly ImageStoreItem[] | null,
  _activation: FirmwareActivation,
  _screen: ScreenConfig | null,
): ImageStoreInfo {
  return {
    ...info,
    physicalContiguousFit: info.physicalContiguousFit ?? info.contiguousFit,
    occupiedIds: info.fixedSlotCount ? info.occupiedIds : items ? items.map((item) => item.id) : info.occupiedIds,
  }
}
