import { renderToStaticMarkup } from 'react-dom/server'
import { MemoryRouter } from 'react-router-dom'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { RecoveryDialog } from './RecoveryDialog'
import { inktagClient } from '../protocol/client'
import { useDeviceStore } from '../store/device'

vi.mock('../store/device', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../store/device')>()
  return { ...actual, useDeviceStore: Object.assign(() => actual.useDeviceStore.getState(), actual.useDeviceStore) }
})

const render = () => renderToStaticMarkup(<MemoryRouter><RecoveryDialog /></MemoryRouter>)
describe('all-platform Recovery choice dialog', () => {
  beforeEach(() => {
    useDeviceStore.getState().resetDevice()
    vi.restoreAllMocks()
    vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('goodix')
  })
  it('offers exactly OTA navigation and guarded Recovery exit after connection', () => {
    useDeviceStore.getState().setIdentity({ firmwareVersion: 'Recovery', deviceName: 'IKT_GR_1234_OTA' })
    useDeviceStore.getState().setConnection('connected')
    const html = render()
    expect(html).toContain('设备处于 Recovery')
    expect(html).toContain('IKT_GR_1234_OTA')
    expect(html).toContain('进入 OTA 页面')
    expect(html).toContain('退出 Recovery')
    expect(html.match(/<button /g)).toHaveLength(2)
    expect(html).toContain('请重新选择与设备匹配的 OTA 应用文件，并完成 OTA 升级')
    expect(html).not.toContain('恢复出厂')
  })
  it('never prompts during an OTA transfer or a normal application connection', () => {
    useDeviceStore.getState().setConnection('connected')
    expect(render()).toBe('')
    useDeviceStore.getState().setIdentity({ firmwareVersion: 'Recovery' })
    useDeviceStore.getState().setAction('ota')
    expect(render()).toBe('')
  })
  it('offers TLSR Recovery connection choices without promising a partial App can boot', () => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue('telink')
    useDeviceStore.getState().setIdentity({ firmwareVersion: 'Recovery', deviceName: 'IKT_TL_6F40_OTA' })
    useDeviceStore.getState().setConnection('connected')
    const html = render()
    expect(html).toContain('IKT_TL_6F40_OTA')
    expect(html).toContain('退出 Recovery')
    expect(html).toContain('进入 OTA 页面')
    expect(html).toContain('请重新选择与设备匹配的 OTA 应用文件，并完成 OTA 升级')
    expect(html).not.toContain('退出不会刷写固件')
  })
  it.each(['onmicro', 'dialog', 'nordic'] as const)('shows %s Recovery with only its supported choices', (type) => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(type)
    useDeviceStore.getState().setIdentity({ firmwareVersion: 'Recovery', deviceName: 'exact-recovery' })
    useDeviceStore.getState().setConnection('connected')
    const html = render()
    expect(html).toContain('设备处于 Recovery')
    expect(html).toContain('进入 OTA 页面')
    if (type === 'onmicro') expect(html).toContain('退出 Recovery')
    else {
      expect(html).not.toContain('退出 Recovery')
      expect(html).toContain('完成 OTA 升级后返回应用')
      expect(html.match(/<button /g)).toHaveLength(1)
    }
  })
  it.each(['dialog', 'nordic'] as const)('offers %s guarded exit only after exact native capability proof', (type) => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(type)
    vi.spyOn(inktagClient, 'getRecoverySafeResumeSupported').mockReturnValue(true)
    useDeviceStore.getState().setIdentity({ firmwareVersion: 'Recovery', deviceName: 'IKT_DA_A458_OTA' })
    useDeviceStore.getState().setConnection('connected')
    expect(render()).toContain('退出 Recovery')
    expect(render()).toContain('请重新选择与设备匹配的 OTA 应用文件，并完成 OTA 升级')
  })
})
