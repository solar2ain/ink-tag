// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { DevicePage } from './DevicePage'
import type { FirmwareActivation } from '../types'

const client = vi.hoisted(() => ({ readActivationRequest: vi.fn(async () => null) }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))

describe('activation status presentation (simulated client)', () => {
  let host: HTMLDivElement
  let root: Root
  const render = async (connection: 'idle' | 'connected', phase: FirmwareActivation['phase']) => {
    useDeviceStore.setState({ connection, activation: { phase, edition: phase === 'active' ? 'advanced' : 'base' } })
    await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=activation']}><DevicePage /></MemoryRouter>))
  }
  const status = () => Array.from(host.querySelectorAll('.activation-status > div')).find((row) => row.querySelector('dt')?.textContent === '激活状态')?.querySelector('dd')?.textContent

  beforeEach(() => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    vi.clearAllMocks()
    useDeviceStore.getState().resetDevice()
    host = document.createElement('div')
    document.body.append(host)
    root = createRoot(host)
  })
  afterEach(async () => {
    await act(async () => root.unmount())
    host.remove()
    useDeviceStore.getState().resetDevice()
    vi.unstubAllGlobals()
  })

  it('uses the device-facing unsupported label and leaves all activation actions disabled', async () => {
    await render('connected', 'unsupported')
    expect(status()).toBe('该设备不支持激活')
    expect(host.textContent).not.toContain('当前固件未提供激活协议')
    const actions = Array.from(host.querySelectorAll('button')).filter((button) => ['刷新状态', '重新生成', '激活设备'].includes(button.textContent || ''))
    expect(actions).toHaveLength(3)
    expect(actions.every((button) => button.disabled)).toBe(true)
    expect(client.readActivationRequest).not.toHaveBeenCalled()
  })
  it.each(['unsupported', 'active'] as const)('does not present the offline %s snapshot as a current device status', async (phase) => {
    await render('idle', phase)
    expect(status()).toBe('连接设备后读取')
    expect(client.readActivationRequest).not.toHaveBeenCalled()
  })
  it.each([
    ['unavailable', '无法读取设备激活身份'],
    ['inactive', '基础版'],
    ['active', '高级版已激活'],
    ['invalid', '设备激活信息损坏'],
  ] as const)('keeps the connected %s state distinct', async (phase, label) => {
    await render('connected', phase)
    expect(status()).toBe(label)
    expect(status()).not.toBe('该设备不支持激活')
    if (phase === 'unavailable') expect(host.querySelector('textarea')?.disabled).toBe(true)
  })
})
