import { describe, expect, it } from 'vitest'

import {
  decodeActivationCode,
  encodeActivationInstall,
  normalizeActivationCode,
  parseActivationRequest,
  parseActivationStatus,
} from './activation'

const CODE = 'SUEBAQAAAAEAAQAAAAEAAAEjRWeJq83viJmqu8zd7v-lpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWlpaWl'

describe('activation protocol', () => {
  it('keeps normal installation unchanged and restores only with an explicit mode', () => {
    expect(encodeActivationInstall(CODE)).toEqual(decodeActivationCode(CODE))
    const restore = encodeActivationInstall(CODE, true)
    expect(restore.length).toBe(97)
    expect(restore[0]).toBe(1)
    expect(restore.subarray(1)).toEqual(decodeActivationCode(CODE))
    expect(() => encodeActivationInstall('invalid', true)).toThrow()
  })
  it('decodes the canonical 96-byte IKAC1 token', () => {
    expect(normalizeActivationCode(`  ${CODE}\n`)).toBe(CODE)
    expect(Array.from(decodeActivationCode(CODE))).toEqual([
      0x49, 0x41, 1, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0,
      1, 35, 69, 103, 137, 171, 205, 239,
      136, 153, 170, 187, 204, 221, 238, 255,
      ...Array(64).fill(0xa5),
    ])
  })

  it('parses a provisioned free status', () => {
    const status = parseActivationStatus(new Uint8Array([
      2, 1, 1, 4, 1, 2, 128, 0,
      7, 0, 0, 0, 1, 0, 0, 0,
      1, 35, 69, 103, 137, 171, 205, 239,
    ]))
    expect(status).toMatchObject({
      phase: 'inactive', edition: 'base', activationId: '0123456789ABCDEF',
      scheme: 1, lastResult: 4, codeLength: 128, keyId: 1,
      sequence: 7, featureMask: 1, requestActive: true,
    })
  })

  it('parses a device-bound request with a challenge', () => {
    const request = parseActivationRequest(new Uint8Array([
      1, 1, 1, 0, 1, 0, 3, 0,
      1, 0, 0, 0, 7, 0, 0, 0,
      1, 35, 69, 103, 137, 171, 205, 239,
      136, 153, 170, 187, 204, 221, 238, 255,
    ]))
    expect(request).toEqual({
      scheme: 1,
      keyId: 1,
      productId: 1,
      securityEpoch: 3,
      supportedFeatures: 1,
      installedSequence: 7,
      deviceId: '0123456789ABCDEF',
      challenge: '8899AABBCCDDEEFF',
      requestCode: '0123456789ABCDEF:8899AABBCCDDEEFF',
    })
  })

  it('rejects malformed status and code', () => {
    expect(() => parseActivationStatus(new Uint8Array(13))).toThrow()
    expect(() => parseActivationRequest(new Uint8Array(31))).toThrow()
    expect(() => decodeActivationCode('TOO-SHORT')).toThrow()
    expect(() => decodeActivationCode(CODE.slice(0, -1) + '*')).toThrow()
  })
})
