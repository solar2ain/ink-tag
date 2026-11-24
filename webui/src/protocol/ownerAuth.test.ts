import { describe, expect, it } from 'vitest'
import { ownerActionHash, parseOwnerChallenge, proveOwnerWithFindK } from './ownerAuth'

const bytes = (hex: string) => Uint8Array.from(hex.match(/../g) ?? [], (part) => parseInt(part, 16))

describe('owner action proof', () => {
  it('matches the frozen Find K wire vector', async () => {
    const deviceId = bytes('0123456789abcdef')
    const nonce = bytes('00112233445566778899aabbccddeeff')
    const key = bytes('0102030405060708090a0b0c0d0e0f10')
    const action = bytes('03')
    const hash = await ownerActionHash(0x0e, 0x01, action)
    expect(Array.from(hash, (byte) => byte.toString(16).padStart(2, '0')).join(''))
      .toBe('0479e61fe14663df4b27373d467ba01bbac1f1991d41baca562a64cecdafb6e3')
    const proof = await proveOwnerWithFindK(key, deviceId, nonce, 0x0e, 0x01, action)
    expect(Array.from(proof, (byte) => byte.toString(16).padStart(2, '0')).join(''))
      .toBe('01020479e61fe14663df4b27373d467ba01bbac1f1991d41baca562a64cecdafb6e3edae8e05f81ed31efca54d1b0784d082')
    expect(parseOwnerChallenge(new Uint8Array([1, ...nonce]))).toEqual(nonce)
  })

  it('rejects an empty or malformed device challenge', () => {
    expect(() => parseOwnerChallenge(new Uint8Array(17))).toThrow()
    expect(() => parseOwnerChallenge(new Uint8Array([2, ...bytes('00112233445566778899aabbccddeeff')]))).toThrow()
  })
})
