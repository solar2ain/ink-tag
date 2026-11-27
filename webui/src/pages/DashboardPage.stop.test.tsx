// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DashboardPage } from './DashboardPage'
import { usePhoneNfcUnavailable } from '../nfc/browserNdef'
import { setPreferences } from '../preferences'
import { useDeviceStore } from '../store/device'

const mock = vi.hoisted(() => ({ stop: vi.fn<() => Promise<void>>() }))
vi.mock('../protocol/client', () => ({ inktagClient: { stop: mock.stop } }))
vi.mock('../components/TemplatePreview', () => ({ TemplatePreview: () => null }))

let host: HTMLDivElement, root: Root
const pause = () => Array.from(host.querySelectorAll('button')).find(b => b.textContent === '暂停' || b.textContent === '停止中')!
const render = () => act(async () => root.render(<MemoryRouter><DashboardPage /></MemoryRouter>))
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  setPreferences({ language: 'zh-CN' })
  usePhoneNfcUnavailable.setState(false)
  mock.stop.mockReset().mockResolvedValue(undefined)
  useDeviceStore.getState().resetDevice()
  useDeviceStore.getState().clearLogs()
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount())
  setPreferences({ language: 'browser' })
  host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})

it.each(['idle', 'clock', 'calendar', 'picture', 'slideshow'] as const)(
  'can stop a connected device while browser action is %s', async action => {
    useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A', action })
    await render()
    expect(pause().disabled).toBe(false)
    await act(async () => pause().click())
    expect(mock.stop).toHaveBeenCalledOnce()
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'success' })
  })

it.each(['disconnected', 'ota'] as const)('does not issue display CANCEL during %s', async mode => {
  useDeviceStore.setState({ connection: mode === 'ota' ? 'connected' : 'idle', action: mode === 'ota' ? 'ota' : 'idle' })
  await render()
  expect(pause().disabled).toBe(true)
  await act(async () => pause().click())
  expect(mock.stop).not.toHaveBeenCalled()
})

it('sends once while waiting and remains available after confirmation', async () => {
  let finish!: () => void
  mock.stop.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve }))
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A', action: 'idle' })
  await render()
  await act(async () => { pause().click(); pause().click() })
  expect(mock.stop).toHaveBeenCalledOnce()
  expect(pause().disabled).toBe(true)
  await act(async () => finish())
  expect(pause().disabled).toBe(false)
})

