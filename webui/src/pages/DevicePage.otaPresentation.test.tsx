// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DevicePage } from './DevicePage'
import { useDeviceStore } from '../store/device'
import { useFirmwareUpdateStore } from '../store/firmwareUpdate'
import { otaPresentationFixtures } from './fixtures/otaPresentation'
import { PROTOCOL_V2_VERSION } from '../protocol/generatedCommandsV2'

const client = vi.hoisted(() => ({ getOtaRecoveryType: vi.fn(), getExpectedRecoveryIdentity: vi.fn(), getRecoverySafeResumeSupported: vi.fn(), selectRecoveryForOta: vi.fn() }))
const ota = vi.hoisted(() => ({ parseOtaPackage: vi.fn(), transferOta: vi.fn(), enterFixedRecovery: vi.fn(), inspectNordicRecovery: vi.fn(), inspectTelinkRecovery: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
vi.mock('../protocol/ota', () => ota)
let host: HTMLDivElement, root: Root
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.resetAllMocks()
  useDeviceStore.getState().resetDevice(); useFirmwareUpdateStore.getState().clear()
  client.getExpectedRecoveryIdentity.mockReturnValue(null)
  client.getOtaRecoveryType.mockReturnValue(null)
  client.getRecoverySafeResumeSupported.mockReturnValue(false)
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount()); host.remove()
  useDeviceStore.getState().resetDevice(); useFirmwareUpdateStore.getState().clear(); vi.unstubAllGlobals()
})

