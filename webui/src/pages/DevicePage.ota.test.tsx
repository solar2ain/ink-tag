// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { MemoryRouter } from 'react-router-dom'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { useDeviceStore } from '../store/device'
import { DevicePage } from './DevicePage'
import { useFirmwareUpdateStore } from '../store/firmwareUpdate'
import { renderToStaticMarkup } from 'react-dom/server'
import { FirmwareUpdateDialog } from '../components/FirmwareUpdateDialog'
import { webcrypto } from 'node:crypto'

const ota = vi.hoisted(() => ({ parseOtaPackage: vi.fn(), transferOta: vi.fn() }))
const online = vi.hoisted(() => ({ download: vi.fn() }))
vi.mock('../protocol/firmwareUpdate', async (original) => ({
  ...await original<typeof import('../protocol/firmwareUpdate')>(), downloadFirmwareFile: online.download,
}))
vi.mock('../protocol/ota', () => ota)
vi.mock('../protocol/client', () => ({ inktagClient: {
  getOtaRecoveryType: () => null, getExpectedRecoveryIdentity: () => null,
  getRecoverySafeResumeSupported: () => false,
} }))

let host: HTMLDivElement
let root: Root
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.clearAllMocks()
  useDeviceStore.getState().resetDevice()
  useFirmwareUpdateStore.getState().clear()
  useDeviceStore.setState({ connection: 'connected', deviceId: 'same-grant', boardId: 'nrf52832_priceetag',
    capabilities: { ...useDeviceStore.getState().capabilities, ota: 'nordic' } })
  ota.parseOtaPackage.mockResolvedValue({ type: 'nordic', summary: 'signed package', payload: {} })
  host = document.createElement('div'); document.body.append(host)
  root = createRoot(host)
  await act(async () => root.render(<MemoryRouter initialEntries={['/device?tab=ota']}><DevicePage /></MemoryRouter>))
  const input = host.querySelector('input[type=file]')!
  Object.defineProperty(input, 'files', { configurable: true, value: [new File(['signed'], 'test.zip')] })
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  expect(host.textContent).toContain('升级包已通过校验')
})
afterEach(async () => {
  await act(async () => root.unmount())
  host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})

