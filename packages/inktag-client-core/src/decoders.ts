import { PROTOCOL_V2_VERSION, SCREEN_PROFILE_V2_VERSION, SCREEN_PROFILE_V2_WIRE_SIZE } from './generated/commands-v2'

function decodeUtf8(bytes: Uint8Array): string {
  let result = ''
  for (let index = 0; index < bytes.length;) {
    const first = bytes[index++]
    let codePoint = first
    let remaining = 0
    if ((first & 0xe0) === 0xc0) { codePoint = first & 0x1f; remaining = 1 }
    else if ((first & 0xf0) === 0xe0) { codePoint = first & 0x0f; remaining = 2 }
    else if ((first & 0xf8) === 0xf0) { codePoint = first & 0x07; remaining = 3 }
    else if (first >= 0x80) { result += '\ufffd'; continue }
    if (index + remaining > bytes.length) { result += '\ufffd'; break }
    let valid = true
    for (let part = 0; part < remaining; part += 1) {
      const continuation = bytes[index++]
      if ((continuation & 0xc0) !== 0x80) { valid = false; break }
      codePoint = (codePoint << 6) | (continuation & 0x3f)
    }
    if (!valid || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) result += '\ufffd'
    else result += String.fromCodePoint(codePoint)
  }
  return result
}

function view(bytes: Uint8Array): DataView {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
}

export type HelloV2 = { frameSize: number; writePayload: number }
export type CapabilitiesV2 = {
  mask: number
  extensionMask: number
  displayModeMask: number
  nameMaxBytes: number
  labelMaxBytes: number
  settingsSchema: number
}
export type DeviceInfoV2 = {
  boardId: string
  deviceName: string
  deviceLabel: string
  bleAddress: string | null
  applicationVersion: number
  applicationSize: number
  applicationChecksum: number
}
export type StatusSnapshotV2 = {
  flags: number
  busyPhase: number
  temperatureC: number | null
  voltageMv: number | null
}
export type DeviceTimeV2 = { utcTimestamp: number; timezoneMinutes: number; weekStart: number }
export type ScreenProfileV2 = {
  schemaVersion: number
  controller: number
  recipe: number
  color: number
  transform: number
  nativeUiScale: number
  width: number
  height: number
  visibleX: number
  visibleY: number
  visibleWidth: number
  visibleHeight: number
}
export type DeviceHealthV2 = {
  powerState: number
  flags: number
  resetReason: number
  voltageMv: number
  warningMv: number
  displayMinMv: number
  flashMinMv: number
  otaMinMv: number
  recoveryHysteresisMv: number
  totalCrashes: number
  crashRecordCount: number
  consecutiveCrashes: number
  latestCrash: null | {
    reason: number
    resetReason: number
    operation: number
    voltageMv: number
    uptimeMs: number
    stage: number
    pc: number
    lr: number
    cfsr: number
    hfsr: number
  }
}

export function decodeHelloV2(bytes: Uint8Array): HelloV2 | null {
  if (bytes.length < 6 || bytes[0] !== 1 || bytes[1] !== PROTOCOL_V2_VERSION) return null
  const data = view(bytes)
  return { frameSize: data.getUint16(2, true), writePayload: Math.max(20, data.getUint16(4, true)) }
}

export function decodeCapabilitiesV2(bytes: Uint8Array): CapabilitiesV2 | null {
  if (bytes.length < 17 || bytes[0] !== 1) return null
  const data = view(bytes)
  return {
    mask: data.getUint32(1, true), extensionMask: bytes.length >= 28 ? data.getUint32(24, true) : 0,
    displayModeMask: data.getUint16(5, true),
    nameMaxBytes: bytes[13], labelMaxBytes: bytes[14], settingsSchema: bytes.length >= 24 ? bytes[17] : 0,
  }
}

