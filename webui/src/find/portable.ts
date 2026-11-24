/** Portable Find backup v1: PBKDF2-SHA256/600000 + AES-256-GCM, no plaintext storage. */
import { weierstrass } from '@noble/curves/abstract/weierstrass.js'
import { FIND_PUBLIC_ONLY_VERSION, type FindOwnerRotationProof } from '../protocol/find'

export const BACKUP_PREFIX = 'IKTFIND1.'
export const BACKUP_ITERATIONS = 600_000
export const MAX_BACKUP_TEXT = 12 * 1024 * 1024
const MAX_PLAIN = 8 * 1024 * 1024
const encoder = new TextEncoder()
const AAD = encoder.encode('InkTag/Find/portable-backup/v1')
export const ORDER = BigInt('0xffffffffffffffffffffffffffff16a2e0b8f03e13dd29455c5c2a3d')
// SEC 2 v2, secp224r1. Arithmetic is supplied by noble; no home-grown ECC.
const Point = weierstrass({
  p: BigInt('0xffffffffffffffffffffffffffffffff000000000000000000000001'),
  a: BigInt('0xfffffffffffffffffffffffffffffffefffffffffffffffffffffffe'),
  b: BigInt('0xb4050a850c04b3abf54132565044b0b7d7bfd8ba270b39432355ffb4'),
  n: ORDER, h: 1n,
  Gx: BigInt('0xb70e0cbd6bb4bf7f321390b94a03c1d356c21122343280d6115c1d21'),
  Gy: BigInt('0xbd376388b5f723fb4c22dfe6cd4375a05a07476444d5819985007e34'),
})

export interface PortableIdentity {
  historyOnly?: false
  fingerprint: string
  materialHex: string
  deviceId: string
  deviceName: string
  bluetoothName: string
  findNote: string
  createdAt: string
  derivationVersion: 2
  periodSeconds: 900
  tracks: unknown[]
}
export type PortableHistoryIdentity = Omit<PortableIdentity, 'materialHex' | 'historyOnly'> & { historyOnly: true; materialHex?: never }
export type PortableEntry = PortableIdentity | PortableHistoryIdentity
export interface PortableBackup {
  format: 'inktag-find-backup'
  version: 1
  selectedFingerprint: string
  identities: PortableEntry[]
}
export type OwnerMetadata = Pick<PortableIdentity, 'deviceId' | 'deviceName' | 'bluetoothName' | 'findNote'>
export const hex = (b: Uint8Array) => Array.from(b, n => n.toString(16).padStart(2, '0')).join('')
export function bytes(text: string) {
  if (!/^(?:[0-9a-fA-F]{2})+$/.test(text)) throw new Error('Find身份材料无效。')
  return Uint8Array.from(text.match(/../g)!, p => parseInt(p, 16))
}
const buffer = (b: Uint8Array) => Uint8Array.from(b).buffer
function concat(...parts: Uint8Array[]) {
  const result = new Uint8Array(parts.reduce((n, b) => n + b.length, 0))
  let offset = 0
  for (const part of parts) { result.set(part, offset); offset += part.length }
  return result
}
function number(b: Uint8Array) { return BigInt('0x' + hex(b)) }
function scalarBytes(n: bigint) { return bytes(n.toString(16).padStart(56, '0')) }
function b64(b: Uint8Array) {
  let s = ''
  for (let i = 0; i < b.length; i += 16384) s += String.fromCharCode(...b.subarray(i, i + 16384))
  return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}
function passwordBytes(password: string) {
  const encoded = encoder.encode(password)
  if (Array.from(password).length < 6 || Array.from(password).length > 512 ||
      new TextDecoder().decode(encoded) !== password) throw new Error('迁移口令需要6–512个字符。')
  if (!globalThis.crypto?.subtle) throw new Error('此浏览器不能安全加密，请使用HTTPS或支持的本地浏览器。')
  return encoded
}
export function generateMigrationPassword() {
  if (!globalThis.crypto?.getRandomValues) throw new Error('此浏览器不能安全生成口令，请手动输入。')
  // 32 symbols: each uniformly random byte contributes five unbiased bits.
  // Avoid 0/O and 1/I; generation never uses Math.random or persists the result.
  const alphabet = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
  return Array.from(crypto.getRandomValues(new Uint8Array(12)), byte => alphabet[byte & 31]).join('')
}
async function keyFor(password: string, salt: Uint8Array) {
  const raw = await crypto.subtle.importKey('raw', buffer(passwordBytes(password)), 'PBKDF2', false, ['deriveKey'])
  return crypto.subtle.deriveKey({ name: 'PBKDF2', hash: 'SHA-256', salt: buffer(salt), iterations: BACKUP_ITERATIONS },
    raw, { name: 'AES-GCM', length: 256 }, false, ['encrypt', 'decrypt'])
}

