// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { DeviceDiagnostics } from './DeviceDiagnostics'
import { useDeviceStore } from '../store/device'
import { clockDiagnostic } from '../protocol/clockDiagnostic'
import { PROTOCOL_V2_VERSION } from '../protocol/generatedCommandsV2'
const client = vi.hoisted(() => ({ readDiagnostics: vi.fn(), readHealth: vi.fn(), readClockDiagnostic: vi.fn() }))
vi.mock('../protocol/client', () => ({ inktagClient: client }))
let host: HTMLDivElement, root: Root
const button = (label: string) => Array.from(host.querySelectorAll<HTMLButtonElement>('button')).find(node => node.textContent === label)!
const value = (label: string) => Array.from(host.querySelectorAll('dt')).find(node => node.textContent === label)!.nextElementSibling!
beforeEach(async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.clearAllMocks(); useDeviceStore.getState().resetDevice()
  useDeviceStore.setState({deviceId: 'one', connection: 'connected', protocolVersion: PROTOCOL_V2_VERSION, action: 'idle', temperature: 23, mcuTemperature: 24.75, panelTemperature: 23})
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
  await act(async () => root.render(<DeviceDiagnostics />))
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals() })
it('uses compact missing values and exposes availability separately without automatic reads', async () => {
  await act(async () => useDeviceStore.setState({voltage: Number.NaN, temperature: null, mcuTemperature: null, panelTemperature: Number.NaN}))
  for (const label of ['主控', '固件', '地址', '电压', '有效温度', 'MCU 温度', '屏幕温度', '运行模式 / 标志 / 进度', '上电遥测']) expect(value(label).textContent).toBe('--')
  expect(host.querySelector('.diagnostic-grid')).not.toBeNull()
  expect(value('电源状态 / 电压有效').textContent).toBe('--')
  expect(value('电源状态 / 电压有效').getAttribute('title')).toBe('不支持健康诊断')
  expect(host.querySelector('.notice')).toBeNull()
  expect(host.textContent).not.toContain('--（未读取或无效）')
  expect(client.readDiagnostics).not.toHaveBeenCalled(); expect(client.readHealth).not.toHaveBeenCalled(); expect(client.readClockDiagnostic).not.toHaveBeenCalled()
})
it('shows independent temperatures without issuing any command on navigation', async () => {
  expect(host.textContent).toContain('MCU 温度24.75 °C'); expect(host.textContent).toContain('屏幕温度23 °C')
  expect(client.readDiagnostics).not.toHaveBeenCalled(); expect(client.readClockDiagnostic).not.toHaveBeenCalled()
  await act(async () => useDeviceStore.setState({panelTemperature: null}))
  expect(value('屏幕温度').textContent).toBe('--')
  expect(host.textContent).not.toContain('屏幕温度0 °C')
})
it('formats the protocol as hexadecimal and uses the same trimmed name fallback as the overview', async () => {
  await act(async () => useDeviceStore.setState({localAlias: '  ', deviceName: 'IKT_NRF_6775'}))
  expect(value('主控').textContent).toBe('--'); expect(value('固件').textContent).toBe('--'); expect(value('协议').textContent).toBe('0x10')
  expect(value('设备名').textContent).toBe('IKT_NRF_6775'); expect(value('蓝牙名').textContent).toBe('IKT_NRF_6775'); expect(value('地址').textContent).toBe('--')
  await act(async () => useDeviceStore.setState({localAlias: ' My Tag '}))
  expect(value('设备名').textContent).toBe('My Tag'); expect(value('蓝牙名').textContent).toBe('IKT_NRF_6775')
  expect(client.readDiagnostics).not.toHaveBeenCalled()
})
it('shows MCU rather than the runtime product-model string without changing the identity', async () => {
  await act(async () => useDeviceStore.setState({boardId: 'tlsr8359_nebular154'}))
  expect(value('主控').textContent).toBe('TLSR8359')
  expect(host.textContent).not.toContain('tlsr8359_nebular154')
  expect(useDeviceStore.getState().boardId).toBe('tlsr8359_nebular154')
  expect(client.readDiagnostics).not.toHaveBeenCalled()
})
it('reads one clock sample only and removes it after identity change', async () => {
  const payload = new Uint8Array(7); new DataView(payload.buffer).setUint32(0, 1791436800, true)
  client.readClockDiagnostic.mockResolvedValue(clockDiagnostic(payload, 1791436800000, 1791436800100, 100))
  await act(async () => button('走时诊断').click())
  expect(client.readClockDiagnostic).toHaveBeenCalledTimes(1); expect(client.readDiagnostics).not.toHaveBeenCalled(); expect(client.readHealth).not.toHaveBeenCalled()
  expect(host.textContent).toContain('设备时间（设备时区）')
  await act(async () => useDeviceStore.setState({deviceId: 'two'}))
  expect(host.textContent).not.toContain('设备时间（设备时区）')
})
it('keeps unavailable/Recovery/busy reads disabled and displays read failure', async () => {
  client.readClockDiagnostic.mockRejectedValue(new Error('DENIED'))
  await act(async () => button('走时诊断').click())
  expect(document.querySelector('[role=alert]')?.textContent).toBe('DENIED')
  for (const state of [{action: 'ota' as const}, {action: 'idle' as const, protocolVersion: null}, {protocolVersion: 2}, {protocolVersion: PROTOCOL_V2_VERSION, connection: 'idle' as const}]) {
    await act(async () => useDeviceStore.setState(state)); expect(button('走时诊断').disabled).toBe(true)
  }
  expect(value('电源状态 / 电压有效').getAttribute('title')).toBe('连接 App 后读取')
})
it('queries optional health only after STATUS and retains an explicit refusal with no retry', async () => {
  await act(async () => useDeviceStore.getState().setCapabilities({health: true}))
  client.readDiagnostics.mockResolvedValue({mode: 2, flags: 12, progress: 0, rawHex: '01020c'})
  client.readHealth.mockRejectedValue(new Error('DENIED'))
  await act(async () => button('运行诊断').click())
  expect(client.readDiagnostics).toHaveBeenCalledTimes(1); expect(client.readHealth).toHaveBeenCalledTimes(1)
  expect(host.textContent).toContain('2 / 0x0c / 0%'); expect(document.querySelector('[role=alert]')?.textContent).toBe('DENIED')
  expect(useDeviceStore.getState().health).toBeNull()
  await act(async () => useDeviceStore.getState().setCapabilities({health: false}))
  await act(async () => button('运行诊断').click())
  expect(client.readHealth).toHaveBeenCalledTimes(1)
  expect(value('电源状态 / 电压有效').getAttribute('title')).toBe('不支持健康诊断')
})
it('keeps actual zero counts and explicit refusals while compacting optional telemetry', async () => {
  await act(async () => useDeviceStore.setState({voltage: 0, panelTemperature: 0, health: {
    powerState: 'critical', voltageValid: false, displayAllowed: false, flashAllowed: false, otaAllowed: false,
    watchdogActive: false, warningPageShown: false, thresholdsQualified: false, resetReason: 0, voltageMv: 0,
    warningMv: 2600, displayMinMv: 2400, flashMinMv: 2400, otaMinMv: 2600, recoveryHysteresisMv: 100,
    totalCrashes: 0, crashRecordCount: 0, consecutiveCrashes: 0, latestCrash: null,
    soakTelemetry: {version: 1, saturated: false, findActive: false, uptimeMs: 0, connectionSessions: 0,
      findRequests: null, findStarts: 0, findCompletions: 0, findStartFailures: 0, findStopFailures: 0,
      findConnectionPauses: 0, findActiveMs: Number.NaN, radioMaintenanceTurns: null, sleepEntries: 0},
  }}))
  expect(value('电压').textContent).toBe('0.000 V'); expect(value('屏幕温度').textContent).toBe('0 °C')
  expect(value('显示 / Flash / OTA 准入').textContent).toBe('拒绝 / 拒绝 / 拒绝')
  expect(value('复位原因 / 累计异常 / 连续异常').textContent).toBe('0 / 0 / 0')
  expect(value('运行时间 / 连接会话').textContent).toBe('0.0 秒 / 0')
  expect(value('Find 请求 / 启动 / 完成').textContent).toBe('-- / 0 / 0')
  expect(value('Find 累计活跃').textContent).toBe('--')
  expect(JSON.parse(host.querySelector('.diagnostic-raw pre')!.textContent!).health.totalCrashes).toBe(0)
  expect(client.readDiagnostics).not.toHaveBeenCalled()
})
it('uses dashes for invalid device time but retains the raw clock sample and warning', async () => {
  client.readClockDiagnostic.mockResolvedValue(clockDiagnostic(new Uint8Array(7), 1791436800000, 1791436800100, 100))
  await act(async () => button('走时诊断').click())
  expect(value('设备时间（设备时区）').textContent).toBe('--')
  expect(host.querySelector('.clock-diagnostic header span')!.textContent).toBe('--')
  expect(host.textContent).toContain('设备时间未初始化或超出有效范围')
  expect(JSON.parse(host.querySelector('.diagnostic-raw pre')!.textContent!).clock.deviceUtcTimestamp).toBe(0)
  expect(client.readClockDiagnostic).toHaveBeenCalledTimes(1)
})
