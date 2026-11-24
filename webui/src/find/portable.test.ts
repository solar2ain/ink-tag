import { afterEach, describe, expect, it, vi } from 'vitest'
import vector from '../../../tests/fixtures/find-portable-v1.json'
import replacementVector from '../../../tests/fixtures/find-owner-replacement-v2.json'
import { decryptPortableBackup, encryptPortableBackup, generateMigrationPassword, masterPublic, materialFingerprint,
  newPortableIdentity, verifyOwnerProof, ownerStage, replacementOwnerStage, readPortableBackup, plainPortableBackup, selectPortableBackup,
  type PortableBackup, type PortableIdentity } from './portable'
import { enrollOfflineFind, savedFindBackups, saveFindBackup, type OfflineFindDevice } from './offlineOwner'

const payload = vector.payload as PortableBackup
const identity = payload.identities[0] as PortableIdentity
const proof = vector.proofs[1]
afterEach(() => vi.restoreAllMocks())

describe('portable Find crypto and native Python interoperability', () => {
  it('matches the existing Python/firmware 113-byte authenticated replacement format', async () => {
    const previous = replacementVector.previous as PortableIdentity, next = replacementVector.next as PortableIdentity
    const stage = await replacementOwnerStage(previous, next, replacementVector.nonceHex)
    expect(stage).toBe(replacementVector.stageHex)
    expect(stage).toHaveLength(226)
    await expect(replacementOwnerStage(previous, { ...next, deviceId: 'activation:FFFFFFFFFFFFFFFF' }, replacementVector.nonceHex)).rejects.toThrow('不匹配')
    await expect(replacementOwnerStage({ ...previous, materialHex: next.materialHex }, next, replacementVector.nonceHex)).rejects.toThrow('不匹配')
    await expect(replacementOwnerStage(previous, next, '00')).rejects.toThrow('挑战')
  })
  it('decrypts a frozen Python AES-GCM backup, including Unicode', async () => {
    expect(await decryptPortableBackup(vector.encrypted, vector.password)).toEqual(payload)
    expect(await materialFingerprint(identity.materialHex)).toBe(identity.fingerprint)
    expect(Array.from(masterPublic(identity.materialHex), b => b.toString(16).padStart(2, '0')).join('')).toBe(vector.publicMasterHex)
    for (const p of vector.proofs) await verifyOwnerProof(identity, p)
  })
  it('produces the exact Python envelope with synthetic fixed salt and nonce', async () => {
    let call = 0
    vi.spyOn(crypto, 'getRandomValues').mockImplementation(value => {
      const array = value as Uint8Array
      const offset = call++ === 0 ? 0 : 16
      array.set(Array.from({ length: array.length }, (_, i) => i + offset))
      return value
    })
    expect(await encryptPortableBackup(payload, vector.password)).toBe(vector.encrypted)
  })
  it('uses different salts/nonces and never exposes the master in exported text', async () => {
    const a = await encryptPortableBackup(payload, vector.password), b = await encryptPortableBackup(payload, vector.password)
    expect(a).not.toBe(b); expect(a).not.toContain(identity.materialHex); expect(a).not.toContain(vector.password)
    expect(await decryptPortableBackup(a, vector.password)).toEqual(payload)
  })
  it('rejects wrong passwords, tampering, plaintext and short passwords', async () => {
    await expect(decryptPortableBackup(vector.encrypted, 'wrong-password-fixture')).rejects.toThrow('损坏')
    const i = 80, replacement = vector.encrypted[i] === 'A' ? 'B' : 'A'
    await expect(decryptPortableBackup(vector.encrypted.slice(0, i) + replacement + vector.encrypted.slice(i + 1), vector.password)).rejects.toThrow('损坏')
    await expect(decryptPortableBackup(JSON.stringify(payload), vector.password)).rejects.toThrow('迁移字符串')
    await expect(encryptPortableBackup(payload, 'short')).rejects.toThrow('6')
    await expect(verifyOwnerProof(identity, { ...proof, publicKeyHex: '00'.repeat(28) })).rejects.toThrow('不匹配')
  })
  it('accepts six-character passwords and retains older longer backups', async () => {
    expect(await decryptPortableBackup(vector.shortEncrypted, vector.shortPassword)).toEqual(payload)
    const encoded = await encryptPortableBackup(payload, '123456')
    expect(await decryptPortableBackup(encoded, '123456')).toEqual(payload)
    expect(await decryptPortableBackup(vector.encrypted, vector.password)).toEqual(payload)
  })
  it('generates twelve readable random characters without storing a passphrase', () => {
    const first = generateMigrationPassword(), second = generateMigrationPassword()
    expect(first).toMatch(/^[A-HJ-NP-Z2-9]{12}$/)
    expect(second).not.toBe(first)
    vi.spyOn(crypto, 'getRandomValues').mockImplementation(value => {
      const array = value as Uint8Array
      expect(array.length).toBe(12)
      array.fill(255)
      return value
    })
    expect(generateMigrationPassword()).toBe('9'.repeat(12))
  })
  it('creates a valid public-only identity/stage without sending the master', async () => {
    const entry = await newPortableIdentity(identity)
    expect(entry.fingerprint).not.toBe(identity.fingerprint)
    const stage = ownerStage(entry, '0102030405060708')
    expect(stage).toHaveLength(162); expect(stage.slice(2, 18)).toBe('0102030405060708')
    expect(stage).not.toContain(entry.materialHex.slice(0, 56))
  })
})

