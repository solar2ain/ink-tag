// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { beforeEach, afterEach, expect, it, vi } from 'vitest'
import { AuxIoSettings } from './AuxIoSettings'
import { useDeviceStore } from '../store/device'
import { emptyDeviceSettingsCapabilities } from '../protocol/deviceSettings'
import { emptyAuxIoConfiguration } from '../protocol/auxIo'
import { PinWriteUnconfirmedError } from '../protocol/pinConfig'
const client = vi.hoisted(() => ({ setAuxIoConfiguration: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
let host: HTMLDivElement, root: Root
const button = (name: string) => [...host.querySelectorAll<HTMLButtonElement>('button')].find(b => b.textContent === name)!
const click = async (name: string) => act(async () => button(name).click())
const fill = async (label: string, value: string) => act(async () => {
  const input = host.querySelector<HTMLInputElement>(`[aria-label="${label}"]`)!
  Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, value)
  input.dispatchEvent(new Event('input', { bubbles: true }))
})
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.resetAllMocks()
  const config = emptyAuxIoConfiguration(); config.buttons[0] = { gpio: 17, activeLow: true, pull: 'up' }
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', deviceId: 'nrf-fixture', reportedAuxIoConfiguration: config,
    settingsCapabilities: { ...emptyDeviceSettingsCapabilities, customButtonGpio: true, reservedIo: true } })
  client.setAuxIoConfiguration.mockImplementation(async value => value)
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<AuxIoSettings />))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.unstubAllGlobals() })
it('reserves two pin fields and preserves the reported built-in default', async () => {
  expect(host.querySelector<HTMLInputElement>('[aria-label="按键 1 GPIO"]')!.value).toBe('17')
  expect(host.querySelector<HTMLInputElement>('[aria-label="按键 2 GPIO"]')!.value).toBe('')
  expect(host.querySelector('[aria-label="按键 3 GPIO"]')).toBeNull()
  expect(button('添加按键')).toBeUndefined()
  await fill('按键 2 GPIO', '18')
  expect(host.querySelector<HTMLSelectElement>('[aria-label="按键 2 有效电平"]')!.value).toBe('low')
  expect(host.querySelector<HTMLSelectElement>('[aria-label="按键 2 上下拉"]')!.value).toBe('up')
  expect(host.textContent).not.toContain('读取设备')
})
it('clears a fixed pin without compacting slots or changing the wire shape', async () => {
  await fill('按键 2 GPIO', '18')
  await fill('按键 1 GPIO', '')
  expect(host.querySelector<HTMLInputElement>('[aria-label="按键 1 GPIO"]')!.value).toBe('')
  expect(host.querySelector<HTMLInputElement>('[aria-label="按键 2 GPIO"]')!.value).toBe('18')
  await click('保存按键配置'); await act(async () => host.querySelector<HTMLButtonElement>('[role="dialog"] .button.danger')!.click())
  expect(client.setAuxIoConfiguration.mock.calls[0][0].buttons.map((b: { gpio: number }) => b.gpio)).toEqual([255, 18, 255])
})
it('hides buzzer controls and preserves hidden legacy fields on a button-only save', async () => {
  const config = emptyAuxIoConfiguration()
  config.reserved = [3, 20, 255, 208, 7]
  await act(async () => useDeviceStore.setState({ reportedAuxIoConfiguration: config }))
  expect(host.textContent).not.toContain('蜂鸣器')
  expect(host.querySelector('[aria-label="蜂鸣器驱动方式"]')).toBeNull()
  await fill('按键 1 GPIO', '17')
  await click('保存按键配置')
  await act(async () => host.querySelector<HTMLButtonElement>('[role="dialog"] .button.danger')!.click())
  expect(client.setAuxIoConfiguration).toHaveBeenCalledTimes(1)
  expect(client.setAuxIoConfiguration.mock.calls[0][0]).toMatchObject({
    reserved: [3, 20, 255, 208, 7],
  })
})