export async function encryptPortableBackup(payload: PortableBackup, password: string) {
  passwordBytes(password)
  const plain = encoder.encode(JSON.stringify(payload))
  if (plain.length > MAX_PLAIN) throw new Error('Find备份超过8MiB。')
  const salt = crypto.getRandomValues(new Uint8Array(16)), iv = crypto.getRandomValues(new Uint8Array(12))
  const key = await keyFor(password, salt)
  const cipher = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-GCM', iv: buffer(iv), additionalData: buffer(AAD), tagLength: 128 }, key, buffer(plain)))
  plain.fill(0)
  return BACKUP_PREFIX + b64(concat(new Uint8Array([1]), salt, iv, cipher))
}

export async function decryptPortableBackup(text: string, password: string): Promise<PortableBackup> {
  passwordBytes(password)
  if (text.length > MAX_BACKUP_TEXT) throw new Error('迁移资料过大。')
  text = text.trim()
  if (!/^IKTFIND1\.[A-Za-z0-9_-]+$/.test(text)) throw new Error('请提供完整的InkTag加密迁移字符串。')
  try {
    const encoded = text.slice(BACKUP_PREFIX.length)
    const raw = Uint8Array.from(atob(encoded.replace(/-/g, '+').replace(/_/g, '/')), c => c.charCodeAt(0))
    if (raw.length < 46 || raw.length > MAX_PLAIN + 45 || raw[0] !== 1 || b64(raw) !== encoded) throw new Error()
    const iv = raw.slice(17, 29), key = await keyFor(password, raw.slice(1, 17))
    const plain = await crypto.subtle.decrypt({ name: 'AES-GCM', iv: buffer(iv), additionalData: buffer(AAD), tagLength: 128 }, key, buffer(raw.slice(29)))
    const payload = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(plain)) as PortableBackup
    new Uint8Array(plain).fill(0)
    return await validatePortablePayload(payload)
  } catch { throw new Error('迁移口令不正确，或迁移资料已损坏。') }
}

async function validatePortablePayload(payload: PortableBackup): Promise<PortableBackup> {
    if (!payload || payload.format !== 'inktag-find-backup' || payload.version !== 1 || !Array.isArray(payload.identities) ||
        !payload.identities.length || payload.identities.length > 256) throw new Error()
    const seen = new Set<string>()
    let points = 0
    for (const entry of payload.identities) {
      if (entry.derivationVersion !== 2 || entry.periodSeconds !== 900 || seen.has(entry.fingerprint) || !/^[0-9a-f]{32}$/.test(entry.fingerprint)) throw new Error()
      if (entry.historyOnly === true) {
        if ('materialHex' in entry) throw new Error()
      } else if ((entry.historyOnly !== undefined && entry.historyOnly !== false) || !entry.materialHex || !/^[0-9a-fA-F]{88}$/.test(entry.materialHex) ||
          await materialFingerprint(entry.materialHex) !== entry.fingerprint) throw new Error()
      seen.add(entry.fingerprint)
      for (const field of ['deviceId', 'deviceName', 'bluetoothName', 'findNote'] as const) {
        if (typeof entry[field] !== 'string' || Array.from(entry[field]).length > (field === 'deviceId' ? 128 : 64) || /[\x00-\x1f]/.test(entry[field])) throw new Error()
      }
      if (typeof entry.createdAt !== 'string' || !Number.isFinite(Date.parse(entry.createdAt)) || !Array.isArray(entry.tracks)) throw new Error()
      points += entry.tracks.length
      if (points > 100000) throw new Error()
      entry.tracks = entry.tracks.map(normalizeTrack)
    }
    if (typeof payload.selectedFingerprint !== 'string' || (payload.selectedFingerprint && !seen.has(payload.selectedFingerprint))) throw new Error()
    return payload
}

