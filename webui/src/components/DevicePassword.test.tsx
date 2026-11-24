// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { DevicePasswordSettingsDialog } from './DevicePassword'

const client = vi.hoisted(() => ({
  passwordStatus: { enabled: false, authorized: false, failures: 0, retryAfterMs: 0, revision: 0 },
  passwordSupported: vi.fn(() => true),
  configureDevicePassword: vi.fn(),
}))
vi.mock('../protocol/client', () => ({ inktagClient: client }))

describe('device-operation password modal (simulated client)', () => {
  let host: HTMLDivElement
  let root: Root
  const onClose = vi.fn()
  const button = (text: string) => Array.from(host.querySelectorAll('button')).find((item) => item.textContent === text)!
  const click = async (text: string) => act(async () => button(text).click())
  const fill = async (label: string, value: string) => {
    const input = Array.from(host.querySelectorAll('label')).find((item) => item.textContent === label)!.querySelector('input')!
    await act(async () => {
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, value)
      input.dispatchEvent(new Event('input', { bubbles: true }))
    })
  }
  const submit = async () => act(async () => host.querySelector('form')!.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })))
  const enable = async () => act(async () => {
    client.passwordStatus = { enabled: true, authorized: true, failures: 0, retryAfterMs: 0, revision: 1 }
    window.dispatchEvent(new CustomEvent('inktag-password-state'))
  })

  beforeEach(async () => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    vi.clearAllMocks()
    client.passwordStatus = { enabled: false, authorized: false, failures: 0, retryAfterMs: 0, revision: 0 }
    client.passwordSupported.mockReturnValue(true)
    client.configureDevicePassword.mockResolvedValue(undefined)
    useDeviceStore.getState().resetDevice()
    useDeviceStore.setState({ connection: 'connected', deviceId: 'fixture', activation: { phase: 'active', edition: 'advanced' } })
    host = document.createElement('div')
    document.body.append(host)
    root = createRoot(host)
    await act(async () => root.render(<DevicePasswordSettingsDialog onClose={onClose} />))
  })
  afterEach(async () => {
    await act(async () => root.unmount())
    host.remove()
    vi.unstubAllGlobals()
  })

  it('sets a password once through the maintained client and returns to management', async () => {
    await click('设置密码')
    await fill('新密码', 'New12345')
    await fill('确认新密码', 'New12345')
    await submit()
    expect(client.configureDevicePassword).toHaveBeenCalledExactlyOnceWith('New12345', '')
    expect(host.querySelector('form')).toBeNull()
    expect(host.querySelector('[role=status]')?.textContent).toBe('设备密码已更新。')
  })
  it.each(['modify', 'delete'])('passes the freshly entered original password for %s', async (action) => {
    await enable()
    await click(action === 'modify' ? '修改密码' : '删除密码')
    expect(host.querySelector('input')?.required).toBe(true)
    await fill('原密码', 'Old12345')
    if (action === 'modify') {
      await fill('新密码', 'New12345')
      await fill('确认新密码', 'New12345')
    }
    await submit()
    expect(client.configureDevicePassword).toHaveBeenCalledExactlyOnceWith(action === 'modify' ? 'New12345' : null, 'Old12345')
  })
  it('rejects mismatched confirmation without a client call and clears canceled inputs', async () => {
    await click('设置密码')
    await fill('新密码', 'New12345')
    await fill('确认新密码', 'Different8')
    await submit()
    expect(host.querySelector('[role=alert]')?.textContent).toContain('不一致')
    expect(client.configureDevicePassword).not.toHaveBeenCalled()
    await click('取消')
    await click('设置密码')
    expect(Array.from(host.querySelectorAll('input')).every((input) => input.value === '')).toBe(true)
  })
  it('shows a refused or unknown commit without replaying it or reporting success', async () => {
    client.configureDevicePassword.mockRejectedValue(new Error('commit response lost'))
    await click('设置密码')
    await fill('新密码', 'New12345')
    await fill('确认新密码', 'New12345')
    await submit()
    expect(host.querySelector('[role=alert]')?.textContent).toBe('commit response lost')
    expect(host.querySelector('form')).not.toBeNull()
    expect(client.configureDevicePassword).toHaveBeenCalledTimes(1)
    expect(host.textContent).not.toContain('设备密码已更新')
  })
  it('clears sensitive fields on disconnect and disables configuration', async () => {
    await click('设置密码')
    await fill('新密码', 'New12345')
    await act(async () => useDeviceStore.getState().setConnection('idle'))
    expect(host.querySelector('form')).toBeNull()
    expect(button('设置密码').disabled).toBe(true)
    expect(client.configureDevicePassword).not.toHaveBeenCalled()
  })
  it('does not close or duplicate a pending command via Escape or close controls', async () => {
    let finish!: () => void
    client.configureDevicePassword.mockReturnValue(new Promise<void>((resolve) => { finish = resolve }))
    await click('设置密码')
    await fill('新密码', 'New12345')
    await fill('确认新密码', 'New12345')
    await submit()
    expect(host.querySelector<HTMLButtonElement>('[aria-label="关闭窗口"]')!.disabled).toBe(true)
    expect(button('确认').disabled).toBe(true)
    await act(async () => window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' })))
    expect(onClose).not.toHaveBeenCalled()
    expect(client.configureDevicePassword).toHaveBeenCalledTimes(1)
    await act(async () => finish())
  })
})