it('holds phone reader mode beyond the success dialog and returns to Bluetooth controls', async () => {
  vi.useFakeTimers()
  vi.setSystemTime(1795276800000)
  const timezone = vi.spyOn(Date.prototype, 'getTimezoneOffset').mockReturnValue(-480)
  let finish!: () => void
  let scanSignal!: AbortSignal
  const scan = vi.fn(async ({ signal }: { signal: AbortSignal }) => { scanSignal = signal })
  const write = vi.fn<(...args: unknown[]) => Promise<void>>(
    () => new Promise<void>(resolve => { finish = resolve }),
  )
  vi.stubGlobal('isSecureContext', true)
  vi.stubGlobal('NDEFReader', class {
    scan(options: { signal: AbortSignal }) { return scan(options) }
    write(...args: unknown[]) { return write(...args) }
  })
  const originalModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', { configurable: true, value: vi.fn() })
  try {
    await render()
    const button = (text: string) => [...host.querySelectorAll('button')].find(item => item.textContent === text)!
    expect(button('蓝牙控制').getAttribute('aria-pressed')).toBe('true')
    await act(async () => button('NFC 控制').click())
    expect(button('NFC 控制').getAttribute('aria-pressed')).toBe('true')
    expect(host.querySelector('.quick-control .quiet-note')?.textContent).toBe('选择命令并将手机NFC贴近设备')
    expect(pause()).toBeUndefined()
    expect(host.querySelector('.nfc-commands')?.textContent).not.toMatch(/同步时间|单独校时|选择串口/)
    expect(button('发送指令').disabled).toBe(true)
    await act(async () => button('时钟').click())
    expect(host.querySelector('.confirmation-dialog')?.textContent).toContain('增加耗电')
    expect(scan).not.toHaveBeenCalled()
    expect(write).not.toHaveBeenCalled()
    await act(async () => button('取消').click())
    expect(host.querySelector('.confirmation-dialog')).toBeNull()
    expect(write).not.toHaveBeenCalled()
    await act(async () => button('时钟').click())
    vi.setSystemTime(1795276805000)
    await act(async () => button('仍然进入').click())
    expect(host.querySelector('dialog [data-placement="phone"]')).not.toBeNull()
    expect(write).toHaveBeenCalledWith({ records: [{ recordType: 'text', lang: 'en', encoding: 'utf-8', data: 'clock#1795276805,+480' }] },
      { overwrite: true, signal: scanSignal })
    expect(scan.mock.invocationCallOrder[0]).toBeLessThan(write.mock.invocationCallOrder[0])
    await act(async () => finish())
    expect(host.querySelector('dialog [data-placement]')).toBeNull()
    await act(async () => vi.advanceTimersByTimeAsync(1999))
    expect(scanSignal.aborted).toBe(false)
    expect(host.querySelector('dialog')?.textContent).toContain('请移开手机')
    expect(button('已移开')).toBeUndefined()
    await act(async () => vi.advanceTimersByTimeAsync(1))
    expect(scanSignal.aborted).toBe(false)
    expect(host.querySelector('dialog')).toBeNull()
    await act(async () => vi.advanceTimersByTimeAsync(8000))
    expect(scanSignal.aborted).toBe(true)
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({
      level: 'success', message: expect.stringContaining('指令已发送（Web NFC 已确认 NDEF 写入）'),
    })
    await act(async () => useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' }))
    expect(button('蓝牙控制').getAttribute('aria-pressed')).toBe('true')
    expect(pause().disabled).toBe(false)
    await act(async () => button('NFC 控制').click())
    expect(button('NFC 控制').getAttribute('aria-pressed')).toBe('true')
  } finally {
    timezone.mockRestore()
    vi.useRealTimers()
    if (originalModal) Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalModal)
    else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
  }
})

it('shows the instruction for the active quick control mode beside its heading', async () => {
  vi.stubGlobal('isSecureContext', true)
  const requestPort = vi.fn().mockRejectedValue(new DOMException('', 'NotFoundError'))
  Object.defineProperty(navigator, 'serial', { configurable: true, value: { requestPort } })
  const originalModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', { configurable: true, value: vi.fn() })
  try {
    await render()
    const button = (text: string) => [...host.querySelectorAll('button')].find(item => item.textContent === text)!
    const note = () => host.querySelector('.quick-control .section-heading .quiet-note')?.textContent
    expect(note()).toBe('连接设备蓝牙后使用')
    await act(async () => button('NFC 控制').click())
    expect(note()).toBe('连接PN532并放置好设备后使用')
    expect(host.querySelector('button[aria-label="NFC 控制帮助"]')).toBeNull()
    expect(host.querySelector('.nfc-commands .nfc-command-note')).toBeNull()
    await act(async () => button('时钟').click())
    expect(requestPort).not.toHaveBeenCalled()
    await act(async () => button('仍然进入').click())
    expect(requestPort).toHaveBeenCalledOnce()
    await act(async () => button('蓝牙控制').click())
    expect(note()).toBe('连接设备蓝牙后使用')
  } finally {
    Reflect.deleteProperty(navigator, 'serial')
    if (originalModal) Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalModal)
    else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
  }
})

it('omits NFC controls when the browser has no NFC transport', async () => {
  await render()
  expect(host.querySelector('.quick-control-mode')).toBeNull()
  expect(pause()).toBeDefined()
})

