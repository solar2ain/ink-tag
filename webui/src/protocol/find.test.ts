import { afterEach, describe, expect, it, vi } from 'vitest'
import { decodeFindCapabilities, decodeFindConfig, decodeFindOwnerRotationProof, decodeFindPublicOnlyStagePayload, decodeFindPublicOnlyReplacementStagePayload, decodeFindPublicOnlyOwnerStatus, encodeFindConfig, decodeFindOwnerRotationChallenge, normalizeFindPublicKey, supportsFindOwnerEnrollment, supportsFindOwnerReplacement } from './find'
import { InkTagClient } from './client'
import { CommandV2, RequestFlagV2 } from './generatedCommandsV2'
import { findClient } from '../find/client'
import { useDeviceStore } from '../store/device'

const key = '123456789ABCDEF0112233445566778899AABBCCDDEE0123456789AB'

describe('Find device protocol', () => {
  it('separates v2 first enrollment from qualified owner replacement', () => {
    const firstOnly = { flags: 0x140, normalWindowMs: 5000, findWindowMs: 1000,
      advertisementIntervalUnits: 800, publicOnlyP224: true, ownerRotation: false }
    expect(supportsFindOwnerEnrollment(firstOnly)).toBe(true)
    expect(supportsFindOwnerReplacement(firstOnly)).toBe(false)
    expect(supportsFindOwnerReplacement({ ...firstOnly, ownerRotation: true })).toBe(false)
    expect(supportsFindOwnerReplacement({ ...firstOnly, publicOnlyOwnerReplacement: true })).toBe(true)
    expect(supportsFindOwnerEnrollment(undefined)).toBe(false)
    expect(supportsFindOwnerReplacement(null)).toBe(false)
    expect(supportsFindOwnerEnrollment({ ...firstOnly, publicOnlyP224: false })).toBe(false)
    expect(supportsFindOwnerReplacement({ ...firstOnly, publicOnlyP224: false, ownerRotation: true })).toBe(false)
    expect(supportsFindOwnerEnrollment({ ...firstOnly, publicOnlyP224: false, ownerRotation: true })).toBe(false)
  })

  it('encodes only the current derived-identity enable state', () => {
    const payload = encodeFindConfig(true)
    expect(payload).toEqual(Uint8Array.from([1, 1, 1]))
    expect(normalizeFindPublicKey(key.match(/../g)!.join(':'))).toBe(key)
  })

  it('toggles an existing derived identity without carrying seed bytes', () => {
    expect(encodeFindConfig(true)).toEqual(Uint8Array.from([1, 1, 1]))
    expect(encodeFindConfig(false)).toEqual(Uint8Array.from([1, 0, 1]))
  })

  it('accepts only the current owner write challenge', () => {
    const nonce = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8])
    expect(decodeFindOwnerRotationChallenge(Uint8Array.from([1, ...nonce]))).toEqual({ nonceHex: '0102030405060708' })
    expect(() => decodeFindOwnerRotationChallenge(Uint8Array.from([2, ...nonce]))).toThrow('无效')
  })

  it('decodes fixed 5/1 time-multiplex capabilities and saved state', () => {
    const capabilities = new Uint8Array(16)
    const view = new DataView(capabilities.buffer)
    capabilities[0] = 1
    view.setUint32(1, 0x5E, true)
    view.setUint16(5, 5000, true)
    view.setUint16(7, 1000, true)
    view.setUint16(9, 0x0320, true)
    capabilities.set([0, 0, 0], 11)
    view.setUint16(14, 900, true)
    expect(decodeFindCapabilities(capabilities)).toMatchObject({ flags: 0x5E, normalWindowMs: 5000, findWindowMs: 1000, derivedIdentity: true, rotationPeriodSeconds: 900 })

    expect(decodeFindConfig(Uint8Array.from([1, 1, 1, 1, 1]))).toEqual({ enabled: true, mode: 1, identityPresent: true, pausedForConnection: true })
    const legacy = capabilities.slice()
    new DataView(legacy.buffer).setUint32(1, 0xBE, true)
    expect(() => decodeFindCapabilities(legacy)).toThrow('旧 Find 密钥格式')
    expect(() => decodeFindCapabilities(new Uint8Array(11))).toThrow('能力响应无效')
    expect(() => decodeFindConfig(new Uint8Array(34))).toThrow('配置响应无效')
  })

  it('rejects missing and sentinel keys', () => {
    expect(() => normalizeFindPublicKey('00')).toThrow('28 字节')
    expect(() => normalizeFindPublicKey('00'.repeat(28))).toThrow('无效')
    expect(() => normalizeFindPublicKey('FF'.repeat(28))).toThrow('无效')
  })

  it('recognizes public-only v2 without changing v1 owner proof decoding', () => {
    const capabilities = new Uint8Array(16)
    capabilities[0] = 1
    const view = new DataView(capabilities.buffer)
    view.setUint32(1, 0x140, true)
    view.setUint16(14, 900, true)
    expect(decodeFindCapabilities(capabilities).publicOnlyP224).toBe(true)

    const proof = new Uint8Array(53)
    proof[0] = 1
    new DataView(proof.buffer).setBigUint64(1, 1234n, true)
    proof.set(Uint8Array.from({ length: 28 }, (_, index) => index + 1), 9)
    proof.set(Uint8Array.from({ length: 16 }, (_, index) => index), 37)
    expect(decodeFindOwnerRotationProof(proof)).toEqual({
      slot: 1234,
      publicKeyHex: Array.from(proof.slice(9, 37), (value) => value.toString(16).padStart(2, '0')).join('').toUpperCase(),
      fingerprintHex: Array.from(proof.slice(37), (value) => value.toString(16).padStart(2, '0')).join('').toUpperCase(),
    })
    expect(() => decodeFindOwnerRotationProof(proof.slice(0, 52))).toThrow('无效')

    const stage = new Uint8Array(81)
    stage[0] = 1
    stage.set(Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]), 1)
    const stageHex = Array.from(stage, (value) => value.toString(16).padStart(2, '0')).join('')
    expect(decodeFindPublicOnlyStagePayload(stageHex, '0102030405060708')).toEqual(stage)
    expect(() => decodeFindPublicOnlyStagePayload(stageHex, '0000000000000000')).toThrow('不匹配')
  })

  it('gates and validates authenticated v2 owner replacement', () => {
    const capabilities = new Uint8Array(16)
    capabilities[0] = 1
    new DataView(capabilities.buffer).setUint32(1, 0x360, true)
    expect(decodeFindCapabilities(capabilities).publicOnlyOwnerReplacement).toBe(true)
    const status = new Uint8Array(62)
    status[0] = 1
    status[1] = 9
    new DataView(status.buffer).setBigUint64(10, 1234n, true)
    status.set(Uint8Array.from({ length: 16 }, (_, index) => index), 46)
    expect(decodeFindPublicOnlyOwnerStatus(status)).toMatchObject({
      identityPresent: true, timeValid: true, slot: 1234,
      fingerprintHex: '000102030405060708090A0B0C0D0E0F',
    })
    const stage = new Uint8Array(113)
    stage[0] = 1
    stage.set(Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]), 1)
    stage.set(status.subarray(46), 9)
    const hex = Array.from(stage, (value) => value.toString(16).padStart(2, '0')).join('')
    expect(decodeFindPublicOnlyReplacementStagePayload(hex, '0102030405060708', '000102030405060708090A0B0C0D0E0F')).toEqual(stage)
    expect(() => decodeFindPublicOnlyReplacementStagePayload(hex, '0000000000000000', '000102030405060708090A0B0C0D0E0F')).toThrow('不匹配')
  })
})

