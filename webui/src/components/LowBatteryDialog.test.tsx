import { describe, expect, it } from 'vitest'
import type { DeviceHealth } from '../types'
import { displayRefreshStatus, lowBatteryMessage } from './LowBatteryDialog'

describe('low-battery dialog', () => {
  it('explains a board whose display refresh has no voltage gate', () => {
    const health: DeviceHealth = {
      powerState: 'warning', voltageValid: true, voltageMv: 2350,
      displayAllowed: true, flashAllowed: false, otaAllowed: false,
      warningMv: 2400, displayMinMv: 0, flashMinMv: 2400, otaMinMv: 2600,
      recoveryHysteresisMv: 150, watchdogActive: true, warningPageShown: false,
      thresholdsQualified: false, resetReason: 0, totalCrashes: 0,
      crashRecordCount: 0, consecutiveCrashes: 0, latestCrash: null,
    }

    const message = lowBatteryMessage(health)
    expect(message).toContain('屏幕刷新不受此低电提示限制')
    expect(message).not.toContain('低于 0.00 V')
  })

  it('shows a concise available state when displayMin=0', () => {
    const health: DeviceHealth = {
      powerState: 'warning', voltageValid: true, voltageMv: 2350,
      displayAllowed: true, flashAllowed: false, otaAllowed: false,
      warningMv: 2400, displayMinMv: 0, flashMinMv: 2400, otaMinMv: 2600,
      recoveryHysteresisMv: 150, watchdogActive: true, warningPageShown: false,
      thresholdsQualified: false, resetReason: 0, totalCrashes: 0,
      crashRecordCount: 0, consecutiveCrashes: 0, latestCrash: null, soakTelemetry: null,
    }
    expect(displayRefreshStatus(health)).toBe('可用')
  })

  it('does not claim a critical state always blocks display refresh', () => {
    const health: DeviceHealth = {
      powerState: 'critical', voltageValid: true, voltageMv: 1950,
      displayAllowed: true, flashAllowed: false, otaAllowed: false,
      warningMv: 2400, displayMinMv: 0, flashMinMv: 2400, otaMinMv: 2600,
      recoveryHysteresisMv: 150, watchdogActive: true, warningPageShown: false,
      thresholdsQualified: false, resetReason: 0, totalCrashes: 0,
      crashRecordCount: 0, consecutiveCrashes: 0, latestCrash: null,
    }
    expect(lowBatteryMessage(health)).not.toContain('阻止新的屏幕刷新')
    expect(displayRefreshStatus(health)).toBe('可用')
  })
})
