import type { DeviceCrashAlert, DeviceHealth, DeviceHealthCrash, DeviceSoakTelemetry } from '../types'

const powerStates: DeviceHealth['powerState'][] = ['unknown', 'normal', 'warning', 'critical']
const crashReasons: Record<number, string> = { 1: '看门狗复位', 2: 'HardFault', 3: '低电压复位' }
const healthOperations: Record<number, string> = {
  0: '启动', 1: '蓝牙', 2: '屏幕刷新', 3: '数据传输', 4: '存储', 5: 'OTA', 6: 'NFC',
}

export function suppressBrownoutAlert(health: DeviceHealth): boolean {
  return health.latestCrash?.reason === 3 && health.voltageValid &&
    health.voltageMv > 2800 && health.voltageMv <= 6000
}

export function buildCrashAlert(health: DeviceHealth, acknowledged?: number): DeviceCrashAlert | null {
  if (health.totalCrashes <= 0 || (acknowledged !== undefined && health.totalCrashes <= acknowledged)) return null
  if (suppressBrownoutAlert(health)) return null
  const latest = health.latestCrash
  const registers: DeviceCrashAlert['registers'] = {}
  if (latest) {
    for (const key of ['pc', 'lr', 'cfsr', 'hfsr'] as const) {
      if (latest[key] !== 0) registers[key] = latest[key]
    }
  }
  return {
    newCrashes: acknowledged === undefined ? null : health.totalCrashes - acknowledged,
    totalCrashes: health.totalCrashes,
    resetReason: latest?.resetReason ?? health.resetReason,
    reason: latest ? (crashReasons[latest.reason] ?? `未知原因 (${latest.reason})`) : '未提供',
    operation: latest ? (healthOperations[latest.operation] ?? `未知操作 (${latest.operation})`) : '未提供',
    stage: latest?.stage ?? '未提供',
    voltageMv: latest?.voltageMv || null,
    uptimeMs: latest?.uptimeMs ?? null,
    registers,
  }
}

function stageText(value: number) {
  const bytes = [value >>> 24, value >>> 16, value >>> 8, value]
    .map((part) => part & 0xff)
  return bytes.every((byte) => byte >= 0x20 && byte <= 0x7e)
    ? String.fromCharCode(...bytes)
    : `0x${value.toString(16).padStart(8, '0')}`
}

export function decodeDeviceHealth(payload: Uint8Array): DeviceHealth | null {
  if (payload.length < 56 || payload[0] !== 1 || payload[1] >= powerStates.length) return null
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const flags = payload[2]
  const recordCount = payload[20]
  let latestCrash: DeviceHealthCrash | null = null
  if (recordCount > 0 && payload[23] !== 0) {
    latestCrash = {
      reason: payload[23], resetReason: payload[24], operation: payload[25],
      voltageMv: view.getUint16(28, true), uptimeMs: view.getUint32(32, true),
      stage: stageText(view.getUint32(36, true)), pc: view.getUint32(40, true),
      lr: view.getUint32(44, true), cfsr: view.getUint32(48, true), hfsr: view.getUint32(52, true),
    }
  }
  let soakTelemetry: DeviceSoakTelemetry | null = null
  if (payload.length >= 108 && payload[56] === 0x49 && payload[57] === 0x53 &&
      payload[58] === 1 && payload[59] >= 52 && payload.length >= 56 + payload[59]) {
    const soakFlags = view.getUint16(60, true)
    const findAvailable = (soakFlags & 0x0001) !== 0
    soakTelemetry = {
      version: payload[58], saturated: (soakFlags & 0x8000) !== 0,
      findActive: (soakFlags & 0x0100) !== 0,
      uptimeMs: (soakFlags & 0x0010) !== 0 ? view.getUint32(64, true) : null,
      connectionSessions: (soakFlags & 0x0002) !== 0 ? view.getUint32(68, true) : null,
      findRequests: findAvailable ? view.getUint32(72, true) : null,
      findStarts: findAvailable ? view.getUint32(76, true) : null,
      findCompletions: findAvailable ? view.getUint32(80, true) : null,
      findStartFailures: findAvailable ? view.getUint32(84, true) : null,
      findStopFailures: findAvailable ? view.getUint32(88, true) : null,
      findConnectionPauses: findAvailable ? view.getUint32(92, true) : null,
      findActiveMs: findAvailable ? view.getUint32(96, true) : null,
      radioMaintenanceTurns: (soakFlags & 0x0004) !== 0 ? view.getUint32(100, true) : null,
      sleepEntries: (soakFlags & 0x0008) !== 0 ? view.getUint32(104, true) : null,
    }
  }
  return {
    powerState: powerStates[payload[1]], voltageValid: (flags & 0x01) !== 0,
    displayAllowed: (flags & 0x02) !== 0, flashAllowed: (flags & 0x04) !== 0,
    otaAllowed: (flags & 0x08) !== 0, watchdogActive: (flags & 0x10) !== 0,
    warningPageShown: (flags & 0x40) !== 0, thresholdsQualified: (flags & 0x80) === 0,
    resetReason: payload[3], voltageMv: view.getUint16(4, true),
    warningMv: view.getUint16(6, true), displayMinMv: view.getUint16(8, true),
    flashMinMv: view.getUint16(10, true), otaMinMv: view.getUint16(12, true),
    recoveryHysteresisMv: view.getUint16(14, true), totalCrashes: view.getUint32(16, true),
    crashRecordCount: recordCount, consecutiveCrashes: payload[22], latestCrash, soakTelemetry,
  }
}