describe('unregistering a Find owner', () => {
  const deviceId = 'activation:0123456789ABCDEF'
  const fingerprint = '12'.repeat(16)
  const nonce = '0102030405060708'
  const payloadHex = `02${nonce}${fingerprint}${'34'.repeat(16)}`

  function setup(flags = 0x500) {
    const client = new InkTagClient()
    useDeviceStore.setState({ connection: 'connected', deviceId: 'synthetic-browser-id',
      activation: { ...useDeviceStore.getState().activation, activationId: deviceId.slice(11) },
      findCapabilities: { flags, normalWindowMs: 1, findWindowMs: 1, advertisementIntervalUnits: 1, publicOnlyP224: true },
    })
    const request = vi.spyOn(client as unknown as { requestV2: (op: number, data?: Uint8Array, flags?: number) => Promise<{ payload: Uint8Array }> }, 'requestV2')
    const readOwner = vi.spyOn(client, 'readPublicOnlyFindOwnerStatus').mockResolvedValueOnce({ identityPresent: true, fingerprintHex: fingerprint, publicKeyHex: '', timeValid: false, slot: 0 })
    const begin = vi.spyOn(client, 'beginPublicOnlyFindOwner').mockResolvedValue({ nonceHex: nonce })
    const prove = vi.spyOn(findClient, 'provePublicOnlyUnregister').mockResolvedValue({ ok: true, payloadHex })
    const config = vi.spyOn(client, 'readFindConfig').mockResolvedValue({ enabled: false, mode: 1, identityPresent: false, pausedForConnection: false })
    vi.spyOn(client, 'abortFindOwnerRotation').mockResolvedValue(undefined)
    return { client, request, readOwner, begin, prove, config }
  }

  afterEach(() => { vi.restoreAllMocks(); useDeviceStore.getState().resetDevice() })

  it('requires support and verifies a confirmed owner and advertising readback after one write', async () => {
    const { client, request, readOwner, prove, config } = setup()
    readOwner.mockResolvedValueOnce({ identityPresent: false, fingerprintHex: '', publicKeyHex: '', timeValid: false, slot: 0 })
    request.mockResolvedValueOnce({ payload: new Uint8Array() })
    await client.unregisterFindOwner(deviceId, fingerprint)
    expect(prove).toHaveBeenCalledExactlyOnceWith(fingerprint, nonce, deviceId)
    expect(request).toHaveBeenCalledExactlyOnceWith(CommandV2.FIND_ACTION,
      Uint8Array.from(payloadHex.match(/../g)!, pair => Number.parseInt(pair, 16)), RequestFlagV2.CONFIRM)
    expect(readOwner).toHaveBeenCalledTimes(2)
    expect(config).toHaveBeenCalledTimes(1)
  })

  it('does not accept a successful reply if the owner record remains', async () => {
    const { client, request, readOwner, config } = setup()
    readOwner.mockResolvedValueOnce({ identityPresent: true, fingerprintHex: fingerprint, publicKeyHex: '', timeValid: false, slot: 0 })
    request.mockResolvedValueOnce({ payload: new Uint8Array() })
    await expect(client.unregisterFindOwner(deviceId, fingerprint)).rejects.toThrow('无法确认')
    expect(config).not.toHaveBeenCalled()
    expect(request).toHaveBeenCalledTimes(1)
  })

  it('never writes on firmware that only supports replacing the owner', async () => {
    const { client, request, prove } = setup(0x300)
    await expect(client.unregisterFindOwner(deviceId, fingerprint)).rejects.toThrow('不支持')
    expect(request).not.toHaveBeenCalled()
    expect(prove).not.toHaveBeenCalled()
  })
})