export async function readPortableBackup(text: string, password: string, allowPlaintext = false): Promise<PortableBackup> {
  if (!text.trimStart().startsWith('{')) return decryptPortableBackup(text, password)
  if (!allowPlaintext) throw new Error('此资料未加密，请先确认隐私泄露风险。')
  if (text.length > MAX_BACKUP_TEXT || encoder.encode(text).length > MAX_PLAIN) throw new Error('迁移资料过大。')
  try { return await validatePortablePayload(JSON.parse(text)) }
  catch { throw new Error('未加密的Find备份格式无效。') }
}
export function plainPortableBackup(payload: PortableBackup) {
  const text = JSON.stringify(payload)
  if (encoder.encode(text).length > MAX_PLAIN) throw new Error('Find备份超过8MiB。')
  return text
}

function normalizeTrack(value: unknown) {
  if (!value || typeof value !== 'object') throw new Error('备份轨迹无效。')
  const p = value as Record<string, unknown>, out: Record<string, unknown> = {}
  for (const field of ['timestamp', 'receivedAt']) {
    const v = p[field]
    if (typeof v !== 'string' || !Number.isFinite(Date.parse(v))) throw new Error('备份时间无效。')
    out[field] = v
  }
  for (const [field, lo, hi] of [['latitude', -90, 90], ['longitude', -180, 180], ['horizontalAccuracy', 0, 1e9]] as const) {
    const v = p[field]
    if (typeof v !== 'number' || !Number.isFinite(v) || v < lo || v > hi) throw new Error('备份坐标无效。')
    out[field] = v
  }
  for (const field of ['confidence', 'status', 'sourceSlot']) {
    const v = p[field] ?? (field === 'sourceSlot' ? -1 : 0)
    if (typeof v !== 'number' || !Number.isSafeInteger(v) || v < -1) throw new Error('备份状态无效。')
    out[field] = v
  }
  return out
}

/** Explicit allowlist: a tracks-only backup cannot retain keys in nested extra fields. */
export function selectPortableBackup(payload: PortableBackup, deviceId: string, includeKeys: boolean, includeHistory: boolean): PortableBackup {
  if (!includeKeys && !includeHistory) throw new Error('请至少选择设备资料或历史轨迹。')
  const identities = payload.identities.filter(e => e.deviceId.toLowerCase() === deviceId.toLowerCase()).map(e => {
    const base = { fingerprint: e.fingerprint, deviceId: e.deviceId, deviceName: e.deviceName, bluetoothName: e.bluetoothName,
      findNote: e.findNote, createdAt: e.createdAt, derivationVersion: e.derivationVersion, periodSeconds: e.periodSeconds,
      tracks: includeHistory ? e.tracks.map(normalizeTrack) : [] }
    return includeKeys && !e.historyOnly ? { ...base, materialHex: e.materialHex } : { ...base, historyOnly: true as const }
  })
  if (!identities.length) throw new Error('迁移资料不属于此设备。')
  return { format: 'inktag-find-backup', version: 1, identities, selectedFingerprint: identities.some(e => e.fingerprint === payload.selectedFingerprint) ? payload.selectedFingerprint : identities[0].fingerprint }
}