it('preserves an already reported third wire slot while editing the two visible pins', async () => {
  const config = emptyAuxIoConfiguration()
  config.buttons[2] = { gpio: 22, activeLow: true, pull: 'up' }
  await act(async () => useDeviceStore.setState({ reportedAuxIoConfiguration: config }))
  await fill('按键 1 GPIO', '17')
  await click('保存按键配置')
  await act(async () => host.querySelector<HTMLButtonElement>('[role="dialog"] .button.danger')!.click())
  expect(client.setAuxIoConfiguration.mock.calls[0][0].buttons[2]).toEqual(config.buttons[2])
})



it('shows PIN_CONFIG-only built-in slot1 read-only, without expansion-write permission', async () => {
  const config=emptyAuxIoConfiguration();config.buttons[0]={gpio:2,activeLow:true,pull:'up'}
  await act(async () => useDeviceStore.setState({ reportedAuxIoConfiguration:config,
    settingsCapabilities:{...emptyDeviceSettingsCapabilities,runtimePinConfig:true} }))
  expect(host.querySelector<HTMLInputElement>('[aria-label="按键 1 GPIO"]')!.value).toBe('2')
  expect(host.querySelector<HTMLInputElement>('[aria-label="按键 2 GPIO"]')!.disabled).toBe(true)
  expect([...host.querySelectorAll<HTMLInputElement|HTMLSelectElement>('input,select')].every(i=>i.disabled)).toBe(true)
  await click('保存按键配置')
  expect(button('保存按键配置').disabled).toBe(true)
  expect(host.textContent).toContain('暂未支持')
  expect(client.setAuxIoConfiguration).not.toHaveBeenCalled()
})
it('keeps the extension introduction short without GPIO or PWM explanations', () => {
  expect(host.querySelector('.panel-title p')!.textContent).toBe('设备读回值；修改后保存到设备。')
  expect(host.querySelector('.aux-button-help a')?.getAttribute('href')).toBe('#/device?tab=behavior')
  expect(host.textContent).not.toMatch(/自选 GPIO|默认关闭；|最多三个实体按键|尚未读回扩展 GPIO/)
})
it('locks an unconfirmed write and never retries it automatically', async () => {
  await fill('按键 2 GPIO', '18')
  client.setAuxIoConfiguration.mockRejectedValueOnce(new PinWriteUnconfirmedError())
  await click('保存按键配置'); await act(async () => host.querySelector<HTMLButtonElement>('[role="dialog"] .button.danger')!.click())
  expect(button('保存按键配置').disabled).toBe(true)
  expect(client.setAuxIoConfiguration).toHaveBeenCalledTimes(1)
  expect(document.body.textContent).toContain('写入结果未确认')
})
it('retains the connection-scoped safety lock after page remount', async () => {
  await act(async () => useDeviceStore.setState({ ioWriteUnconfirmed: true }))
  await act(async () => { root.unmount(); root = createRoot(host); root.render(<AuxIoSettings />) })
  expect(button('保存按键配置').disabled).toBe(true)
  expect(document.body.textContent).toContain('上次引脚写入结果未确认')
  expect(client.setAuxIoConfiguration).not.toHaveBeenCalled()
})

it('keeps offline pin placeholders read-only, like the LED pin controls', async () => {
  await act(async () => useDeviceStore.setState({ connection: 'idle', reportedAuxIoConfiguration: null }))
  for (const label of ['按键 1 GPIO', '按键 2 GPIO']) {
    const input = host.querySelector<HTMLInputElement>(`[aria-label="${label}"]`)!
    expect(input.value).toBe('')
    expect(input.disabled).toBe(true)
  }
  expect(client.setAuxIoConfiguration).not.toHaveBeenCalled()
})

it('does not borrow buzzer write permission for readonly buttons', async () => {
  await act(async () => useDeviceStore.setState({ settingsCapabilities: { ...emptyDeviceSettingsCapabilities, reservedIo: true } }))
  expect(host.querySelector<HTMLSelectElement>('[aria-label="按键 1 有效电平"]')!.disabled).toBe(true)
  expect(button('保存按键配置').disabled).toBe(true)
  expect(host.textContent).not.toContain('蜂鸣器')
})
