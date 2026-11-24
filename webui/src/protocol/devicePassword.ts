/** Password wire helpers. Firmware must advertise DEVICE_PASSWORD before use. */
import { buildInkTagDeviceUrl, INKTAG_DEVICE_LINK_ROOT } from '../qr/qr'
import { RELEASE_VERSION } from './generatedReleaseV1'

export const DEVICE_PASSWORD_WARNING = '二维码包含控制凭据，请勿公开展示。'
const encoder = new TextEncoder()
const hex = (data: Uint8Array) => Array.from(data, (v) => v.toString(16).padStart(2, '0')).join('')
const concat = (...parts: Uint8Array[]) => {
  const output = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0))
  let offset = 0
  for (const part of parts) { output.set(part, offset); offset += part.length }
  return output
}
const buffer = (data: Uint8Array) => Uint8Array.from(data).buffer

export function validateDevicePassword(value: string) {
  if (value.length < 4 || value.length > 16 || /[^A-Za-z0-9]/.test(value)) throw new Error('密码须为 4–16 位字母或数字，区分大小写。')
  return value
}

/** Input validation is not device-side verification and never authorizes a write. */
export function validatePasswordChange(oldPassword: string, newPassword: string, confirmation: string) {
  validateDevicePassword(oldPassword)
  validateDevicePassword(newPassword)
  if (newPassword !== confirmation) throw new Error('两次输入的新密码不一致。')
}

export function generateDevicePassword(length = 8) {
  if (!Number.isInteger(length) || length < 4 || length > 16) throw new Error('密码长度须为 4–16 位。')
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'
  let output = ''
  while (output.length < length) {
    const bytes = crypto.getRandomValues(new Uint8Array(32))
    for (const byte of bytes) {
      if (byte < 248 && output.length < length) output += alphabet[byte % 62]
    }
  }
  return output
}

function deviceBytes(id: string) {
  if (id.length !== 16 || /[^0-9a-fA-F]/.test(id) || /^0+$/.test(id)) throw new Error('设备标识无效。')
  return Uint8Array.from(id.match(/../g)!, (part) => Number.parseInt(part, 16))
}

export async function derivePasswordCredential(id: string, password: string) {
  return new Uint8Array(await crypto.subtle.digest('SHA-256', buffer(concat(
    encoder.encode('IKT-DEVICE-PASSWORD-KEY-v1'), deviceBytes(id), encoder.encode(validateDevicePassword(password)),
  ))))
}

/** W is independent of the QR bearer V and never exported in a QR. */
export async function derivePasswordChangeKey(id: string, password: string) {
  return new Uint8Array(await crypto.subtle.digest('SHA-256', buffer(concat(
    encoder.encode('IKT-DEVICE-PASSWORD-CHANGE-v1'), deviceBytes(id), encoder.encode(validateDevicePassword(password)),
  ))))
}

export interface DevicePasswordStatus {
  enabled: boolean; authorized: boolean; failures: number; retryAfterMs: number; revision: number
}

