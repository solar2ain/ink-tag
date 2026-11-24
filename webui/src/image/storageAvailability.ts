import type { DeviceCapabilities, ImageStoreInfo } from '../types'
import { StatusV2 } from '../protocol/generatedCommandsV2'

// Capability storage size is advisory. It must never enable a device write or
// turn failed/unknown resource metadata into a writable empty store.
export function unavailableImageStorageMessage(capabilities: DeviceCapabilities, readError = '') {
  if (readError) return readError
  if ((capabilities.imageStoreSectors ?? 0) > 0) {
    return `设备已声明 ${capabilities.imageStoreSectors! * 4} KiB 图片存储，但当前屏幕配置下未提供可用图槽。可选择仅显示一次；请核对屏幕配置或存储初始化状态。`
  }
  return '当前固件未提供可用的图片图槽；仍可选择仅显示一次。'
}

export function resourceInfoFailureMessage(status: number) {
  if (status === StatusV2.NO_SPACE) return '当前屏幕图片超出可用图槽容量，或存储区尚未就绪。可选择仅显示一次；请核对屏幕配置或固件状态，不要先清空图片。'
  if (status === StatusV2.IO_ERROR) return '设备图片存储读写失败。请检查设备状态后重试读取，不要先清空图片。'
  if (status === StatusV2.UNSUPPORTED) return '当前固件未提供可用的图片图槽；仍可选择仅显示一次。'
  return undefined
}

export function imageStorageWriteHint(info: ImageStoreInfo) {
  if (info.requiredSectors > info.totalSectors || info.bytesPerImage > info.totalSectors * 4096) {
    return '当前屏幕的一张完整图片超过设备图槽总容量，删除其他图片也无法解决。请选择仅显示一次，或核对屏幕配置。'
  }
  if ((info.physicalContiguousFit ?? info.contiguousFit) > 0 && info.contiguousFit === 0) {
    return '当前图片数量额度已用完，可覆盖已有图片或删除不用的图片。'
  }
  return '设备当前可用图槽空间不足，可覆盖已有图片或删除不用的图片。'
}