it('replaces offline only after old proof and saved new material, keeping the existing HMAC stage', async () => {
  const s = storage(), previous = replacementVector.previous as PortableIdentity, next = replacementVector.next as PortableIdentity
  saveFindBackup({ fingerprint: next.fingerprint, encryptedBackup: 'IKTFIND1.synthetic-new' }, s)
  const d = { ...device(), stagePublicOnlyFindOwnerReplacement: vi.fn().mockResolvedValue(undefined) }
  vi.mocked(d.readPublicOnlyFindOwnerStatus).mockResolvedValue({ ...vector.proofs[1], identityPresent: true, timeValid: true })
  vi.mocked(d.commitPublicOnlyFindOwner).mockResolvedValue(replacementVector.nextProof)
  await enrollOfflineFind(next, 'IKTFIND1.synthetic-new', d, () => undefined, s, false, previous)
  expect(d.stagePublicOnlyFindOwner).not.toHaveBeenCalled()
  expect(d.stagePublicOnlyFindOwnerReplacement).toHaveBeenCalledExactlyOnceWith(replacementVector.stageHex, replacementVector.nonceHex, previous.fingerprint)
  expect(d.configureFind).toHaveBeenCalledExactlyOnceWith(true)
  const wrong = { ...d, readPublicOnlyFindOwnerStatus: vi.fn().mockResolvedValue({ ...vector.proofs[1], publicKeyHex: '00'.repeat(28) }), beginPublicOnlyFindOwner: vi.fn() }
  await expect(enrollOfflineFind(next, 'IKTFIND1.synthetic-new', wrong, () => undefined, s, false, previous)).rejects.toThrow('不匹配')
  expect(wrong.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
})

function storage() {
  const map = new Map<string, string>()
  return { map, getItem: (key: string) => map.get(key) ?? null, setItem: (key: string, value: string) => { map.set(key, value) } }
}
function device(): OfflineFindDevice {
  return { beginPublicOnlyFindOwner: vi.fn().mockResolvedValue({ nonceHex: '0102030405060708' }),
    stagePublicOnlyFindOwner: vi.fn().mockResolvedValue(undefined), commitPublicOnlyFindOwner: vi.fn().mockResolvedValue(proof),
    readPublicOnlyFindOwnerStatus: vi.fn().mockResolvedValue({ ...proof, identityPresent: true, timeValid: true }),
    abortFindOwnerRotation: vi.fn().mockResolvedValue(undefined), configureFind: vi.fn().mockResolvedValue(undefined) }
}
describe('offline registration persistence and uncertain commit boundary', () => {
  it('permits confirmed plaintext enrollment but never persists plaintext or accepts unconfirmed/foreign material', async () => {
    const s = storage(), d = device(), text = plainPortableBackup(payload)
    await expect(enrollOfflineFind(identity, text, d, () => undefined, s)).rejects.toThrow('保存')
    expect(d.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
    await enrollOfflineFind(identity, text, d, () => undefined, s, true)
    expect(d.configureFind).toHaveBeenCalledExactlyOnceWith(true)
    expect(s.map.size).toBe(0)
    expect(() => saveFindBackup({ fingerprint: identity.fingerprint, encryptedBackup: text }, s)).toThrow('只保存加密')
    const other = { ...identity, deviceId: 'activation:1111111111111111' }
    const foreign = device()
    await expect(enrollOfflineFind(other, text, foreign, () => undefined, s, true)).rejects.toThrow('保存')
    expect(foreign.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
  })
  it('exports independent tracks without keys or hidden nested fields and requires plaintext consent', async () => {
    const withTracks = structuredClone(payload)
    withTracks.identities[0].tracks = [{ timestamp: '2026-01-01T00:00:00Z', receivedAt: '2026-01-01T00:00:00Z',
      latitude: 1, longitude: 2, horizontalAccuracy: 3, confidence: 0, status: 0, materialHex: identity.materialHex }]
    const archive = selectPortableBackup(withTracks, identity.deviceId, false, true)
    expect(JSON.stringify(archive)).not.toContain(identity.materialHex)
    expect(archive.identities[0].historyOnly).toBe(true)
    await expect(readPortableBackup(plainPortableBackup(archive), '')).rejects.toThrow('隐私')
    expect(await readPortableBackup(plainPortableBackup(archive), '', true)).toEqual(archive)
    expect(await decryptPortableBackup(await encryptPortableBackup(archive, '123456'), '123456')).toEqual(archive)
    expect(() => selectPortableBackup(withTracks, identity.deviceId, false, false)).toThrow('至少选择')
    expect(() => selectPortableBackup(withTracks, 'different-device', true, true)).toThrow('不属于')
  })
  it('refuses all BLE entry before an encrypted recovery copy exists', async () => {
    const s = storage(), d = device()
    await expect(enrollOfflineFind(identity, vector.encrypted, d, () => undefined, s)).rejects.toThrow('保存')
    expect(d.beginPublicOnlyFindOwner).not.toHaveBeenCalled()
    expect(() => saveFindBackup({ fingerprint: identity.fingerprint, encryptedBackup: vector.encrypted },
      { getItem: s.getItem, setItem: () => { throw Error('quota') } })).toThrow('未开始')
  })
  it('persists ciphertext only, verifies the actual derived proof, then enables', async () => {
    const s = storage(), d = device()
    saveFindBackup({ fingerprint: identity.fingerprint, encryptedBackup: vector.encrypted }, s)
    await enrollOfflineFind(identity, vector.encrypted, d, () => undefined, s)
    expect(d.configureFind).toHaveBeenCalledExactlyOnceWith(true)
    expect(JSON.stringify([...s.map.values()])).not.toContain(identity.materialHex)
    expect(savedFindBackups(s)[0].encryptedBackup).toBe(vector.encrypted)
  })
  it('does not replay an uncertain commit; recovers by one matching status proof', async () => {
    const s = storage(), d = device(); saveFindBackup({ fingerprint: identity.fingerprint, encryptedBackup: vector.encrypted }, s)
    vi.mocked(d.commitPublicOnlyFindOwner).mockRejectedValue(Error('unknown result'))
    await enrollOfflineFind(identity, vector.encrypted, d, () => undefined, s)
    expect(d.commitPublicOnlyFindOwner).toHaveBeenCalledOnce(); expect(d.abortFindOwnerRotation).not.toHaveBeenCalled()
    expect(d.readPublicOnlyFindOwnerStatus).toHaveBeenCalledOnce(); expect(d.configureFind).toHaveBeenCalledOnce()
  })
  it('retains ciphertext and refuses enable when both commit and status are unknown', async () => {
    const s = storage(), d = device(); saveFindBackup({ fingerprint: identity.fingerprint, encryptedBackup: vector.encrypted }, s)
    vi.mocked(d.commitPublicOnlyFindOwner).mockRejectedValue(Error('unknown result'))
    vi.mocked(d.readPublicOnlyFindOwnerStatus).mockRejectedValue(Error('disconnected'))
    await expect(enrollOfflineFind(identity, vector.encrypted, d, () => undefined, s)).rejects.toThrow()
    expect(d.configureFind).not.toHaveBeenCalled(); expect(d.abortFindOwnerRotation).not.toHaveBeenCalled()
    expect(savedFindBackups(s)).toHaveLength(1)
  })
  it('blocks a device change after BEGIN rather than staging/aborting on another device', async () => {
    const s = storage(), d = device(); saveFindBackup({ fingerprint: identity.fingerprint, encryptedBackup: vector.encrypted }, s)
    let current = true
    vi.mocked(d.beginPublicOnlyFindOwner).mockImplementation(async () => { current = false; return { nonceHex: '0102030405060708' } })
    await expect(enrollOfflineFind(identity, vector.encrypted, d, () => { if (!current) throw Error('target changed') }, s)).rejects.toThrow('target changed')
    expect(d.stagePublicOnlyFindOwner).not.toHaveBeenCalled(); expect(d.abortFindOwnerRotation).not.toHaveBeenCalled()
    expect(d.configureFind).not.toHaveBeenCalled()
  })
})
