import { decodeActivationCode } from './activation'

export const OWNER_AUTH_VERSION = 1
export const OWNER_AUTH_PRO_CODE = 1
export const OWNER_AUTH_FIND_K = 2
export const OWNER_AUTH_MAX_ACTION_PAYLOAD = 192

const encoder = new TextEncoder()

function concat(...parts: Uint8Array[]) {
  const result = new Uint8Array(parts.reduce((length, part) => length + part.length, 0))
  let offset = 0
  for (const part of parts) {
    result.set(part, offset)
    offset += part.length
  }
  return result
}

function buffer(bytes: Uint8Array): ArrayBuffer {
  return Uint8Array.from(bytes).buffer
}

async function sha256(bytes: Uint8Array) {
  return new Uint8Array(await crypto.subtle.digest('SHA-256', buffer(bytes)))
}

export function parseOwnerChallenge(response: Uint8Array) {
  if (response.length !== 17 || response[0] !== OWNER_AUTH_VERSION ||
      !response.subarray(1).some((value) => value !== 0)) {
    throw new Error('设备返回的所有者挑战无效。')
  }
  return response.slice(1)
}

export async function ownerActionHash(opcode: number, flags: number, payload: Uint8Array) {
  if (!Number.isInteger(opcode) || opcode < 0 || opcode > 255 ||
      !Number.isInteger(flags) || flags < 0 || flags > 255 ||
      payload.length > OWNER_AUTH_MAX_ACTION_PAYLOAD) {
    throw new Error('所有者操作无效或参数过长。')
  }
  const header = new Uint8Array(4)
  header[0] = opcode
  header[1] = flags
  new DataView(header.buffer).setUint16(2, payload.length, true)
  return sha256(concat(header, payload))
}

async function proof(method: number, secret: Uint8Array, deviceId: Uint8Array,
                     nonce: Uint8Array, opcode: number, flags: number,
                     payload: Uint8Array) {
  if (deviceId.length !== 8 || !deviceId.some((value) => value !== 0) ||
      nonce.length !== 16 || !nonce.some((value) => value !== 0)) {
    throw new Error('设备身份或挑战无效。')
  }
  const keyDomain = method === OWNER_AUTH_PRO_CODE
    ? 'IKT-OWNER-PRO-v1' : 'IKT-OWNER-FIND-v1'
  const keyBytes = await sha256(concat(encoder.encode(keyDomain), secret))
  const actionHash = await ownerActionHash(opcode, flags, payload)
  const hmacKey = await crypto.subtle.importKey(
    'raw', buffer(keyBytes), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign'],
  )
  const message = concat(encoder.encode('IKT-OWNER-ACTION-v1'), deviceId, nonce, actionHash)
  const tag = new Uint8Array(await crypto.subtle.sign('HMAC', hmacKey, buffer(message)))
  return concat(new Uint8Array([OWNER_AUTH_VERSION, method]), actionHash, tag.subarray(0, 16))
}

export async function proveOwnerWithProCode(
  code: string, deviceId: Uint8Array, nonce: Uint8Array,
  opcode: number, flags: number, payload: Uint8Array,
) {
  const token = decodeActivationCode(code)
  if (deviceId.length !== 8 ||
      !deviceId.every((value, index) => value === token[index + 16])) {
    throw new Error('激活码不属于当前设备。')
  }
  return proof(OWNER_AUTH_PRO_CODE, token, deviceId, nonce, opcode, flags, payload)
}

export async function proveOwnerWithFindK(
  key: Uint8Array, deviceId: Uint8Array, nonce: Uint8Array,
  opcode: number, flags: number, payload: Uint8Array,
) {
  if (key.length !== 16 || !key.some((value) => value !== 0)) {
    throw new Error('Find 所有者凭据无效。')
  }
  return proof(OWNER_AUTH_FIND_K, key, deviceId, nonce, opcode, flags, payload)
}
