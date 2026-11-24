import { describe, expect, it } from 'vitest'
import { buildCrashAlert, decodeDeviceHealth, suppressBrownoutAlert } from './health'

describe('device health', () => {
  it('decodes power gates and the newest crash record', () => {
    const bytes = new Uint8Array(56)
    const view = new DataView(bytes.buffer)
    bytes[0] = 1; bytes[1] = 2; bytes[2] = 0x35; bytes[3] = 4
    view.setUint16(4, 2630, true); view.setUint16(6, 2700, true)
    view.setUint16(8, 2500, true); view.setUint16(10, 2600, true)
    view.setUint16(12, 2800, true); view.setUint16(14, 150, true)
    view.setUint32(16, 7, true); bytes[20] = 1; bytes[22] = 2
    bytes[23] = 1; bytes[24] = 4; bytes[25] = 2
    view.setUint16(28, 2480, true); view.setUint32(32, 1234, true)
    view.setUint32(36, 0x47524450, true); view.setUint32(40, 0x01024567, true)
    const health = decodeDeviceHealth(bytes)
    expect(health).toMatchObject({ powerState: 'warning', voltageMv: 2630,
      displayAllowed: false, flashAllowed: true, otaAllowed: false,
      watchdogActive: true, thresholdsQualified: true, totalCrashes: 7 })
    expect(health?.latestCrash).toMatchObject({ reason: 1, operation: 2, stage: 'GRDP' })
    expect(health?.soakTelemetry).toBeNull()
  })

  it('decodes the optional soak extension without turning unavailable values into zero', () => {
    const bytes = new Uint8Array(108)
    const view = new DataView(bytes.buffer)
    bytes[0] = 1; bytes[1] = 1
    bytes.set([0x49, 0x53, 1, 52], 56)
    view.setUint16(60, 0x8113, true)
    for (let offset = 64, value = 10; offset <= 104; offset += 4, value += 10) {
      view.setUint32(offset, value, true)
    }
    const health = decodeDeviceHealth(bytes)
    expect(health?.soakTelemetry).toMatchObject({
      version: 1, saturated: true, findActive: true, uptimeMs: 10,
      connectionSessions: 20, findRequests: 30, findStarts: 40,
      findCompletions: 50, findStartFailures: 60, findStopFailures: 70,
      findConnectionPauses: 80, findActiveMs: 90,
    })
    expect(health?.soakTelemetry?.radioMaintenanceTurns).toBeNull()
    expect(health?.soakTelemetry?.sleepEntries).toBeNull()
  })

  it('rejects malformed payloads', () => {
    expect(decodeDeviceHealth(new Uint8Array(12))).toBeNull()
  })

  it('reports only total crash-count growth and omits zero diagnostic registers', () => {
    const bytes = new Uint8Array(56)
    const view = new DataView(bytes.buffer)
    bytes[0] = 1; bytes[1] = 1; bytes[20] = 1
    view.setUint32(16, 7, true)
    bytes[23] = 3; bytes[24] = 5; bytes[25] = 2
    view.setUint16(28, 2595, true); view.setUint32(32, 1234, true)
    view.setUint32(36, 0x4f4d4450, true); view.setUint32(40, 0x12345678, true)
    const health = decodeDeviceHealth(bytes)!
    expect(buildCrashAlert(health, 5)).toEqual(expect.objectContaining({
      newCrashes: 2, totalCrashes: 7, reason: '低电压复位', operation: '屏幕刷新', stage: 'OMDP',
      registers: { pc: 0x12345678 },
    }))
    expect(buildCrashAlert(health, 7)).toBeNull()
    health.consecutiveCrashes = 0
    expect(buildCrashAlert(health, undefined)?.newCrashes).toBeNull()
    expect(buildCrashAlert(health, 0)?.newCrashes).toBe(7)
  })

  it('suppresses only low-voltage reset alerts above 2.8 V, preserving raw records', () => {
    const bytes = new Uint8Array(56)
    const view = new DataView(bytes.buffer)
    bytes[0] = 1; bytes[1] = 1; bytes[2] = 1; bytes[20] = 1; bytes[23] = 3
    view.setUint32(16, 3, true)
    view.setUint16(4, 2801, true)
    view.setUint16(28, 1900, true)
    const health = decodeDeviceHealth(bytes)!
    const original = structuredClone(health)
    expect(suppressBrownoutAlert(health)).toBe(true)
    expect(buildCrashAlert(health)).toBeNull()
    expect(health).toEqual(original)

    for (const voltageMv of [2800, 2799, 0, 6001]) {
      expect(buildCrashAlert({ ...health, voltageMv })).not.toBeNull()
    }
    expect(buildCrashAlert({ ...health, voltageValid: false })).not.toBeNull()
    for (const reason of [1, 2, 99]) {
      expect(buildCrashAlert({ ...health, latestCrash: { ...health.latestCrash!, reason } })).not.toBeNull()
    }
    expect(buildCrashAlert({ ...health, latestCrash: null })).not.toBeNull()
  })
})
