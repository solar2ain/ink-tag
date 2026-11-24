import { RELEASE_FIND_CONFIG_VERSION, RELEASE_FIND_CAPABILITIES_VERSION, RELEASE_FIND_PUBLIC_VERSION } from './generatedReleaseV1'
import { FindActionV2, FindCapabilityV2 } from './generatedCommandsV2'

export const FIND_CONFIG_VERSION = RELEASE_FIND_CONFIG_VERSION
export const FIND_PUBLIC_KEY_BYTES = 28
export const FIND_CONFIG_WIRE_BYTES = 3
export const FIND_CONFIG_RESPONSE_WIRE_BYTES = 5
export const FIND_CAPABILITIES_VERSION_DERIVED = RELEASE_FIND_CAPABILITIES_VERSION
export const FIND_OWNER_NONCE_BYTES = 8
export const FIND_PUBLIC_ONLY_VERSION = RELEASE_FIND_PUBLIC_VERSION
export const FIND_PUBLIC_ONLY_STAGE_BYTES = 81
export const FIND_PUBLIC_ONLY_REPLACEMENT_STAGE_BYTES = 113
export const FIND_PUBLIC_ONLY_STATUS_BYTES = 62
export const FIND_PUBLIC_ONLY_COMMIT_BYTES = 53

export const FindCapability = {
  TIME_MULTIPLEX: 2,
  PERSISTENT_CONFIG: 4,
  PAUSES_WHILE_CONNECTED: 8,
  EXPERIMENTAL: 16,
  OWNER_ROTATION: 32,
  UTC_SLOT_ROTATION: 64,
  INVALID_TIME_FALLBACK: 128,
  PUBLIC_ONLY_P224_V2: 256,
  PUBLIC_ONLY_OWNER_REPLACEMENT: 512,
  PUBLIC_ONLY_OWNER_UNREGISTER: FindCapabilityV2.PUBLIC_ONLY_OWNER_UNREGISTER,
} as const

export interface DeviceFindCapabilities {
  flags: number
  normalWindowMs: number
  findWindowMs: number
  advertisementIntervalUnits: number
  rotationPeriodSeconds?: number
  derivedIdentity?: boolean
  utcSlotRotation?: boolean
  invalidTimeFallback?: boolean
  ownerRotation?: boolean
  publicOnlyP224?: boolean
  publicOnlyOwnerReplacement?: boolean
}

export interface DeviceFindConfig {
  enabled: boolean
  mode: 1
  identityPresent: boolean
  pausedForConnection: boolean
}

export function supportsFindOwnerEnrollment(capabilities?: DeviceFindCapabilities | null) {
  return Boolean(capabilities?.publicOnlyP224)
}

export function supportsFindOwnerReplacement(capabilities?: DeviceFindCapabilities | null) {
  return Boolean(capabilities?.publicOnlyP224 && capabilities.publicOnlyOwnerReplacement)
}

export function supportsFindOwnerUnregister(capabilities?: DeviceFindCapabilities | null) {
  return Boolean(capabilities?.publicOnlyP224 && (capabilities.flags & FindCapability.PUBLIC_ONLY_OWNER_UNREGISTER))
}

export interface FindOwnerRotationChallenge {
  nonceHex: string
}

export interface FindOwnerRotationProof {
  slot: number
  publicKeyHex: string
  fingerprintHex?: string
}

export interface FindPublicOnlyOwnerStatus {
  identityPresent: boolean
  timeValid: boolean
  fingerprintHex: string
  slot: number
  publicKeyHex: string
}

function bytesToHex(bytes: Uint8Array) {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join('').toUpperCase()
}

export function decodeFindOwnerRotationChallenge(payload: Uint8Array): FindOwnerRotationChallenge {
  if (payload.length !== 1 + FIND_OWNER_NONCE_BYTES ||
      payload[0] !== FIND_PUBLIC_ONLY_VERSION) {
    throw new Error('设备返回了无效的 Find 所有者挑战')
  }
  return { nonceHex: bytesToHex(payload.subarray(1)) }
}