it('places the explicit OTA action immediately above the file area, separate from Recovery', () => {
  const actions = host.querySelector('.ota-file-actions')!
  expect(actions.textContent).toContain('开始 OTA')
  expect(actions.nextElementSibling?.classList.contains('drop-zone')).toBe(true)
  expect(host.querySelector('.panel-title')?.textContent).not.toContain('开始 OTA')
  expect(host.querySelector('.ota-technical-details summary')?.textContent).toBe('详情')
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it.each(['available', 'checking', 'current', 'not-published', 'unavailable'] as const)('hides stale cloud %s information in Recovery while retaining local OTA selection', async status => {
  await act(async () => {
    useFirmwareUpdateStore.setState({ status, latestVersion: '0.2.1', error: 'test cloud failure', update: {
      currentVersion: '0.2.0', latestVersion: '0.2.1', releaseTag: 'v0.2.1', edition: 'basic',
      boardId: 'nrf52832_priceetag', boardName: 'nRF52832', artifactName: 'cloud.zip',
      downloadUrl: 'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1/cloud.zip', sha256: '0'.repeat(64), bytes: 100,
    } })
    useDeviceStore.setState({ firmwareVersion: 'Recovery' })
  })
  expect(host.querySelector('.firmware-update-notice')).toBeNull()
  expect(host.querySelector('.firmware-update-line')).toBeNull()
  expect(host.querySelector('.tab-update-dot')).toBeNull()
  expect(renderToStaticMarkup(<MemoryRouter><FirmwareUpdateDialog /></MemoryRouter>)).toBe('')
  expect(host.querySelector('input[type=file]')).not.toBeNull()
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it('keeps the package, progress and eventual failure across an active App/Recovery role change', async () => {
  let finish!: () => void
  ota.transferOta.mockImplementation(async (_pkg, progress) => {
    useDeviceStore.getState().setAction('ota', .35)
    progress({ percent: 35, message: '正在传输 35/100 bytes' })
    await new Promise<void>((resolve) => { finish = resolve })
    useDeviceStore.getState().setAction('idle', 0)
    throw new Error('Recovery refused')
  })
  await act(async () => Array.from(host.querySelectorAll('button')).find((button) => button.textContent === '开始 OTA')!.click())
  await act(async () => useDeviceStore.getState().resetDevice(true))
  expect(host.textContent).toContain('升级包已通过校验')
  expect(host.textContent).toContain('正在传输 35/100 bytes')
  expect(host.querySelector('.ota-progress')?.textContent).toContain('35%')
  await act(async () => {
    useDeviceStore.getState().setCapabilities({ ota: 'nordic' })
    useDeviceStore.getState().setIdentity({ boardId: 'nrf52832_priceetag' })
  })
  expect(host.textContent).toContain('正在传输 35/100 bytes')
  await act(async () => finish())
  expect(host.textContent).toContain('OTA 失败：Recovery refused')
  expect(host.querySelector('[role=alert]')?.textContent).toContain('OTA 更新失败')
  expect(host.querySelector('.drop-zone')?.classList.contains('ready')).toBe(false)
  expect(host.querySelector('.drop-zone .lucide-circle-check-big')).toBeNull()
  expect(host.querySelector('.ota-progress')?.classList.contains('failed')).toBe(true)
  expect(ota.transferOta).toHaveBeenCalledOnce()
})

it('shows a prominent failure even after all bytes transferred, never OTA success or an automatic retry', async () => {
  ota.transferOta.mockImplementation(async (_pkg, progress) => {
    progress({ percent: 100, message: '传输完成，正在核验应用' })
    throw new Error('nRF 应用核验不一致')
  })
  await act(async () => Array.from(host.querySelectorAll('button')).find((button) => button.textContent === '开始 OTA')!.click())
  const alert = host.querySelector('[role=alert]')!
  expect(alert.textContent).toContain('OTA 更新失败')
  expect(alert.textContent).toContain('nRF 应用核验不一致')
  expect(alert.textContent).toContain('请重新连接设备确认更新结果')
  expect(alert.textContent).toContain('若升级未完成，请重新选择 OTA 应用文件并重试')
  expect(host.querySelector('.drop-zone strong')?.textContent).toBe('OTA 更新失败')
  expect(host.querySelector('.drop-zone')?.classList.contains('ready')).toBe(false)
  expect(host.textContent).not.toContain('OTA 已完成')
  expect(host.querySelector('.ota-progress')?.textContent).toContain('100%')
  expect(ota.transferOta).toHaveBeenCalledOnce()
})

it('does not mark OTA complete at 100% until the transfer and final attestation resolve', async () => {
  let finish!: () => void
  ota.transferOta.mockImplementation(async (_pkg, progress) => {
    progress({ percent: 100, message: '传输完成，正在核验应用' })
    await new Promise<void>((resolve) => { finish = resolve })
    progress({ percent: 100, message: 'OTA 已完成；设备时间未校准，可稍后手动同步' })
  })
  await act(async () => Array.from(host.querySelectorAll('button')).find((button) => button.textContent === '开始 OTA')!.click())
  expect(host.querySelector('.drop-zone strong')?.textContent).toBe('正在更新固件')
  expect(host.querySelector('.drop-zone')?.classList.contains('ready')).toBe(false)
  expect(host.querySelector('.ota-package-note')?.textContent).toContain('这不代表更新成功')
  await act(async () => finish())
  expect(host.querySelector('.drop-zone strong')?.textContent).toBe('OTA 已完成')
  expect(host.textContent).toContain('设备时间未校准')
  expect(host.querySelector('[role=alert]')).toBeNull()
  expect(ota.transferOta).toHaveBeenCalledOnce()
})

it('highlights a rejected package and clears the error when a new package is chosen, without a write', async () => {
  const input = host.querySelector('input[type=file]')!
  ota.parseOtaPackage.mockRejectedValueOnce(new Error('签名校验失败'))
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  expect(host.querySelector('[role=alert]')?.textContent).toContain('升级包校验失败')
  expect(host.querySelector('[role=alert]')?.textContent).toContain('签名校验失败')
  expect(host.querySelector('[role=alert]')?.textContent).toContain('请选择与当前设备匹配的 OTA 应用文件，然后重新开始升级')
  expect(host.querySelector('.drop-zone strong')?.textContent).toBe('升级包校验失败')
  expect(host.textContent).not.toContain('升级包已通过校验')
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  expect(host.querySelector('[role=alert]')).toBeNull()
  expect(host.querySelector('.drop-zone strong')?.textContent).toBe('升级包已通过校验')
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it('clears an idle package when the board/type changes instead of applying it to another target', async () => {
  await act(async () => {
    useDeviceStore.getState().setCapabilities({ ota: 'onmicro' })
    useDeviceStore.getState().setIdentity({ boardId: 'om6626_zkc21b' })
  })
  expect(host.textContent).not.toContain('升级包已通过校验')
  expect(host.textContent).not.toContain('signed package')
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it.each(['package-error', 'role-change'])('permits explicit same-file selection after %s without starting OTA', async mode => {
  const input = host.querySelector<HTMLInputElement>('input[type=file]')!
  const file = new File(['signed'], 'same-package.zip')
  let selectedValue = 'C:\\fakepath\\same-package.zip'
  Object.defineProperty(input, 'value', { configurable: true, get: () => selectedValue, set: value => { selectedValue = value } })
  // Clearing a real file input also empties its live FileList. The handler
  // must capture the File before clearing the native selection.
  Object.defineProperty(input, 'files', { configurable: true, get: () => selectedValue ? [file] : [] })
  if (mode === 'package-error') ota.parseOtaPackage.mockRejectedValueOnce(new Error('original package error'))
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  expect(selectedValue).toBe('')
  if (mode === 'role-change') {
    await act(async () => useDeviceStore.getState().resetDevice(true))
    await act(async () => useDeviceStore.setState({ connection: 'connected', deviceId: 'same-grant', boardId: 'nrf52832_priceetag',
      capabilities: { ...useDeviceStore.getState().capabilities, ota: 'nordic' } }))
  }
  selectedValue = 'C:\\fakepath\\same-package.zip'
  await act(async () => input.dispatchEvent(new Event('change', { bubbles: true })))
  expect(selectedValue).toBe('')
  expect(ota.parseOtaPackage).toHaveBeenLastCalledWith('nordic', file, 'nrf52832_priceetag')
  expect(host.textContent).toContain('升级包已通过校验')
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it('shows the installed Pro build badge for an unactivated device without offering a BASIC download', async () => {
  await onlineChoice()
  await act(async () => useDeviceStore.setState({
    firmwareBuildEdition: 'pro', activation: { phase: 'inactive', edition: 'base' },
  }))
  expect(host.querySelector('.firmware-build-badge')?.textContent).toBe('Pro')
  expect(host.querySelector('.firmware-build-badge svg')).not.toBeNull()
  expect(host.querySelector('.firmware-update-notice')).toBeNull()
  expect(host.querySelector('.tab-update-dot')).toBeNull()
  expect(online.download).not.toHaveBeenCalled()
})

async function onlineChoice() {
  vi.stubGlobal('crypto', webcrypto)
  await act(async () => {
    useDeviceStore.setState({ firmwareVersion: '0.2.0.0', firmwareBuildEdition: 'basic' })
    useFirmwareUpdateStore.setState({ status: 'available', latestVersion: '0.2.1.0', update: {
      currentVersion: '0.2.0.0', latestVersion: '0.2.1.0', releaseTag: 'v0.2.1', edition: 'basic',
      boardId: 'nrf52832_priceetag', boardName: 'nRF52832', buildTarget: 'nrf52832_priceetag',
      artifactName: 'cloud.zip', downloadUrl: 'https://github.com/solar2ain/ink-tag/releases/download/v0.2.1/cloud.zip',
      bytes: 3, sha256: 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
    } })
  })
  const file = new File(['abc'], 'cloud.zip')
  Object.defineProperty(file, 'arrayBuffer', { value: async () => new TextEncoder().encode('abc').buffer })
  ota.parseOtaPackage.mockClear()
  return { file, button: () => Array.from(host.querySelectorAll('button')).find(b => b.textContent === '载入在线固件')! }
}

it('loads and validates an online package without starting OTA automatically', async () => {
  const { file, button } = await onlineChoice()
  online.download.mockResolvedValueOnce(file)
  await act(async () => button().click())
  await act(async () => vi.waitFor(() => {
    const failure = host.querySelector('.ota-failure')
    if (failure) throw new Error(failure.textContent || 'package failure')
    expect(ota.parseOtaPackage).toHaveBeenCalled()
  }))
  expect(ota.parseOtaPackage).toHaveBeenCalledWith('nordic', file, 'nrf52832_priceetag')
  expect(host.textContent).toContain('升级包已通过校验')
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it('blocks OTA after online download failure', async () => {
  const { button } = await onlineChoice()
  online.download.mockRejectedValueOnce(new Error('download refused'))
  await act(async () => button().click())
  expect(host.textContent).toContain('download refused')
  expect(ota.parseOtaPackage).not.toHaveBeenCalled()
  expect(Array.from(host.querySelectorAll<HTMLButtonElement>('button')).find(b => b.textContent === '开始 OTA')?.disabled).toBe(true)
  expect(ota.transferOta).not.toHaveBeenCalled()
})

it('discards an online result if the user switches to another device', async () => {
  const { file, button } = await onlineChoice()
  let finish!: (file: File) => void
  online.download.mockImplementationOnce(() => new Promise(resolve => { finish = resolve }))
  await act(async () => button().click())
  const options = online.download.mock.calls.at(-1)![1]
  await act(async () => useDeviceStore.setState({ deviceId: 'another-tag' }))
  expect(options.signal.aborted).toBe(true)
  await act(async () => finish(file))
  expect(ota.parseOtaPackage).not.toHaveBeenCalled()
  expect(ota.transferOta).not.toHaveBeenCalled()
})
