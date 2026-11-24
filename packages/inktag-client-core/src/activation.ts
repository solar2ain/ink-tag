export type FirmwareEdition = 'base' | 'advanced'
export type FirmwareActivationPhase = 'unsupported' | 'unavailable' | 'inactive' | 'active' | 'invalid'
export interface FirmwareActivation {
  phase: FirmwareActivationPhase
  edition: FirmwareEdition
  activationId?: string
  scheme?: number
  lastResult?: number
  codeLength?: number
  keyId?: number
  sequence?: number
  featureMask?: number
  requestActive?: boolean
}
import { ActivationResultV2, ActivationSchemeV2, ActivationStateV2 } from './generated/commands-v2'

export const ACTIVATION_CODE_CHARS = 128
export const ACTIVATION_CODE_BYTES = 96
export const ACTIVATION_STATUS_BYTES = 24
export const ACTIVATION_REQUEST_BYTES = 32

export interface FirmwareActivationRequest {
  scheme: number
  keyId: number
  productId: number
  securityEpoch: number
  supportedFeatures: number
  installedSequence: number
  deviceId: string
  challenge: string
  requestCode: string
}

export const emptyFirmwareActivation: FirmwareActivation = {
  phase: 'unsupported',
  edition: 'base',
}

export function hasFirmwareEdition(activation: FirmwareActivation, required: FirmwareEdition) {
  if (required === 'base') return true
  return activation.phase === 'active' && activation.edition === 'advanced'
}

export function normalizeActivationCode(value: string) {
  return value.trim()
}

function hex(bytes: Uint8Array) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('').toUpperCase()
}

export function decodeActivationCode(value: string) {
  const compact = normalizeActivationCode(value)
  if (compact.length !== ACTIVATION_CODE_CHARS) throw new Error('激活码应为 128 个字符。')
  if (!/^[A-Za-z0-9_-]+$/.test(compact)) throw new Error('激活码包含无效字符。')
  // IKAC1 has exactly 96 bytes / 128 Base64URL characters, without padding bits.
  // Avoid browser atob / Node Buffer so native clients share the same validation.
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'
  const output = new Uint8Array(ACTIVATION_CODE_BYTES)
  for (let index = 0, offset = 0; index < compact.length; index += 4) {
    const word = (alphabet.indexOf(compact[index]) << 18) | (alphabet.indexOf(compact[index + 1]) << 12)
      | (alphabet.indexOf(compact[index + 2]) << 6) | alphabet.indexOf(compact[index + 3])
    output[offset++] = word >>> 16
    output[offset++] = word >>> 8
    output[offset++] = word
  }
  if (output.length !== ACTIVATION_CODE_BYTES) throw new Error('激活码长度无效。')
  if (output[0] !== 0x49 || output[1] !== 0x41 || output[2] !== 1) {
    throw new Error('激活码不是 IKAC1 格式。')
  }
  return output
}

export function parseActivationStatus(payload: Uint8Array): FirmwareActivation {
  if (payload.length !== ACTIVATION_STATUS_BYTES || payload[0] !== 2) {
    throw new Error('设备激活状态响应无效。')
  }
  const phases = new Map<number, FirmwareActivation['phase']>([
    [ActivationStateV2.UNAVAILABLE, 'unavailable'],
    [ActivationStateV2.DEFAULT, 'inactive'],
    [ActivationStateV2.PRO, 'active'],
    [ActivationStateV2.INVALID, 'invalid'],
  ])
  const phase = phases.get(payload[1])
  if (!phase) throw new Error('设备返回了未知激活状态。')
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  return {
    phase,
    edition: phase === 'active' ? 'advanced' : 'base',
    activationId: phase === 'unavailable' ? undefined : hex(payload.subarray(16, 24)),
    scheme: payload[2],
    lastResult: payload[3],
    keyId: payload[4] || undefined,
    requestActive: (payload[5] & 0x02) !== 0,
    codeLength: payload[6] || undefined,
    sequence: view.getUint32(8, true),
    featureMask: view.getUint32(12, true),
  }
}

export function parseActivationRequest(payload: Uint8Array): FirmwareActivationRequest {
  if (payload.length !== ACTIVATION_REQUEST_BYTES || payload[0] !== 1) {
    throw new Error('设备激活请求响应无效。')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const deviceId = hex(payload.subarray(16, 24))
  const challenge = hex(payload.subarray(24, 32))
  if (!/^(?!0{16})[0-9A-F]{16}$/.test(deviceId) ||
      !/^(?!0{16})[0-9A-F]{16}$/.test(challenge)) {
    throw new Error('设备返回了无效的设备码或挑战码。')
  }
  return {
    scheme: payload[1],
    keyId: payload[2],
    productId: view.getUint16(4, true),
    securityEpoch: view.getUint16(6, true),
    supportedFeatures: view.getUint32(8, true),
    installedSequence: view.getUint32(12, true),
    deviceId,
    challenge,
    requestCode: `${deviceId}:${challenge}`,
  }
}

export const activationResultLabels: Record<number, string> = {
  [ActivationResultV2.OK]: '状态正常',
  [ActivationResultV2.BAD_LENGTH]: '激活码长度不正确',
  [ActivationResultV2.BAD_FORMAT]: '激活码格式不正确',
  [ActivationResultV2.KEY_UNAVAILABLE]: '固件未配置对应的产品公钥',
  [ActivationResultV2.WRONG_CODE]: '激活码与当前协议不兼容',
  [ActivationResultV2.STORAGE_FAILURE]: '设备保存激活状态失败',
  [ActivationResultV2.IDENTITY_UNAVAILABLE]: '无法读取芯片唯一标识',
  [ActivationResultV2.INTERNAL_ERROR]: '设备验签发生内部错误',
  [ActivationResultV2.NO_SESSION]: '激活请求已超时，请重新获取挑战码',
  [ActivationResultV2.WRONG_DEVICE]: '激活码不属于这台设备',
  [ActivationResultV2.WRONG_PRODUCT]: '激活码产品类型不匹配',
  [ActivationResultV2.CHALLENGE_MISMATCH]: '挑战码已变化，请重新签发',
  [ActivationResultV2.UNSUPPORTED_FEATURE]: '激活码包含固件不支持的功能',
  [ActivationResultV2.SECURITY_EPOCH_TOO_OLD]: '设备安全代际低于授权要求',
  [ActivationResultV2.STALE_SEQUENCE]: '激活码序号低于当前授权',
  [ActivationResultV2.BAD_SIGNATURE]: '激活码签名无效或已被篡改',
}

export function isP256Activation(activation: FirmwareActivation) {
  return activation.scheme === ActivationSchemeV2.ECDSA_P256_IKAC1_V1
}
