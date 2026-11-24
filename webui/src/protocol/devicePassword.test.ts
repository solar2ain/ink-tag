import { describe, expect, it } from 'vitest'
import {
  buildPasswordQrLink, createPasswordProof, decodeDevicePasswordStatus,
  derivePasswordCredential, derivePasswordChangeKey, encodePasswordBundle, generateDevicePassword,
  parsePasswordQrLink, validateDevicePassword, validatePasswordChange,
  RESET_SETTINGS_PROOF_DATA,
} from './devicePassword'

const bytes = (value: string) => Uint8Array.from(value.match(/../g) ?? [], (part) => parseInt(part, 16))
const hex = (value: Uint8Array) => Array.from(value, (v) => v.toString(16).padStart(2, '0')).join('')
const id = '0123456789ABCDEF'
const key = bytes('8a8b4a5c756384dbde8dfebb29190605a481d1b561ee8a11650c462cf5c348d3')
const challenge = bytes('010000112233445566778899aabbccddeeff07000000')
const changeKey = bytes('f8c1346b34188df9b4eb60e8ee784faae4a58af9ea31fe775299eba0ea84b834')

describe('pending device password codecs, not firmware qualification', () => {
  it('matches the Python vector and binds device, nonce and revision', async () => {
    expect(await derivePasswordCredential(id, 'Test1234')).toEqual(key)
    const proof = await createPasswordProof(id, key, challenge)
    expect(hex(proof)).toBe('01003fb33f5b1eee30cb72c64782efb4cb8a')
    expect(await createPasswordProof('0123456789ABCDE0', key, challenge)).not.toEqual(proof)
    const changed = challenge.slice(); changed[18] = 8
    expect(await createPasswordProof(id, key, changed)).not.toEqual(proof)
    changed[2] = 9
    expect(await createPasswordProof(id, key, changed)).not.toEqual(proof)
  })
  it('requires old password input and exact confirmation without trimming', () => {
    validatePasswordChange('Test1234', 'New12345', 'New12345')
    expect(() => validatePasswordChange('', 'New12345', 'New12345')).toThrow()
    expect(() => validatePasswordChange('Test1234', 'New12345', 'new12345')).toThrow()
    for (const value of ['', 'abc', 'a'.repeat(17), 'Test1234\n', ' abcd', '密码1234']) {
      expect(() => validateDevicePassword(value)).toThrow()
    }
  })
  it('imports only a candidate, removing credentials from the navigation URL', () => {
    const url = buildPasswordQrLink('IKT_TL_9903', id, key)
    expect(url).not.toContain('Test1234')
    expect(parsePasswordQrLink(url)).toEqual({ deviceId: id, credential: key, cleanUrl: 'https://solar2ain.github.io/ink-tag/?ble=IKT_TL_9903' })
    for (const invalid of [url.replace('solar2ain.github.io', 'example.com'), `${url}&auth=${hex(key)}`, url.replace(hex(key).toUpperCase(), '0'.repeat(64)), url.replace('https://', 'https://user@')]) {
      expect(() => parsePasswordQrLink(invalid)).toThrow()
    }
  })
  it('validates status, bounded bundles and padding', async () => {
    expect(decodeDevicePasswordStatus(bytes('010101051027000007000000'))).toEqual({ enabled: true, authorized: true, failures: 5, retryAfterMs: 10000, revision: 7 })
    expect(encodePasswordBundle(key, 49, new Uint8Array(301), changeKey).length).toBe(374)
    expect(encodePasswordBundle(null, 57, new Uint8Array(407), null, 8).length).toBe(480)
    expect(() => encodePasswordBundle(key, 53, new Uint8Array(352), changeKey)).toThrow()
    expect(encodePasswordBundle(null, 21, new Uint8Array(56))[1]).toBe(0)
    for (const value of [new Uint8Array(12), bytes('010101000000000000000000'), bytes('010001000000000000000000')]) {
      expect(() => decodeDevicePasswordStatus(value)).toThrow()
    }
    const invalid = new Uint8Array(56); invalid[55] = 1
    expect(() => encodePasswordBundle(key, 21, invalid)).toThrow()
    await expect(createPasswordProof(id, key, new Uint8Array(21))).rejects.toThrow()
  })
  it('binds change to W, purpose and exact candidate bytes, not the scanned V', async () => {
    expect(await derivePasswordChangeKey(id, 'Test1234')).toEqual(changeKey)
    const bundle = encodePasswordBundle(key, 21, new Uint8Array(56), changeKey, 8)
    const changeChallenge = challenge.slice(); changeChallenge[1] = 1
    const proof = await createPasswordProof(id, changeKey, changeChallenge, bundle)
    expect(hex(proof)).toBe('0101f8917f038d200729dd2586b4fde9f474474841a6fbafa7d25edbd2c8612edc3c9409a0a9a955c6c23f07beaa2667efaf')
    expect(await createPasswordProof(id, key, changeChallenge, bundle)).not.toEqual(proof)
    const changed = bundle.slice(); changed[5] = 9
    expect(await createPasswordProof(id, changeKey, changeChallenge, changed)).not.toEqual(proof)
    await expect(createPasswordProof(id, changeKey, changeChallenge)).rejects.toThrow()
    expect(() => encodePasswordBundle(key, 21, new Uint8Array(56), key, 8)).toThrow()
  })
  it('generates only supported lengths and characters', () => {
    for (const length of [4, 8, 16]) {
      const value = generateDevicePassword(length)
      expect(value.length).toBe(length)
      expect(validateDevicePassword(value)).toBe(value)
    }
    for (const length of [3, 17, 8.5]) expect(() => generateDevicePassword(length)).toThrow()
  })
  it('binds reset-settings to a fresh purpose2 W proof and the fixed action bytes', async () => {
    expect(hex(RESET_SETTINGS_PROOF_DATA)).toBe('494b542d52455345542d53455454494e47532d76310e0105')
    const resetChallenge = challenge.slice(); resetChallenge[1] = 2
    const proof = await createPasswordProof(id, changeKey, resetChallenge, RESET_SETTINGS_PROOF_DATA)
    expect(hex(proof)).toBe('0102b3c19dde69da6152490e42efa9d479ee7f9f07daeef071e6aedf0673e1b24655b40b6ab24aa42223759443274aabfb13')
    expect(await createPasswordProof(id, key, resetChallenge, RESET_SETTINGS_PROOF_DATA)).not.toEqual(proof)
    const altered = RESET_SETTINGS_PROOF_DATA.slice(); altered[altered.length - 1] = 6
    await expect(createPasswordProof(id, changeKey, resetChallenge, altered)).rejects.toThrow()
    resetChallenge[1] = 1
    await expect(createPasswordProof(id, changeKey, resetChallenge, RESET_SETTINGS_PROOF_DATA)).rejects.toThrow()
  })
})