it.each(['NotSupportedError', 'NotReadableError'])('handles the phone reporting %s', async errorName => {
  vi.useFakeTimers()
  let scanSignal!: AbortSignal
  vi.stubGlobal('isSecureContext', true)
  vi.stubGlobal('NDEFReader', class {
    async scan({ signal }: { signal: AbortSignal }) { scanSignal = signal }
    async write() { throw new DOMException('', errorName) }
  })
  const originalModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', { configurable: true, value: vi.fn() })
  try {
    await render()
    const button = (text: string) => [...host.querySelectorAll('button')].find(item => item.textContent === text)
    await act(async () => button('NFC 控制')!.click())
    await act(async () => button('时钟')!.click())
    await act(async () => button('仍然进入')!.click())
    expect(scanSignal.aborted).toBe(true)
    await act(async () => vi.advanceTimersByTimeAsync(2200))
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({
      level: 'error', message: expect.stringContaining('[NFC · 手机]'),
    })
    expect(host.querySelector('dialog')?.textContent).toContain(errorName === 'NotSupportedError'
      ? '当前手机无法使用 Web NFC' : '请开启手机 NFC 后重试')
    await act(async () => button('关闭')!.click())
    expect(Boolean(button('NFC 控制'))).toBe(errorName === 'NotReadableError')
  } finally {
    vi.useRealTimers()
    if (originalModal) Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalModal)
    else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
  }
})

it('uses English throughout the phone NFC command flow', async () => {
  setPreferences({ language: 'en' })
  vi.useFakeTimers()
  vi.setSystemTime(1795276800000)
  const timezone = vi.spyOn(Date.prototype, 'getTimezoneOffset').mockReturnValue(300)
  const write = vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('isSecureContext', true)
  vi.stubGlobal('NDEFReader', class {
    write(message: unknown, options: unknown) { return write(message, options) }
  })
  const originalModal = Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, 'showModal')
  Object.defineProperty(HTMLDialogElement.prototype, 'showModal', { configurable: true, value: vi.fn() })
  try {
    await render()
    const button = (text: string) => [...host.querySelectorAll('button')].find(item => item.textContent === text)!
    await act(async () => button('NFC control').click())
    const section = host.querySelector('.quick-control')!
    expect(section.querySelector('.quiet-note')?.textContent).toBe('Choose a command, then hold your phone’s NFC area close to the device.')
    await act(async () => button('Clock').click())
    expect(section.textContent).toContain('Switch to the clock page?')
    expect(section.textContent).toContain('uses more battery power')
    expect(write).not.toHaveBeenCalled()
    await act(async () => button('Cancel').click())
    await act(async () => button('Calendar').click())
    expect(write).toHaveBeenCalledExactlyOnceWith({ records: [{ recordType: 'text', lang: 'en', encoding: 'utf-8', data: 'calendar#1795276800,-300' }] },
      { overwrite: true, signal: expect.any(AbortSignal) })
    expect(section.querySelector('dialog')?.textContent).toContain('Command sent. Move your phone away and check the device.')
    expect(section.textContent).not.toMatch(/\p{Script=Han}/u)
    await act(async () => button('Close').click())
    await act(async () => button('Custom').click())
    const field = section.querySelector<HTMLInputElement>('input[aria-label="Image alias or command"]')!
    await act(async () => {
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(field, 'calendar')
      field.dispatchEvent(new Event('input', { bubbles: true }))
    })
    await act(async () => button('Send command').click())
    expect(write).toHaveBeenLastCalledWith({ records: [{ recordType: 'text', lang: 'en', encoding: 'utf-8', data: 'calendar' }] },
      { overwrite: true, signal: expect.any(AbortSignal) })
  } finally {
    timezone.mockRestore()
    vi.useRealTimers()
    if (originalModal) Object.defineProperty(HTMLDialogElement.prototype, 'showModal', originalModal)
    else Reflect.deleteProperty(HTMLDialogElement.prototype, 'showModal')
  }
})

it('reports rejection and permits a manual retry without claiming success', async () => {
  mock.stop.mockRejectedValueOnce(new Error('设备拒绝取消'))
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' })
  await render()
  await act(async () => pause().click())
  expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'error', message: '停止失败：设备拒绝取消' })
  expect(pause().disabled).toBe(false)
  await act(async () => pause().click())
  expect(mock.stop).toHaveBeenCalledTimes(2)
})

it('does not report the previous device result on a newly selected device', async () => {
  let finish!: () => void
  mock.stop.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve }))
  useDeviceStore.setState({ connection: 'connected', deviceId: 'tag-A' })
  await render()
  await act(async () => pause().click())
  await act(async () => { useDeviceStore.setState({ deviceId: 'tag-B' }); finish() })
  expect(useDeviceStore.getState().logs).toHaveLength(0)
})
