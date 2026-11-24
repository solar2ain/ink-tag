import { renderToStaticMarkup } from 'react-dom/server'
import { beforeEach, describe, expect, it, vi } from 'vitest'
vi.mock('../store/device', async (importOriginal) => {
  const original = await importOriginal<typeof import('../store/device')>()
  return { useDeviceStore: Object.assign(() => original.useDeviceStore.getState(), original.useDeviceStore) }
})
import { DeviceImageSlots } from './DeviceImageSlots'
import { useDeviceStore } from '../store/device'

describe('resource readiness display', () => {
  beforeEach(() => {
    useDeviceStore.getState().resetDevice()
    useDeviceStore.getState().setConnection('connected')
    useDeviceStore.getState().setCapabilities({ imageStore: true })
  })
  it('shows loading rather than a false empty store without locking the global action', () => {
    useDeviceStore.getState().setResourceReadState('loading')
    const html = renderToStaticMarkup(<DeviceImageSlots />)
    expect(html).toContain('正在读取设备图槽')
    expect(html).toContain('读取中')
    expect(html).not.toContain('设备图片槽为空')
    expect(useDeviceStore.getState().action).toBe('idle')
  })
  it('shows a retryable failure rather than claiming an empty store', () => {
    useDeviceStore.getState().setResourceReadState('error', '仍未就绪，请稍后刷新')
    const html = renderToStaticMarkup(<DeviceImageSlots />)
    expect(html).toContain('图槽读取未完成')
    expect(html).toContain('请稍后刷新')
    expect(html).not.toContain('设备图片槽为空')
  })
  it('does not call declared physical storage absent when no frame is admitted', () => {
    useDeviceStore.getState().setCapabilities({ imageStore: false, imageStoreSectors: 16 })
    const html = renderToStaticMarkup(<DeviceImageSlots />)
    expect(html).toContain('64 KiB')
    expect(html).toContain('当前屏幕配置')
    expect(html).not.toContain('当前机型没有可用的图片存储区')
  })
})
