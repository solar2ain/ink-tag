// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DevicePage } from './DevicePage'
import { useDeviceStore } from '../store/device'
import { setPreferences } from '../preferences'
import { emptyAuxIoConfiguration } from '../protocol/auxIo'

const client = vi.hoisted(() => ({ getAdvertisingConfig: vi.fn(async () => null), requestNfcStatus: vi.fn(async () => {}), setNfcTapSettings: vi.fn(async () => {}) }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
vi.mock('../data/devicePreferences', async () => {
  const settings = await import('../protocol/deviceSettings')
  return {
    getDevicePreference: vi.fn(async () => ({ led: settings.defaultLedSettings(), nfcTap: settings.defaultNfcTapSettings(), buttonActions: settings.defaultButtonBehaviorSettings(), deviceInfoQrUrl: '' })),
    updateDevicePreference: vi.fn(async () => {}),
  }
})
let host: HTMLDivElement, root: Root
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.clearAllMocks()
  setPreferences({ language: 'zh-CN' })
  useDeviceStore.getState().resetDevice()
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount()); host.remove()
  useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})
const render = async (tab = 'behavior') => act(async () => {
  root.render(<MemoryRouter initialEntries={[`/device?tab=${tab}`]}><DevicePage /></MemoryRouter>)
})

it('marks unavailable extensions while retaining the qualified board button', async () => {
  useDeviceStore.setState({ connection: 'connected', capabilities: { ...useDeviceStore.getState().capabilities, button: true } })
  await render('overview')
  const value = (label: string) => Array.from(host.querySelectorAll('.settings-list > div')).find(node => node.querySelector('dt')?.textContent === label)?.querySelector('dd')?.textContent
  expect(value('按键')).toBe('板载按键')
  expect(value('蜂鸣器')).toBeUndefined()
  expect(value('NFC')).toBe('无板载')
})

it('links every reported button pin to the one shared firmware action table without inventing independent writes', async () => {
  const config = emptyAuxIoConfiguration()
  config.buttons[0] = { gpio: 17, activeLow: true, pull: 'up' }
  config.buttons[1] = { gpio: 18, activeLow: true, pull: 'up' }
  useDeviceStore.setState({ connection: 'connected', reportedAuxIoConfiguration: config, settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities, customButtonGpio: true } })
  await render()
  expect(host.querySelector('.behavior-button-pins')?.textContent).toContain('按键 1 · GPIO 17')
  expect(host.querySelector('.behavior-button-pins')?.textContent).toContain('按键 2 · GPIO 18')
  expect(host.querySelector('.behavior-button-pins a')?.getAttribute('href')).toBe('#/device?tab=hardware')
  expect(host.textContent).toContain('共用动作')
  expect(host.querySelectorAll('.button-gesture-list')).toHaveLength(1)
  expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})

it('omits local-plan controls and nested section headings while keeping the URL above its actions', async () => {
  useDeviceStore.setState({ connection: 'connected', deviceId: 'local-fixture' })
  await render()
  expect(host.querySelector('.behavior-settings .panel-title button')).toBeNull()
  expect(host.querySelector('.behavior-button-pins strong')).toBeNull()
  expect(host.querySelector('.led-heading small, .behavior-section-heading small')).toBeNull()
  expect(host.textContent).not.toContain('保存到本机')
  expect(host.textContent).not.toContain('载入本机')
  expect(client.setNfcTapSettings).not.toHaveBeenCalled()
  const qr = host.querySelector('.device-info-qr-grid')!
  expect(qr.nextElementSibling?.classList.contains('behavior-qr-actions')).toBe(true)
})

it('places missing-device capability explanations in a bubble outside section headings', async () => {
  useDeviceStore.setState({ connection: 'connected', deviceId: 'no-extras' })
  await render()
  const led = host.querySelector('.led-heading')!.closest('.behavior-block')!
  expect(led.querySelector('.behavior-unavailable')?.textContent).toContain('未提供状态灯')
  expect(led.querySelector('.led-heading .behavior-unavailable')).toBeNull()
  expect(host.querySelectorAll('.behavior-unavailable')).toHaveLength(2)
  expect(host.querySelector('.advertising-settings .behavior-unavailable')?.textContent).toContain('当前固件不支持')
  expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})

it('keeps write buttons text-only and explains the selected LED policy below the choices', async () => {
  useDeviceStore.setState({ connection: 'connected', capabilities: { ...useDeviceStore.getState().capabilities, led: 'rgb' } })
  await render()
  expect(host.querySelector('.led-heading button svg, .behavior-section-heading button svg')).toBeNull()
  await act(async () => host.querySelector<HTMLInputElement>('input[name="led-policy"][value="off"]')!.click())
  const hint = host.querySelector('.behavior-policy-hint')!
  expect(hint.textContent).toBe('关闭所有状态灯')
  expect(hint.previousElementSibling?.classList.contains('policy-options')).toBe(true)
  expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})

