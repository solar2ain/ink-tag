// @vitest-environment jsdom
import { act } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { CrashAlertDialog } from './CrashAlertDialog'
import { buildCrashAlert } from '../protocol/health'
import { useDeviceStore } from '../store/device'
import type { DeviceHealth } from '../types'
const save = vi.hoisted(() => vi.fn().mockResolvedValue(undefined))
vi.mock('../data/devicePreferences', () => ({ updateDevicePreference: save }))
let host: HTMLDivElement, root: Root
const health: DeviceHealth = {
  powerState: 'normal', voltageValid: true, voltageMv: 2920,
  displayAllowed: true, flashAllowed: true, otaAllowed: true,
  warningMv: 2400, displayMinMv: 0, flashMinMv: 2400, otaMinMv: 2600,
  recoveryHysteresisMv: 150, watchdogActive: true, warningPageShown: false,
  thresholdsQualified: false, resetReason: 1, totalCrashes: 40,
  crashRecordCount: 4, consecutiveCrashes: 0,
  latestCrash: { reason: 2, resetReason: 4, operation: 2, voltageMv: 2941,
    uptimeMs: 126812659, stage: 'OMDP', pc: 0x46d044, lr: 0x46d23d,
    cfsr: 0x8200, hfsr: 0x40000000 },
}
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true); vi.clearAllMocks()
  useDeviceStore.getState().resetDevice()
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => {
  await act(async () => root.unmount()); host.remove()
  useDeviceStore.getState().resetDevice(); vi.unstubAllGlobals()
})
async function show(acknowledged?: number) {
  useDeviceStore.setState({ deviceId: 'isolated-device', crashAlert: buildCrashAlert(health, acknowledged) })
  await act(async () => root.render(<CrashAlertDialog />))
}
it('does not open a dialog or acknowledge first-read historical records', async () => {
  const original = structuredClone(health)
  await show()
  expect(host.querySelector('[role="alertdialog"]')).toBeNull()
  expect(save).not.toHaveBeenCalled()
  expect(health).toEqual(original)
})
it.each([[0, 40], [39, 1]])('preserves real increments from explicit baseline %s', async (baseline, count) => {
  await show(baseline)
  expect(host.textContent).toContain('设备发生过新的异常复位')
  expect(host.textContent).toContain(`自本浏览器上次记录后新增 ${count} 次，累计 40 次`)
  expect(save).not.toHaveBeenCalled()
})