it.each(otaPresentationFixtures)('renders the same real package card for $id without starting an operation', async ({ pkg, boardId, platform, appBytes }) => {
  useDeviceStore.setState({ connection: 'connected', deviceId: 'offline-display-fixture', boardId,
    capabilities: { ...useDeviceStore.getState().capabilities, ota: pkg.type } })
  ota.parseOtaPackage.mockResolvedValue(pkg)
  await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=ota']}><DevicePage /></MemoryRouter>))
  expect(host.querySelector('.drop-zone')?.textContent).toContain(platform)
  const input = host.querySelector('input[type=file]')!
  expect(input.getAttribute('accept')).toBe('.zip,.img')
  Object.defineProperty(input, 'files', { value: [new File(['display sample'], pkg.payload.fileName)] })
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  const card = host.querySelector('[aria-label="升级包信息"]')!
  expect(card.querySelector('.ota-platform-label')?.textContent).toBe(platform)
  expect(card.querySelector('dl')?.textContent).toContain(`${appBytes.toLocaleString('zh-CN')} B`)
  expect(card.querySelector('dl')?.textContent).toContain(pkg.payload.fileName)
  expect(card.querySelector('details')?.hasAttribute('open')).toBe(false)
  expect(card.querySelector('details p')?.textContent).toBe(pkg.summary)
  expect(host.querySelector('.drop-zone')?.textContent).not.toMatch(/GOODIX|ONMICRO|DIALOG|签名初始化/)
  expect(ota.parseOtaPackage).toHaveBeenCalledExactlyOnceWith(pkg.type, expect.any(File), boardId)
  expect(ota.transferOta).not.toHaveBeenCalled()
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
  expect(ota.inspectNordicRecovery).not.toHaveBeenCalled()
  expect(ota.inspectTelinkRecovery).not.toHaveBeenCalled()
})
it.each(['goodix', 'onmicro', 'telink', 'nordic', 'dialog'] as const)('keeps unsupported Recovery exit visible and disabled for %s', async type => {
  client.getOtaRecoveryType.mockReturnValue(type)
  useDeviceStore.setState({ connection: 'connected', firmwareVersion: 'Recovery', boardId: '', capabilities: { ...useDeviceStore.getState().capabilities, ota: type } })
  await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=ota']}><DevicePage /></MemoryRouter>))
  const buttons = [...host.querySelectorAll('button')].map(b => b.textContent)
  const exit = [...host.querySelectorAll('button')].find(b => b.textContent === '退出 Recovery')!
  expect(exit.disabled).toBe(true)
  expect(buttons.includes('读取 Recovery 状态')).toBe(type === 'nordic' || type === 'telink')
  expect(ota.transferOta).not.toHaveBeenCalled()
})
async function chooseSample(sample: typeof otaPresentationFixtures[number]) {
  useDeviceStore.setState({ connection: 'connected', deviceId: 'offline-display-fixture', boardId: sample.boardId,
    capabilities: { ...useDeviceStore.getState().capabilities, ota: sample.pkg.type } })
  ota.parseOtaPackage.mockResolvedValue(sample.pkg)
  await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=ota']}><DevicePage /></MemoryRouter>))
  const input = host.querySelector('input[type=file]')!
  Object.defineProperty(input, 'files', { value: [new File(['display sample'], sample.pkg.payload.fileName)] })
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  return [...host.querySelectorAll('button')].find(b => b.textContent === '开始 OTA')!
}
it.each(otaPresentationFixtures)('shows a red failure after 100% for $id without retrying or changing package metadata', async sample => {
  const start = await chooseSample(sample)
  ota.transferOta.mockImplementationOnce(async (_pkg, progress) => {
    progress({ percent: 100, message: '传输完成，正在核验应用' })
    throw new Error('应用核验失败')
  })
  await act(async () => start.click())
  expect(host.querySelector('.ota-failure[role=alert]')?.textContent).toContain('应用核验失败')
  expect(host.querySelector('.ota-progress.failed')?.textContent).toContain('100%')
  expect(host.querySelector('.drop-zone.failed')).not.toBeNull()
  expect(host.textContent).not.toContain('OTA 已完成')
  expect(host.querySelector('.ota-package-card dl')?.textContent).toContain(sample.pkg.payload.fileName)
  expect(ota.transferOta).toHaveBeenCalledExactlyOnceWith(sample.pkg, expect.any(Function))
  expect(ota.enterFixedRecovery).not.toHaveBeenCalled()
})
it.each(otaPresentationFixtures)('shows progress separately from completion for $id with the same single start action', async sample => {
  const start = await chooseSample(sample)
  let finish!: () => void
  ota.transferOta.mockImplementationOnce(async (_pkg, progress) => {
    progress({ percent: 47, message: '正在传输应用' })
    await new Promise<void>(resolve => { finish = resolve })
  })
  await act(async () => start.click())
  expect(host.querySelector('.ota-progress')?.getAttribute('aria-valuenow')).toBe('47')
  expect(host.querySelector('.drop-zone strong')?.textContent).toBe('正在更新固件')
  expect(host.querySelector('.ota-package-note')?.textContent).toContain('这不代表更新成功')
  expect(start.disabled).toBe(true)
  expect(ota.transferOta).toHaveBeenCalledExactlyOnceWith(sample.pkg, expect.any(Function))
  await act(async () => finish())
})
it('keeps the verified package after TLSR discovery loss and offers an explicit Recovery selection', async () => {
  const sample = otaPresentationFixtures.find(item => item.pkg.type === 'telink')!
  const start = await chooseSample(sample)
  const handoff = { deviceId: 'offline-display-fixture', recoveryName: 'IKT_TL_335C_OTA' }
  ota.transferOta.mockImplementationOnce(async () => {
    client.getExpectedRecoveryIdentity.mockReturnValue(handoff)
    throw Object.assign(new Error('固件尚未传输。'), { code: 'RECOVERY_SELECT_REQUIRED' })
  })
  await act(async () => start.click())
  expect(host.querySelector('.ota-failure')?.textContent).toContain('需要重新连接 Recovery')
  expect(host.querySelector('.ota-failure')?.textContent).not.toContain('重新选择 OTA 应用文件')
  const select = [...host.querySelectorAll('button')].find(b => b.textContent === '重新选择 Recovery')!
  client.selectRecoveryForOta.mockImplementation(async () => {
    useDeviceStore.getState().setAction('ota')
    useDeviceStore.setState({ firmwareVersion: 'Recovery' })
    useDeviceStore.getState().setAction('idle')
  })
  await act(async () => select.click())
  expect(client.selectRecoveryForOta).toHaveBeenCalledExactlyOnceWith(handoff)
  expect(host.querySelector('.ota-package-card dl')?.textContent).toContain(sample.pkg.payload.fileName)
  expect(ota.transferOta).toHaveBeenCalledTimes(1)
  expect(ota.parseOtaPackage).toHaveBeenCalledTimes(1)
})

it('uses red alert styling for Recovery errors while keeping the exact entry action', async () => {
  useDeviceStore.setState({ connection: 'connected', deviceId: 'offline-display-fixture', boardId: 'nrf52832_priceetag', protocolVersion: PROTOCOL_V2_VERSION,
    capabilities: { ...useDeviceStore.getState().capabilities, ota: 'nordic' } })
  ota.enterFixedRecovery.mockRejectedValue(new Error('设备拒绝进入 Recovery'))
  await chooseSample(otaPresentationFixtures.find(sample => sample.id === 'nrf32')!)
  const button = (text: string) => [...host.querySelectorAll('button')].find(b => b.textContent === text)!
  await act(async () => button('进入 Recovery').click())
  await act(async () => button('确认进入').click())
  expect(document.querySelector('.library-message.error[role=alert]')?.textContent).toContain('设备拒绝进入 Recovery')
  expect(ota.enterFixedRecovery).toHaveBeenCalledExactlyOnceWith('offline-display-fixture')
  expect(ota.transferOta).not.toHaveBeenCalled()
})