export function decodeDeviceInfoV2(bytes: Uint8Array): DeviceInfoV2 | null {
  if (bytes.length < 23 || bytes[0] !== 1) return null
  const boardLength = bytes[1]
  const nameLength = bytes[2]
  const labelLength = bytes[3]
  const stringsEnd = 23 + boardLength + nameLength + labelLength
  if (stringsEnd > bytes.length) return null
  const data = view(bytes)
  const boardStart = 23
  const nameStart = boardStart + boardLength
  const labelStart = nameStart + nameLength
  return {
    boardId: decodeUtf8(bytes.subarray(boardStart, nameStart)),
    deviceName: decodeUtf8(bytes.subarray(nameStart, labelStart)),
    deviceLabel: decodeUtf8(bytes.subarray(labelStart, stringsEnd)),
    bleAddress: bytes[4]
      ? Array.from(bytes.subarray(5, 11), (value) => value.toString(16).padStart(2, '0')).join(':').toUpperCase()
      : null,
    applicationVersion: data.getUint32(11, true),
    applicationSize: data.getUint32(15, true),
    applicationChecksum: data.getUint32(19, true),
  }
}

export function decodeStatusV2(bytes: Uint8Array): StatusSnapshotV2 | null {
  if (bytes.length < 16 || bytes[0] !== 1) return null
  const data = view(bytes)
  const flags = bytes[2]
  return {
    flags,
    busyPhase: bytes[1],
    temperatureC: (flags & 0x08) !== 0 ? data.getInt8(7) : null,
    voltageMv: (flags & 0x04) !== 0 ? data.getUint16(8, true) : null,
  }
}

export function decodeTimeV2(bytes: Uint8Array): DeviceTimeV2 | null {
  if (bytes.length !== 7 || bytes[6] > 6) return null
  const data = view(bytes)
  return { utcTimestamp: data.getUint32(0, true), timezoneMinutes: data.getInt16(4, true), weekStart: bytes[6] }
}

export function decodeScreenProfileV2(bytes: Uint8Array): ScreenProfileV2 | null {
  if (bytes.length !== SCREEN_PROFILE_V2_WIRE_SIZE || bytes[0] !== SCREEN_PROFILE_V2_VERSION) return null
  const data = view(bytes)
  return {
    schemaVersion: bytes[0], controller: bytes[1], recipe: bytes[2], color: bytes[3],
    transform: bytes[4] & 0x0f, nativeUiScale: (bytes[4] >> 4) & 0x0f,
    width: data.getUint16(5, true), height: data.getUint16(7, true),
    visibleX: data.getUint16(9, true), visibleY: data.getUint16(11, true),
    visibleWidth: data.getUint16(13, true), visibleHeight: data.getUint16(15, true),
  }
}

export function decodeHealthV2(bytes: Uint8Array): DeviceHealthV2 | null {
  if (bytes.length < 56 || bytes[0] !== 1 || bytes[1] > 3) return null
  const data = view(bytes)
  const recordCount = bytes[20]
  const hasCrash = recordCount > 0 && bytes[23] !== 0
  return {
    powerState: bytes[1], flags: bytes[2], resetReason: bytes[3], voltageMv: data.getUint16(4, true),
    warningMv: data.getUint16(6, true), displayMinMv: data.getUint16(8, true),
    flashMinMv: data.getUint16(10, true), otaMinMv: data.getUint16(12, true),
    recoveryHysteresisMv: data.getUint16(14, true), totalCrashes: data.getUint32(16, true),
    crashRecordCount: recordCount, consecutiveCrashes: bytes[22],
    latestCrash: hasCrash ? {
      reason: bytes[23], resetReason: bytes[24], operation: bytes[25],
      voltageMv: data.getUint16(28, true), uptimeMs: data.getUint32(32, true),
      stage: data.getUint32(36, true), pc: data.getUint32(40, true), lr: data.getUint32(44, true),
      cfsr: data.getUint32(48, true), hfsr: data.getUint32(52, true),
    } : null,
  }
}