it('ends each available section with a short explanation below its controls', async () => {
  const state = useDeviceStore.getState()
  useDeviceStore.setState({ connection: 'connected', activation: { phase: 'active', edition: 'advanced' },
    capabilities: { ...state.capabilities, led: 'rgb', nfc: 'tap', button: true }, nfcCapabilities: { flags: 0x41, commandMask: 1 << 4, maxNdefBytes: 192, maxCommandPayload: 8, maxUidBytes: 10, commandFormatVersion: 1 },
    settingsCapabilities: { ...state.settingsCapabilities, nfcActionMask: 3, buttonGestureMask: 7, buttonActionMask: 0x6ff } })
  await render()
  expect(host.querySelectorAll('.behavior-block')).toHaveLength(5)
  const blocks = [...host.querySelectorAll('.behavior-block:not(.advertising-settings)')]
  expect(blocks).toHaveLength(4)
  for (const block of blocks.filter(b => !b.classList.contains('nfc-usage-section'))) expect(block.lastElementChild?.classList.contains('behavior-section-note')).toBe(true)
  expect(blocks[1].lastElementChild?.textContent).toContain('二维码')
  expect(blocks[3].lastElementChild?.textContent).toContain('共用动作')
  expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})


it.each([false,true])('shows only NFC usage for a capable device, Pro=%s', async pro => {
 const state=useDeviceStore.getState()
 useDeviceStore.setState({connection:'connected',deviceId:'fake-nfc',activation:{phase:pro?'active':'inactive',edition:pro?'advanced':'base'},
  capabilities:{...state.capabilities,nfc:'tap'},settingsCapabilities:{...state.settingsCapabilities,nfcActionMask:3},
  nfcCapabilities:{flags:0x41,commandMask:(1<<4)|(1<<7),maxNdefBytes:61,maxCommandPayload:8,maxUidBytes:10,commandFormatVersion:1}})
 await render()
 const section=host.querySelector('.nfc-usage-section')!
 expect(section).not.toBeNull()
 expect(section.querySelector('.behavior-section-heading > .nfc-capability-label')?.textContent).toBe('设备具有 NFC 功能')
 expect(section.querySelector('button,input,select')).toBeNull()
 expect(section.textContent).toContain('打开设备链接')
 expect(section.textContent).toContain('NFC Tools')
 expect([...section.querySelectorAll('code')].map(n=>n.textContent)).toEqual(['CLOCK','SLOT-1'])
 expect(section.textContent).not.toContain('关闭 NFC')
 expect(host.querySelector('input[name="nfc-mode"]')).toBeNull()
 expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})
it.each(['offline','absent'])('shows the NFC capability beside the heading without controls or unavailable help when %s', async mode => {
 const state=useDeviceStore.getState()
 useDeviceStore.setState(mode==='offline'?{connection:'idle',capabilities:{...state.capabilities,nfc:'tap'}}:{connection:'connected',deviceId:'fake-none'})
 await render()
 const section=host.querySelector('.nfc-usage-section')!
 expect(section.querySelector('.behavior-section-heading > .nfc-capability-label')?.textContent).toBe(mode==='offline'?'未连接设备':'设备无 NFC 功能')
 expect(section.querySelector('button,input,select,textarea')).toBeNull()
 expect(section.textContent).not.toContain('NFC Tools')
 expect(section.textContent).not.toContain('打开设备链接')
 expect(client.requestNfcStatus).not.toHaveBeenCalled()
 expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})
it('does not teach unsupported phone writes and distinguishes unknown capability readback',async()=>{
 const state=useDeviceStore.getState()
 useDeviceStore.setState({connection:'connected',deviceId:'fake-only',capabilities:{...state.capabilities,nfc:'tap'}})
 await render()
 expect(host.querySelector('.nfc-usage-section')?.textContent).toContain('尚未读到 NFC 命令能力')
 await act(async()=>useDeviceStore.setState({nfcCapabilities:{flags:1,commandMask:0,maxNdefBytes:61,maxCommandPayload:0,maxUidBytes:10,commandFormatVersion:1}}))
 expect(host.querySelector('.nfc-usage-section')?.textContent).toContain('当前固件未启用手机文本命令')
 expect(host.querySelector('.nfc-usage-section')?.textContent).not.toContain('NFC Tools')
 expect(client.setNfcTapSettings).not.toHaveBeenCalled()
})
