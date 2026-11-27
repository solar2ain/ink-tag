// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { DevicePage } from './DevicePage'
import { PROTOCOL_V2_VERSION } from '../protocol/generatedCommandsV2'

const client = vi.hoisted(() => ({ getOtaRecoveryType: vi.fn(), getRecoverySafeResumeSupported: vi.fn(), getExpectedRecoveryIdentity: vi.fn() }))
const ota = vi.hoisted(() => ({ enterFixedRecovery: vi.fn(), parseOtaPackage: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
vi.mock('../protocol/ota', () => ota)
let host: HTMLDivElement, root: Root
const button = (label: string) => Array.from(host.querySelectorAll('button')).find(button => button.textContent === label)!
const chooseOtaFile = async (valid = true) => {
  if (valid) ota.parseOtaPackage.mockResolvedValueOnce({ type: 'nordic', summary: 'verified package', payload: {} })
  else ota.parseOtaPackage.mockRejectedValueOnce(new Error('signature rejected'))
  const input = host.querySelector('input[type=file]')!
  Object.defineProperty(input, 'files', { configurable: true, value: [new File(['signed'], 'app.zip')] })
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
}
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.clearAllMocks()
  client.getOtaRecoveryType.mockReturnValue('nordic'); client.getRecoverySafeResumeSupported.mockReturnValue(false)
  client.getExpectedRecoveryIdentity.mockReturnValue(null)
  useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({ connection: 'connected', deviceId: 'exact-grant', boardId: 'nrf52832_priceetag', protocolVersion: PROTOCOL_V2_VERSION,
    capabilities: { ...useDeviceStore.getState().capabilities, ota: 'nordic' } })
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=ota']}><DevicePage /></MemoryRouter>))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })
it.each(['nordic', 'dialog', 'goodix', 'telink', 'onmicro'])('uses the exact queried %s support for its notice and button', async type => {
  client.getOtaRecoveryType.mockReturnValue(type)
  await act(async () => useDeviceStore.setState({ firmwareVersion: 'Recovery' }))
  expect(host.textContent).toContain('不支持安全退出')
  expect(button('退出 Recovery').disabled).toBe(true)
  client.getRecoverySafeResumeSupported.mockReturnValue(true)
  await act(async () => useDeviceStore.setState({ deviceName: 'verified-recovery' }))
  expect(host.textContent).toContain('可更新固件或安全返回原应用')
  expect(host.textContent).not.toContain('不支持安全退出')
  expect(button('退出 Recovery').disabled).toBe(false)
  expect(button('进入 Recovery')).toBeUndefined()
})
it('requires explicit confirmation, with cancel causing zero entry calls', async () => {
  await chooseOtaFile()
  await act(async () => button('进入 Recovery').click())
  const dialog = host.querySelector('[role=dialog]')
  expect(dialog?.textContent).toContain('不发送升级包或清除用户数据')
  expect(dialog?.textContent).toContain('Recovery 模式下 MCU 不会休眠，十分耗电，请不要长期停留在 Recovery 模式。')
  expect(dialog?.textContent).not.toContain('安全退出是否可用由已安装的 Recovery 决定')
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
  await act(async () => button('取消').click())
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
  await act(async () => button('进入 Recovery').click())
  await act(async () => button('确认进入').click())
  expect(ota.enterFixedRecovery).toHaveBeenCalledExactlyOnceWith('exact-grant')
})
it('does not show firmware transfer progress during Recovery entry', async () => {
  await chooseOtaFile()
  let finish!: () => void
  ota.enterFixedRecovery.mockImplementationOnce(async () => {
    useDeviceStore.getState().setAction('ota', 0, '正在进入 Recovery')
    await new Promise<void>(resolve => { finish = resolve })
    useDeviceStore.getState().setAction('idle')
  })
  await act(async () => button('进入 Recovery').click())
  expect(host.querySelector('.drop-zone.working')).toBeNull()
  await act(async () => button('确认进入').click())
  expect(host.textContent).toContain('正在进入')
  expect(host.textContent).not.toContain('正在更新固件')
  expect(host.querySelector('.drop-zone.working')).toBeNull()
  expect(host.querySelector('[role=progressbar]')).toBeNull()
  await act(async () => finish())
})
it('binds the target selected before confirmation rather than a newly connected device', async () => {
  await chooseOtaFile()
  await act(async () => button('进入 Recovery').click())
  await act(async () => useDeviceStore.setState({ deviceId: 'another-grant' }))
  await act(async () => button('确认进入').click())
  expect(ota.enterFixedRecovery).toHaveBeenCalledExactlyOnceWith('exact-grant')
})
it('allows connected qualified Recovery entry without a file while retaining offline and busy guards', async () => {
  expect(button('进入 Recovery').disabled).toBe(false)
  expect(button('开始 OTA').disabled).toBe(true)
  await chooseOtaFile()
  expect(button('进入 Recovery').disabled).toBe(false)
  await act(async () => useDeviceStore.setState({ action: 'picture' }))
  expect(button('进入 Recovery').disabled).toBe(true)
  await act(async () => useDeviceStore.setState({ connection: 'idle' }))
  expect(button('进入 Recovery').disabled).toBe(true)
  await act(async () => useDeviceStore.setState({ connection: 'connected', protocolVersion: 1 }))
  expect(button('进入 Recovery').disabled).toBe(true)
  await act(async () => useDeviceStore.setState({ protocolVersion: 2 }))
  expect(button('进入 Recovery').disabled).toBe(true)
})

it('orders the Recovery notice, both actions and file selection without a duplicate entry button', async () => {
  expect(host.querySelector('.recovery-notice')).toBeNull()
  client.getRecoverySafeResumeSupported.mockReturnValue(true)
  await act(async () => useDeviceStore.setState({ firmwareVersion: 'Recovery' }))
  const notice = host.querySelector('.recovery-notice')!
  const actions = host.querySelector('.ota-file-actions')!
  expect(notice.nextElementSibling).toBe(actions)
  expect(actions.nextElementSibling?.classList.contains('drop-zone')).toBe(true)
  expect([...actions.querySelectorAll('button')].map(b => b.textContent)).toEqual(['退出 Recovery', '开始 OTA'])
  expect(button('退出 Recovery').disabled).toBe(false)
  expect(button('开始 OTA').disabled).toBe(true)
  expect(button('进入 Recovery')).toBeUndefined()
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
})

it('does not show a Recovery notice for an App or an unconfirmed handoff', async () => {
  client.getExpectedRecoveryIdentity.mockReturnValue({ deviceId: 'exact-grant', recoveryName: 'IKT_TL_80FA_OTA' })
  await act(async () => useDeviceStore.setState({ firmwareVersion: '0.2.0' }))
  expect(host.querySelector('.recovery-notice')).toBeNull()
  expect([...host.querySelectorAll('.ota-file-actions button')].map(b => b.textContent)).toEqual(['进入 Recovery', '开始 OTA'])
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
})

it('rejects the firmware for OTA without disabling independent Recovery entry', async () => {
  await chooseOtaFile(false)
  expect([...host.querySelectorAll('.ota-file-actions button')].map(b => b.textContent)).toEqual(['进入 Recovery', '开始 OTA'])
  expect(button('进入 Recovery').disabled).toBe(false)
  expect(button('开始 OTA').disabled).toBe(true)
  expect(host.querySelector('.ota-failure')?.textContent).toContain('signature rejected')
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
})

it('requires confirmation for entry without loading or sending a firmware file', async () => {
  await act(async () => button('进入 Recovery').click())
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
  expect(button('开始 OTA').disabled).toBe(true)
  await act(async () => button('确认进入').click())
  expect(ota.enterFixedRecovery).toHaveBeenCalledExactlyOnceWith('exact-grant')
  expect(ota.parseOtaPackage).not.toHaveBeenCalled()
})
