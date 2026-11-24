// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { emptyDeviceSettingsCapabilities } from '../protocol/deviceSettings'
import { pinRoles, PinWriteUnconfirmedError, type PinConfig, type PinReport } from '../protocol/pinConfig'
import { RuntimePinSettings } from './RuntimePinSettings'

const client = vi.hoisted(() => ({ getRuntimePinConfig: vi.fn(), setRuntimePinConfig: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
const pins = (): PinConfig => ({ activeLow: 0x3c0, pins: Object.fromEntries(pinRoles.map((role, index) => [role, index < 10 ? index : 255])) as PinConfig['pins'] })
const report = (): PinReport => ({ overridePresent: false, defaultValid: true, editableRoles: 0x3ff, default: pins(), effective: pins() })

describe('board routing uses connection readback and section saves', () => {
  let host: HTMLDivElement, root: Root
  const button = (text: string) => Array.from(host.querySelectorAll('button')).find(item => item.textContent === text)!
  const click = async (text: string) => act(async () => button(text).click())
  const confirm = async () => act(async () => host.querySelector<HTMLButtonElement>('[role="dialog"] .button.danger')!.click())
  const fill = async (label: string, value: string) => act(async () => {
    const input = host.querySelector<HTMLInputElement>(`[aria-label="${label}"]`)!
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, value)
    input.dispatchEvent(new Event('input', { bubbles: true }))
  })
  beforeEach(() => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
    vi.resetAllMocks()
    client.getRuntimePinConfig.mockResolvedValue(report())
    client.setRuntimePinConfig.mockImplementation(async (effective: PinConfig) => {
      const applied = { ...report(), overridePresent: true, effective }
      useDeviceStore.setState({ reportedPinConfig: applied }); return applied
    })
    useDeviceStore.getState().resetDevice()
    useDeviceStore.setState({ connection: 'connected', deviceId: 'fixture-A', boardId: 'tlsr8359_hsel', reportedPinConfig: report(),
      settingsCapabilities: { ...emptyDeviceSettingsCapabilities, runtimePinConfig: true } })
    host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  })
  afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.unstubAllGlobals() })
  const mount = async () => act(async () => root.render(<RuntimePinSettings />))

  it('uses auto-read state, with no tab-entry request or dedicated read button', async () => {
    await mount()
    expect(client.getRuntimePinConfig).not.toHaveBeenCalled()
    expect(host.querySelector<HTMLInputElement>('[aria-label="屏幕引脚 MOSI"]')!.value).toBe('0')
    expect([...host.querySelectorAll('h2')].map(n => n.textContent)).toEqual(['屏幕引脚', 'LED 引脚'])
    expect(host.querySelector('[aria-label="屏幕引脚 BUSY低有效"]')).toBeNull()
    for (const label of ['MOSI', 'SCLK', 'CS', 'DC', 'RESET', 'BUSY']) {
      const field = host.querySelector(`[aria-label="屏幕引脚 ${label}"]`)!.closest('.pin-field')!
      expect(field.querySelector('input[type="checkbox"], select')).toBeNull()
    }
    expect(host.querySelector('[aria-label="屏幕引脚 供电 EN低有效"]')).not.toBeNull()
    expect(host.querySelectorAll('.pin-field')).toHaveLength(10)
    expect(host.querySelectorAll('.pin-polarity')).toHaveLength(4)
    expect(host.querySelector('[aria-label="BUSY 有效电平"]')).toBeNull()
    expect(host.textContent).not.toMatch(/读取设备|TXD|RXD/)
  })
  it('shows current read-only pins without presenting them as editable', async () => {
    useDeviceStore.setState({ reportedPinConfig: { ...report(), editableRoles: 0 } })
    await mount()
    expect(host.textContent).toContain('设备当前有效引脚（只读）')
    expect(host.querySelector<HTMLInputElement>('[aria-label="屏幕引脚 MOSI"]')!.value).toBe('0')
    expect([...host.querySelectorAll<HTMLInputElement>('input')].every(input => input.disabled)).toBe(true)
    expect(button('保存屏幕引脚').disabled).toBe(true)
    expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
  })
  it.each(['disconnected', 'unsupported', 'unread'])('shows ten placeholders while %s', async state => {
    useDeviceStore.setState(state === 'disconnected' ? { connection: 'idle' } : state === 'unsupported' ? { settingsCapabilities: emptyDeviceSettingsCapabilities } : { reportedPinConfig: null })
    await mount()
    const fields = [...host.querySelectorAll<HTMLInputElement>('input[type="number"]')]
    expect(fields).toHaveLength(10)
    expect(fields.every(field => field.placeholder === '--' && field.value === '' && field.disabled)).toBe(true)
    expect(button('保存屏幕引脚').disabled).toBe(true)
    expect(button('保存LED 引脚').disabled).toBe(true)
    expect(client.getRuntimePinConfig).not.toHaveBeenCalled()
  })
  it('saves screen only, preserving fresh LED and BUSY and the unsaved LED draft', async () => {
    await mount(); await fill('屏幕引脚 MOSI', '14'); await fill('LED 引脚 红', '16')
    const fresh = report(); fresh.effective.pins.red = 17; fresh.effective.activeLow &= ~32
    client.getRuntimePinConfig.mockImplementationOnce(async () => { useDeviceStore.setState({ reportedPinConfig: fresh }); return fresh })
    await click('保存屏幕引脚'); expect(client.setRuntimePinConfig).not.toHaveBeenCalled(); await confirm()
    expect(client.setRuntimePinConfig).toHaveBeenCalledExactlyOnceWith({ pins: { ...fresh.effective.pins, mosi: 14 }, activeLow: fresh.effective.activeLow })
    expect(host.querySelector<HTMLInputElement>('[aria-label="LED 引脚 红"]')!.value).toBe('16')
    expect(button('保存LED 引脚').disabled).toBe(false)
  })
  it('saves LED only, preserving fresh screen routing and BUSY', async () => {
    await mount(); await fill('LED 引脚 红', '16')
    const fresh = report(); fresh.effective.pins.mosi = 15; fresh.effective.activeLow &= ~32
    client.getRuntimePinConfig.mockResolvedValueOnce(fresh)
    await click('保存LED 引脚'); await confirm()
    expect(client.setRuntimePinConfig).toHaveBeenCalledExactlyOnceWith({ pins: { ...fresh.effective.pins, red: 16 }, activeLow: fresh.effective.activeLow })
  })
  it('never writes to a different device during confirmation', async () => {
    await mount(); await fill('屏幕引脚 MOSI', '14'); await click('保存屏幕引脚')
    await act(async () => useDeviceStore.setState({ deviceId: 'fixture-B', reportedPinConfig: null }))
    await confirm(); expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
  })
  it('refuses a device change during the read before SET', async () => {
    await mount(); await fill('屏幕引脚 MOSI', '14')
    client.getRuntimePinConfig.mockImplementationOnce(async () => { useDeviceStore.setState({ deviceId: 'fixture-B', reportedPinConfig: null }); return report() })
    await click('保存屏幕引脚'); await confirm(); expect(client.setRuntimePinConfig).not.toHaveBeenCalled()
  })
  it('does not replay an unconfirmed write until a new connection', async () => {
    await mount(); await fill('屏幕引脚 MOSI', '14')
    client.setRuntimePinConfig.mockRejectedValueOnce(new PinWriteUnconfirmedError())
    await click('保存屏幕引脚'); await confirm()
    expect(document.body.textContent).toContain('写入结果未确认')
    expect(button('保存屏幕引脚').disabled).toBe(true)
    await act(async () => useDeviceStore.setState({ reportedPinConfig: report() }))
    expect(button('保存屏幕引脚').disabled).toBe(true)
    expect(client.setRuntimePinConfig).toHaveBeenCalledTimes(1)
    await act(async () => useDeviceStore.setState({ connection: 'idle' }))
    expect([...host.querySelectorAll<HTMLInputElement>('input[type="number"]')].every(field => field.value === '')).toBe(true)
  })
})
