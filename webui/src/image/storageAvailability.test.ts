import { describe, expect, it } from 'vitest'
import { imageStorageWriteHint, resourceInfoFailureMessage, unavailableImageStorageMessage } from './storageAvailability'
import { StatusV2 } from '../protocol/generatedCommandsV2'
import type { DeviceCapabilities, ImageStoreInfo } from '../types'

describe('storage explanations do not invent write eligibility', () => {
  const caps = { imageStore: false, imageStoreSectors: 16 } as DeviceCapabilities
  it('distinguishes declared capacity from absent runtime support', () => {
    expect(unavailableImageStorageMessage(caps)).toContain('64 KiB')
    expect(unavailableImageStorageMessage({ ...caps, imageStoreSectors: 0 })).toContain('固件未提供')
    expect(caps.imageStore).toBe(false)
    expect(unavailableImageStorageMessage(caps, '初始化未完成')).toBe('初始化未完成')
  })
  it('does not suggest deleting data when a whole frame exceeds total capacity', () => {
    const info = { requiredSectors: 20, totalSectors: 16, bytesPerImage: 76000, contiguousFit: 0 } as ImageStoreInfo
    expect(imageStorageWriteHint(info)).toContain('删除其他图片也无法解决')
  })
  it('separates the record quota from physical free space', () => {
    const info = { requiredSectors: 4, totalSectors: 16, bytesPerImage: 15000, contiguousFit: 0, physicalContiguousFit: 2 } as ImageStoreInfo
    expect(imageStorageWriteHint(info)).toContain('数量额度')
    expect(imageStorageWriteHint({ ...info, physicalContiguousFit: 0 })).toContain('空间不足')
  })
  it('keeps unrelated authorization and busy errors untouched', () => {
    expect(resourceInfoFailureMessage(StatusV2.NO_SPACE)).toContain('不要先清空图片')
    expect(resourceInfoFailureMessage(StatusV2.IO_ERROR)).toContain('读写失败')
    expect(resourceInfoFailureMessage(StatusV2.UNSUPPORTED)).toContain('固件未提供')
    expect(resourceInfoFailureMessage(StatusV2.DENIED)).toBeUndefined()
    expect(resourceInfoFailureMessage(StatusV2.BUSY)).toBeUndefined()
  })
})
