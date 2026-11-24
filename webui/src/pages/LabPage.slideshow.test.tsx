// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import type { ImageStoreInfo } from '../types'
import { LabPanel } from './LabPage'

const client = vi.hoisted(() => ({ listTests: vi.fn().mockResolvedValue([]), startSlideshow: vi.fn(), stopSlideshow: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
let host: HTMLDivElement, root: Root
const info = (playable: number): ImageStoreInfo => ({ width: 152, height: 152, bytesPerImage: 2888,
  requiredSectors: 1, totalSectors: 40, freeSectors: 40, largestFreeRun: 40, fit: 40, contiguousFit: 40,
  saved: playable, compatible: playable, playable, incompatible: 0, slideshow: false, intervalSeconds: 60 })
const card = () => Array.from(host.querySelectorAll('button')).find(button => button.querySelector('h2')?.textContent === '轮播测试')!
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.clearAllMocks()
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', action: 'idle',
    capabilities: { ...useDeviceStore.getState().capabilities, imageStore: true }, imageStoreInfo: info(0) })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<LabPanel />))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })
it('explains the supported zero-playable state visibly without sending commands', async () => {
  expect(card().disabled).toBe(true)
  expect(card().textContent).toContain('请先上传当前屏幕可播放的图片')
  expect(card().getAttribute('aria-label')).toContain('请先上传')
  expect(card().title).not.toContain('未上报此能力')
  await act(async () => card().click())
  expect(host.querySelector('[role=dialog]')).toBeNull()
  expect(client.startSlideshow).not.toHaveBeenCalled()
})
it('distinguishes unread inventory from a known empty store', async () => {
  await act(async () => useDeviceStore.setState({ imageStoreInfo: null }))
  expect(card().disabled).toBe(true)
  expect(card().textContent).toContain('设备图槽信息尚未读到')
  expect(card().textContent).not.toContain('请先上传')
})
it('keeps unsupported, offline and busy gates distinct from empty images', async () => {
  await act(async () => useDeviceStore.getState().setCapabilities({ imageStore: false }))
  expect(card().title).toBe('当前设备不支持此测试')
  expect(card().textContent).not.toContain('请先上传')
  await act(async () => useDeviceStore.setState({ capabilities: { ...useDeviceStore.getState().capabilities, imageStore: true }, connection: 'idle' }))
  expect(card().disabled).toBe(true)
  expect(card().title).toBe('请先连接设备')
  expect(card().textContent).not.toContain('请先上传')
  await act(async () => useDeviceStore.setState({ connection: 'connected', action: 'ota' }))
  expect(card().disabled).toBe(true)
  expect(card().title).toBe('设备操作进行中，请稍后')
  expect(card().textContent).not.toContain('请先上传')
})
it('preserves the normal playable entry without auto-starting a slideshow', async () => {
  await act(async () => useDeviceStore.setState({ imageStoreInfo: info(2) }))
  expect(card().disabled).toBe(false)
  expect(card().textContent).not.toContain('请先上传')
  await act(async () => card().click())
  expect(host.querySelector('[role=dialog]')?.textContent).toContain('设备需要至少两张兼容的轮播图片')
  expect(client.startSlideshow).not.toHaveBeenCalled()
})