export function masterPublic(materialHex: string) {
  const material = bytes(materialHex), scalar = number(material.slice(0, 28))
  if (material.length !== 44 || scalar < 1n || scalar >= ORDER ||
      material.slice(28).every(n => n === 0) || material.slice(28).every(n => n === 255)) throw new Error('Find身份材料无效。')
  return Point.BASE.multiply(scalar).toBytes(false).slice(1)
}
export async function materialFingerprint(materialHex: string) {
  const material = bytes(materialHex), pub = masterPublic(materialHex)
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', buffer(concat(encoder.encode('InkTag/Find/public-only/v2'), pub, material.slice(28)))))).slice(0, 32)
}
export async function newPortableIdentity(metadata: OwnerMetadata): Promise<PortableIdentity> {
  if (!globalThis.crypto?.subtle) throw new Error('此浏览器不能安全加密，请使用HTTPS或支持的本地浏览器。')
  if (!/^activation:(?!0{16}$)[0-9A-Fa-f]{16}$/.test(metadata.deviceId)) throw new Error('无法确认设备固定身份。')
  for (const name of ['deviceId', 'deviceName', 'bluetoothName', 'findNote'] as const) {
    const field = metadata[name]
    if (typeof field !== 'string' || Array.from(field).length > (name === 'deviceId' ? 128 : 64) || /[\x00-\x1f]/.test(field)) throw new Error('标签名称或身份格式无效。')
  }
  let scalar: Uint8Array, k: Uint8Array
  do { scalar = crypto.getRandomValues(new Uint8Array(28)) } while (number(scalar) === 0n || number(scalar) >= ORDER)
  do { k = crypto.getRandomValues(new Uint8Array(16)) } while (k.every(n => n === 0) || k.every(n => n === 255))
  const materialHex = hex(concat(scalar, k))
  scalar.fill(0); k.fill(0)
  return { deviceId: metadata.deviceId, deviceName: metadata.deviceName, bluetoothName: metadata.bluetoothName,
    findNote: metadata.findNote, fingerprint: await materialFingerprint(materialHex), materialHex,
    derivationVersion: 2, periodSeconds: 900, createdAt: new Date().toISOString(), tracks: [] }
}
export function singleIdentityBackup(identity: PortableIdentity): PortableBackup {
  return { format: 'inktag-find-backup', version: 1, identities: [identity], selectedFingerprint: identity.fingerprint }
}
export function ownerStage(identity: PortableIdentity, nonceHex: string) {
  if (!/^[0-9a-fA-F]{16}$/.test(nonceHex)) throw new Error('设备所有者挑战无效。')
  return hex(concat(new Uint8Array([FIND_PUBLIC_ONLY_VERSION]), bytes(nonceHex), masterPublic(identity.materialHex), bytes(identity.materialHex).slice(28)))
}
export async function replacementOwnerStage(previous: PortableIdentity, next: PortableIdentity, nonceHex: string) {
  if (previous.deviceId.toLowerCase() !== next.deviceId.toLowerCase() ||
      !/^activation:(?!0{16}$)[0-9A-Fa-f]{16}$/.test(next.deviceId) ||
      previous.fingerprint === next.fingerprint ||
      await materialFingerprint(previous.materialHex) !== previous.fingerprint ||
      await materialFingerprint(next.materialHex) !== next.fingerprint)
    throw new Error('原身份资料与当前设备不匹配。')
  // Existing firmware v2: old-K HMAC binds the fixed device, fresh nonce and new P/K.
  const initial = bytes(ownerStage(next, nonceHex))
  const stage = concat(initial.slice(0, 9), bytes(previous.fingerprint), initial.slice(9))
  const oldKey = bytes(previous.materialHex).slice(28)
  try {
    const key = await crypto.subtle.importKey('raw', buffer(oldKey), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign'])
    const tag = new Uint8Array(await crypto.subtle.sign('HMAC', key, buffer(concat(
      encoder.encode('InkTag/Find/public-only/replace/v1\0'), bytes(next.deviceId.slice(11)), stage))))
    return hex(concat(stage, tag.slice(0, 16)))
  } finally { oldKey.fill(0) }
}
async function coefficient(key: CryptoKey, slot: number, retry: number, role: string) {
  const position = new Uint8Array(8), counter = new Uint8Array(4)
  new DataView(position.buffer).setBigUint64(0, BigInt(slot), false)
  new DataView(counter.buffer).setUint32(0, retry, false)
  const prefix = concat(encoder.encode('InkTag/Find/public-only/v2\0' + role), position, counter)
  for (let sample = 0; sample <= 0xffffffff; sample++) {
    const tail = new Uint8Array(4); new DataView(tail.buffer).setUint32(0, sample, false)
    const value = number(new Uint8Array(await crypto.subtle.sign('HMAC', key, buffer(concat(prefix, tail)))).slice(0, 28))
    if (value < ORDER && (role === 'v' || value !== 0n)) return value
  }
  throw new Error('Find派生计数超限。')
}
export async function verifyOwnerProof(identity: PortableIdentity, proof: FindOwnerRotationProof) {
  if (!Number.isSafeInteger(proof.slot) || proof.slot < 0 || proof.fingerprintHex?.toLowerCase() !== identity.fingerprint) throw new Error('设备登记的身份不匹配；迁移资料已保留。')
  const material = bytes(identity.materialHex), master = number(material.slice(0, 28))
  const key = await crypto.subtle.importKey('raw', buffer(material.slice(28)), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign'])
  for (let retry = 0; retry <= 0xffffffff; retry++) {
    const scalar = ((await coefficient(key, proof.slot, retry, 'u')) * master + (await coefficient(key, proof.slot, retry, 'v'))) % ORDER
    if (!scalar) continue
    const actual = hex(Point.BASE.multiply(number(scalarBytes(scalar))).toBytes(false).slice(1, 29))
    if (actual !== proof.publicKeyHex.toLowerCase()) throw new Error('设备提交的派生公钥不匹配；迁移资料已保留。')
    return
  }
  throw new Error('Find派生计数超限。')
}