export function decodeFindOwnerRotationProof(payload: Uint8Array): FindOwnerRotationProof {
  if (payload.length !== FIND_PUBLIC_ONLY_COMMIT_BYTES || payload[0] !== FIND_PUBLIC_ONLY_VERSION) {
    throw new Error('设备返回了无效的 Find 身份更新证明')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const slot = Number(view.getBigUint64(1, true))
  if (!Number.isSafeInteger(slot)) throw new Error('设备返回的 Find 时间片超出安全范围')
  return {
    slot,
    publicKeyHex: bytesToHex(payload.subarray(9, 37)),
    fingerprintHex: bytesToHex(payload.subarray(37, 53)),
  }
}

export function decodeFindPublicOnlyOwnerStatus(payload: Uint8Array): FindPublicOnlyOwnerStatus {
  if (payload.length !== FIND_PUBLIC_ONLY_STATUS_BYTES || payload[0] !== FIND_PUBLIC_ONLY_VERSION || payload[1] & ~0x0f) {
    throw new Error('设备返回了无效的新版 Find 身份状态')
  }
  const slot = Number(new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getBigUint64(10, true))
  if (!Number.isSafeInteger(slot)) throw new Error('设备 Find 时间片超出安全范围')
  return {
    identityPresent: Boolean(payload[1] & 1),
    timeValid: Boolean(payload[1] & 8),
    slot,
    publicKeyHex: bytesToHex(payload.subarray(18, 46)),
    fingerprintHex: bytesToHex(payload.subarray(46, 62)),
  }
}

export function decodeFindPublicOnlyStagePayload(payloadHex: string, nonceHex: string) {
  const normalized = payloadHex.trim()
  if (!new RegExp(`^[0-9a-fA-F]{${FIND_PUBLIC_ONLY_STAGE_BYTES * 2}}$`).test(normalized)) {
    throw new Error('本机 Find 服务返回了无效的新版身份载荷')
  }
  const payload = Uint8Array.from(normalized.match(/../g) || [], (pair) => Number.parseInt(pair, 16))
  if (payload[0] !== FIND_PUBLIC_ONLY_VERSION || bytesToHex(payload.subarray(1, 9)) !== nonceHex.toUpperCase()) {
    throw new Error('新版 Find 身份载荷与设备挑战不匹配')
  }
  return payload
}

export function decodeFindPublicOnlyReplacementStagePayload(payloadHex: string, nonceHex: string, fingerprintHex: string) {
  const normalized = payloadHex.trim()
  if (!new RegExp(`^[0-9a-fA-F]{${FIND_PUBLIC_ONLY_REPLACEMENT_STAGE_BYTES * 2}}$`).test(normalized)) {
    throw new Error('本机 Find 服务返回了无效的身份更新载荷')
  }
  const payload = Uint8Array.from(normalized.match(/../g) || [], (pair) => Number.parseInt(pair, 16))
  if (payload[0] !== FIND_PUBLIC_ONLY_VERSION || bytesToHex(payload.subarray(1, 9)) !== nonceHex.toUpperCase() ||
      bytesToHex(payload.subarray(9, 25)) !== fingerprintHex.toUpperCase()) {
    throw new Error('新版 Find 身份更新载荷与当前设备不匹配')
  }
  return payload
}

export function decodeFindOwnerUnregisterPayload(payloadHex: string, nonceHex: string, fingerprintHex: string) {
  if (!/^[0-9a-f]{82}$/i.test(payloadHex)) throw new Error('本机 Find 服务返回了无效的解除登记证明。')
  const bytes = Uint8Array.from(payloadHex.match(/../g)!, pair => Number.parseInt(pair, 16))
  if (bytes[0] !== FindActionV2.UNREGISTER_OWNER || bytesToHex(bytes.subarray(1, 9)) !== nonceHex.toUpperCase() ||
      bytesToHex(bytes.subarray(9, 25)) !== fingerprintHex.toUpperCase()) {
    throw new Error('解除登记证明与当前标签不匹配。')
  }
  return bytes
}

export function normalizeFindPublicKey(value: string) {
  const normalized = value.replace(/[^0-9a-f]/gi, '').toUpperCase()
  if (normalized.length !== FIND_PUBLIC_KEY_BYTES * 2 || !/^[0-9A-F]+$/.test(normalized)) {
    throw new Error('Find 广播公钥必须是 28 字节十六进制数据')
  }
  if (/^0+$/.test(normalized) || /^F+$/.test(normalized)) {
    throw new Error('Find 广播公钥无效')
  }
  return normalized
}

export function decodeFindCapabilities(payload: Uint8Array): DeviceFindCapabilities {
  if (payload.length !== 16 || payload[0] !== FIND_CAPABILITIES_VERSION_DERIVED) {
    throw new Error('设备 Find 能力响应无效')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const flags = view.getUint32(1, true)
  if (!(flags & FindCapability.UTC_SLOT_ROTATION) ||
      (flags & FindCapability.INVALID_TIME_FALLBACK)) {
    throw new Error('旧 Find 密钥格式不再支持，请更新设备固件')
  }
  return {
    flags,
    normalWindowMs: view.getUint16(5, true),
    findWindowMs: view.getUint16(7, true),
    advertisementIntervalUnits: view.getUint16(9, true),
    rotationPeriodSeconds: view.getUint16(14, true),
    derivedIdentity: true,
    utcSlotRotation: true,
    invalidTimeFallback: false,
    ownerRotation: Boolean(flags & FindCapability.OWNER_ROTATION),
    publicOnlyP224: Boolean(flags & FindCapability.PUBLIC_ONLY_P224_V2),
    publicOnlyOwnerReplacement: Boolean(flags & FindCapability.PUBLIC_ONLY_OWNER_REPLACEMENT),
  }
}

export function decodeFindConfig(payload: Uint8Array): DeviceFindConfig {
  if (payload.length !== FIND_CONFIG_RESPONSE_WIRE_BYTES || payload[0] !== FIND_CONFIG_VERSION ||
      payload[1] > 1 || payload[2] !== 1 || payload[3] > 1 || payload[4] > 1) {
    throw new Error('设备 Find 配置响应无效')
  }
  return {
    enabled: payload[1] === 1,
    mode: 1,
    identityPresent: payload[3] === 1,
    pausedForConnection: payload[4] === 1,
  }
}

export function encodeFindConfig(enabled: boolean) {
  const payload = new Uint8Array(FIND_CONFIG_WIRE_BYTES)
  payload[0] = FIND_CONFIG_VERSION
  payload[1] = enabled ? 1 : 0
  payload[2] = 1
  return payload
}