export function decodeDevicePasswordStatus(payload: Uint8Array): DevicePasswordStatus {
  if (payload.length !== 12 || payload[0] !== 1 || payload[1] > 1 || payload[2] > 1 || (!payload[1] && payload[2])) {
    throw new Error('设备密码状态无效。')
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const revision = view.getUint32(8, true)
  if (payload[1] && !revision) throw new Error('设备密码版本无效。')
  return { enabled: Boolean(payload[1]), authorized: Boolean(payload[2]), failures: payload[3], retryAfterMs: view.getUint32(4, true), revision }
}

export const RESET_SETTINGS_PROOF_DATA = concat(encoder.encode('IKT-RESET-SETTINGS-v1'), new Uint8Array([0x0e, 0x01, 0x05]))

export async function createPasswordProof(id: string, credential: Uint8Array, challenge: Uint8Array, candidateBundle?: Uint8Array) {
  if (credential.length !== 32 || !credential.some(Boolean) || challenge.length !== 22 || challenge[0] !== 1 || challenge[1] > 2 ||
      !challenge.subarray(2, 18).some(Boolean) || !challenge.subarray(18).some(Boolean)) throw new Error('设备密码挑战无效。')
  const purpose = challenge[1]
  if ((purpose === 0) !== (candidateBundle === undefined)) throw new Error('修改或关闭密码须绑定完整候选数据。')
  const settingsReset = purpose === 2 && candidateBundle?.length === RESET_SETTINGS_PROOF_DATA.length &&
    candidateBundle.every((byte, index) => byte === RESET_SETTINGS_PROOF_DATA[index])
  if (candidateBundle && !settingsReset && (candidateBundle.length < 73 || candidateBundle[0] !== RELEASE_VERSION || candidateBundle[1] !== Number(purpose === 1))) throw new Error('候选数据与校验目的不符。')
  const digest = candidateBundle ? new Uint8Array(await crypto.subtle.digest('SHA-256', buffer(candidateBundle))) : new Uint8Array()
  const key = await crypto.subtle.importKey('raw', buffer(credential), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign'])
  const message = concat(encoder.encode('IKT-DEVICE-PASSWORD-PROOF-v1'), deviceBytes(id), challenge.subarray(18), challenge.subarray(2, 18), new Uint8Array([purpose]), digest)
  const tag = new Uint8Array(await crypto.subtle.sign('HMAC', key, buffer(message)))
  return concat(new Uint8Array([1, purpose]), digest, tag.subarray(0, 16))
}

export function buildPasswordQrLink(name: string, id: string, credential: Uint8Array) {
  deviceBytes(id)
  if (credential.length !== 32 || !credential.some(Boolean)) throw new Error('扫码凭据无效。')
  return `${buildInkTagDeviceUrl(name)}#/?device=${id.toUpperCase()}&auth=${hex(credential).toUpperCase()}`
}

export function parsePasswordQrLink(value: string) {
  const url = new URL(value)
  const base = new URL(INKTAG_DEVICE_LINK_ROOT)
  if (url.origin !== base.origin || url.pathname !== base.pathname || url.username || url.password || !url.hash.startsWith('#/?')) throw new Error('密码二维码地址无效。')
  const fields = new URLSearchParams(url.hash.slice(3))
  if ([...fields.keys()].length !== 2 || fields.getAll('device').length !== 1 || fields.getAll('auth').length !== 1) throw new Error('密码二维码字段无效。')
  const deviceId = (fields.get('device') ?? '').toUpperCase()
  deviceBytes(deviceId)
  const token = fields.get('auth') ?? ''
  if (token.length !== 64 || /[^0-9a-fA-F]/.test(token) || /^0+$/.test(token)) throw new Error('扫码凭据无效。')
  const credential = Uint8Array.from(token.match(/../g)!, (part) => Number.parseInt(part, 16))
  url.hash = ''
  // Candidate only: compare actual immutable ID and authenticate before saving.
  return { deviceId, credential, cleanUrl: url.href }
}

export function encodePasswordBundle(credential: Uint8Array | null, modules: number, packedQr: Uint8Array, changeKey: Uint8Array | null = null, revision = 1) {
  if (!Number.isInteger(modules) || modules < 21 || modules > 57 || (modules - 21) % 4 || packedQr.length !== Math.ceil(modules * modules / 8)) throw new Error('密码二维码矩阵无效。')
  const padding = packedQr.length * 8 - modules * modules
  if (padding && (packedQr[packedQr.length - 1] & ((1 << padding) - 1))) throw new Error('二维码填充无效。')
  if (!Number.isInteger(revision) || revision < 1 || revision > 0xffffffff) throw new Error('设备密码版本无效。')
  if ((credential === null) !== (changeKey === null)) throw new Error('需要完整的独立密码校验材料。')
  if (credential && (credential.length !== 32 || !credential.some(Boolean) || changeKey?.length !== 32 || !changeKey.some(Boolean) || credential.every((v, i) => v === changeKey[i]))) throw new Error('扫码凭据无效。')
  if (credential && 73 + packedQr.length > 420) throw new Error('密码二维码超出设备配置容量。')
  const header = new Uint8Array(73)
  header.set([RELEASE_VERSION, credential ? 1 : 0, modules])
  new DataView(header.buffer).setUint16(3, packedQr.length, true)
  new DataView(header.buffer).setUint32(5, revision, true)
  if (credential) header.set(credential, 9)
  if (changeKey) header.set(changeKey, 41)
  return concat(header, packedQr)
}
