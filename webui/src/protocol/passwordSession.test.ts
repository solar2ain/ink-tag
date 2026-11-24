import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import { InkTagClient, ProtocolV2Error } from './client'
import { CapabilityV2, CommandV2, StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'
import { derivePasswordChangeKey, derivePasswordCredential } from './devicePassword'
import { getPasswordCredential, savePasswordCredential } from '../data/passwordCredentials'

describe('password client session, simulated device only', () => {
  afterEach(() => vi.restoreAllMocks())
  beforeEach(() => {
    Object.defineProperty(globalThis, 'window', { configurable: true, value: globalThis })
    const memory = new Map<string,string>()
    Object.defineProperty(globalThis, 'localStorage', { configurable: true, value: { getItem: (key:string) => memory.get(key) ?? null, setItem: (key:string,value:string) => memory.set(key,value), removeItem: (key:string) => memory.delete(key) } })
    useDeviceStore.getState().resetDevice()
    useDeviceStore.getState().setActivation({ ...useDeviceStore.getState().activation, activationId: '0123456789ABCDEF' })
  })
  it('rejects optional reads/writes before authentication without any BLE send', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { protocolV2: boolean; v2Capabilities: number; requestV2: (op:number) => Promise<unknown>; writePacket: ReturnType<typeof vi.fn> }
    internal.protocolV2 = true; internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    internal.writePacket = vi.fn()
    await expect(internal.requestV2(CommandV2.SET_TIME)).rejects.toThrow('验证设备密码')
    await expect(internal.requestV2(CommandV2.GET_SETTINGS)).rejects.toThrow('验证设备密码')
    expect(internal.writePacket).not.toHaveBeenCalled()
  })
  it('stores V only after authorization and never substitutes it for the old password', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2: ReturnType<typeof vi.fn>; finishPasswordConnection: ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    // OS retained an authorized LL link: this browser must still prove its V.
    const status = new Uint8Array([1,1,1,0,0,0,0,0,1,0,0,0])
    const authorized = status.slice(); authorized[2] = 1
    internal.requestV2 = vi.fn().mockResolvedValueOnce({ payload:status }).mockResolvedValueOnce({ payload:new Uint8Array([1,0,...Array.from({length:16},(_,i)=>i+1),1,0,0,0]) }).mockResolvedValueOnce({ payload:authorized })
    internal.finishPasswordConnection = vi.fn().mockResolvedValue(undefined)
    await device.authenticateDevicePassword('Old12345')
    expect(getPasswordCredential('0123456789ABCDEF')).toEqual(await derivePasswordCredential('0123456789ABCDEF','Old12345'))
    internal.requestV2.mockResolvedValue({ payload:authorized })
    await expect(device.configureDevicePassword('New12345')).rejects.toThrow('原密码')
    expect(internal.requestV2.mock.calls.some(([op])=>op===CommandV2.TRANSFER_BEGIN)).toBe(false)
  })
  it('keeps local V unchanged when auth is rejected and rate-limits the next proof', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2: ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    const old = await derivePasswordCredential('0123456789ABCDEF','Old12345')
    savePasswordCredential('0123456789ABCDEF',old)
    const status = new Uint8Array([1,1,0,0,0,0,0,0,1,0,0,0])
    internal.requestV2 = vi.fn().mockResolvedValueOnce({payload:status}).mockResolvedValueOnce({payload:new Uint8Array([1,0,...Array.from({length:16},(_,i)=>i+1),1,0,0,0])}).mockRejectedValueOnce(new Error('denied'))
    await expect(device.authenticateDevicePassword('New12345', undefined, false)).rejects.toThrow('denied')
    expect(getPasswordCredential('0123456789ABCDEF')).toEqual(old)
    internal.requestV2.mockResolvedValue({payload:status})
    await expect(device.authenticateDevicePassword('New12345')).rejects.toThrow('频繁')
    expect(internal.requestV2.mock.calls.filter(([op])=>op===CommandV2.AUTHORIZE_DEVICE_PASSWORD)).toHaveLength(1)
  })
  it('keeps an unchecked credential in this session only and clears a prior local record after AUTH', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2:ReturnType<typeof vi.fn>; finishPasswordConnection:ReturnType<typeof vi.fn>; activePasswordCredential:Uint8Array | null }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    savePasswordCredential('0123456789ABCDEF', new Uint8Array(32).fill(7))
    internal.requestV2 = vi.fn().mockResolvedValueOnce({payload:new Uint8Array([1,1,0,0,0,0,0,0,1,0,0,0])})
      .mockResolvedValueOnce({payload:new Uint8Array([1,0,...new Array(16).fill(1),1,0,0,0])})
      .mockResolvedValueOnce({payload:new Uint8Array([1,1,1,0,0,0,0,0,1,0,0,0])})
    internal.finishPasswordConnection = vi.fn().mockResolvedValue(undefined)
    await device.authenticateDevicePassword('Old12345', undefined, false)
    expect(device.passwordStatus?.authorized).toBe(true)
    expect(getPasswordCredential('0123456789ABCDEF')).toBeNull()
    expect(internal.activePasswordCredential).toEqual(await derivePasswordCredential('0123456789ABCDEF', 'Old12345'))
    internal.requestV2.mockResolvedValue({payload:new Uint8Array([1,1,1,0,0,0,0,0,1,0,0,0])})
    await expect(device.configureDevicePassword(null)).rejects.toThrow('原密码')
    device.disconnect()
    expect(internal.activePasswordCredential).toBeNull()
  })
  it('stages one bounded QR bundle and confirms new V before local replacement', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2: ReturnType<typeof vi.fn>; provePassword:ReturnType<typeof vi.fn>; writeV2TransferChunk: ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    useDeviceStore.getState().setIdentity({deviceName:'IKT_TL_ACEE'})
    let committed = false
    internal.provePassword = vi.fn().mockImplementation(async () => { device.passwordStatus = { enabled:true,authorized:true,revision:1,failures:0,retryAfterMs:0 } })
    internal.writeV2TransferChunk = vi.fn().mockResolvedValue(undefined)
    internal.requestV2 = vi.fn().mockImplementation(async (op:number) => {
      if (op===CommandV2.GET_DEVICE_PASSWORD_STATUS) return {payload:new Uint8Array(committed ? [1,1,0,0,0,0,0,0,1,0,0,0] : [1,0,0,0,0,0,0,0,0,0,0,0])}
      if (op===CommandV2.TRANSFER_BEGIN) return {payload:new Uint8Array([1,7,64,0])}
      if (op===CommandV2.TRANSFER_COMMIT) committed = true
      return {payload:new Uint8Array()}
    })
    await device.configureDevicePassword('New12345')
    const begin = internal.requestV2.mock.calls.find(([op]) => op===CommandV2.TRANSFER_BEGIN)![1] as Uint8Array
    expect(begin[0]).toBe(7)
    expect(new DataView(begin.buffer).getUint32(4,true)).toBeLessThanOrEqual(420)
    expect(internal.provePassword).toHaveBeenCalledTimes(1)
    expect(getPasswordCredential('0123456789ABCDEF')).toBeNull()
    expect((device as unknown as { activePasswordCredential: Uint8Array }).activePasswordCredential)
      .toEqual(await derivePasswordCredential('0123456789ABCDEF','New12345'))
  })
  it.each([0, 5000, 60000])('uses device cooldown %i without adding 30 seconds after a typo', async (cooldown) => {
    vi.spyOn(Date, 'now').mockReturnValue(100000)
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; passwordAttemptAfter:number; requestV2: ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    const status = new Uint8Array([1,1,1,5,0,0,0,0,1,0,0,0])
    const rejected = status.slice()
    new DataView(rejected.buffer).setUint32(4, cooldown, true)
    internal.requestV2 = vi.fn().mockResolvedValueOnce({payload:status})
      .mockResolvedValueOnce({payload:new Uint8Array([1,0,...Array.from({length:16},(_,i)=>i+1),1,0,0,0])})
      .mockRejectedValueOnce(new ProtocolV2Error(StatusV2.DENIED))
      .mockResolvedValueOnce({payload:rejected})
    await expect(device.authenticateDevicePassword('Wrong123')).rejects.toBeInstanceOf(ProtocolV2Error)
    expect(internal.passwordAttemptAfter).toBe(100000 + Math.max(1000, cooldown))
    expect(device.passwordStatus?.authorized).toBe(false)
    expect(internal.requestV2.mock.calls.filter(([op])=>op===CommandV2.AUTHORIZE_DEVICE_PASSWORD)).toHaveLength(1)
    expect(getPasswordCredential('0123456789ABCDEF')).toBeNull()
  })
  it('does not guess a cooldown or retry an ambiguous AUTH response', async () => {
    vi.spyOn(Date, 'now').mockReturnValue(100000)
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; passwordAttemptAfter:number; requestV2: ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    internal.requestV2 = vi.fn().mockResolvedValueOnce({payload:new Uint8Array([1,1,1,0,0,0,0,0,1,0,0,0])})
      .mockResolvedValueOnce({payload:new Uint8Array([1,0,...Array.from({length:16},(_,i)=>i+1),1,0,0,0])})
      .mockRejectedValueOnce(new Error('AUTH response lost'))
    await expect(device.authenticateDevicePassword('Old12345')).rejects.toThrow('AUTH response lost')
    expect(internal.passwordAttemptAfter).toBe(101000)
    expect(internal.requestV2).toHaveBeenCalledTimes(3)
    expect(device.passwordStatus?.authorized).toBe(false)
  })
  it('does not authorize/save after a connection changes during the AUTH response', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; connectionGeneration:number; requestV2: ReturnType<typeof vi.fn>; finishPasswordConnection: ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    const status = new Uint8Array([1,1,0,0,0,0,0,0,1,0,0,0])
    internal.requestV2 = vi.fn().mockResolvedValueOnce({payload:status})
      .mockResolvedValueOnce({payload:new Uint8Array([1,0,...Array.from({length:16},(_,i)=>i+1),1,0,0,0])})
      .mockImplementationOnce(async () => {
        internal.connectionGeneration++
        device.passwordStatus = null
        useDeviceStore.getState().setActivation({ ...useDeviceStore.getState().activation, activationId:'FEDCBA9876543210' })
        const authorized = status.slice(); authorized[2] = 1
        return {payload:authorized}
      })
    internal.finishPasswordConnection = vi.fn()
    await expect(device.authenticateDevicePassword('Old12345')).rejects.toThrow('连接已改变')
    expect(device.passwordStatus).toBeNull()
    expect(getPasswordCredential('0123456789ABCDEF')).toBeNull()
    expect(getPasswordCredential('FEDCBA9876543210')).toBeNull()
    expect(internal.finishPasswordConnection).not.toHaveBeenCalled()
  })
  it.each([null, 'New12345'])('requires fresh old-password W even when V already authorized, new=%s', async (newPassword) => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2:ReturnType<typeof vi.fn>; provePassword:ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    internal.requestV2 = vi.fn().mockResolvedValue({payload:new Uint8Array([1,1,1,0,0,0,0,0,1,0,0,0])})
    internal.provePassword = vi.fn().mockRejectedValue(new Error('old password rejected'))
    await expect(device.configureDevicePassword(newPassword, 'Wrong123')).rejects.toThrow('old password rejected')
    const [proofKey, purpose, bundle] = internal.provePassword.mock.calls[0]
    expect(proofKey).toEqual(await derivePasswordChangeKey('0123456789ABCDEF', 'Wrong123'))
    expect(proofKey).not.toEqual(await derivePasswordCredential('0123456789ABCDEF', 'Wrong123'))
    expect(purpose).toBe(newPassword ? 1 : 2)
    expect(bundle[1]).toBe(newPassword ? 1 : 0)
    expect(internal.requestV2.mock.calls.some(([op]) => op === CommandV2.TRANSFER_BEGIN)).toBe(false)
  })
  it('preserves an authorized session when browser storage is unavailable', async () => {
    Object.defineProperty(globalThis, 'localStorage', {configurable:true, value:{
      getItem:()=>{ throw new Error('storage blocked') }, setItem:()=>{ throw new Error('storage blocked') },
    }})
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2:ReturnType<typeof vi.fn>; finishPasswordConnection:ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    const status = new Uint8Array([1,1,1,0,0,0,0,0,1,0,0,0])
    internal.requestV2 = vi.fn().mockResolvedValueOnce({payload:status})
      .mockResolvedValueOnce({payload:new Uint8Array([1,0,...Array.from({length:16},(_,i)=>i+1),1,0,0,0])})
      .mockResolvedValueOnce({payload:status})
    internal.finishPasswordConnection = vi.fn().mockResolvedValue(undefined)
    await device.authenticateDevicePassword('Old12345')
    expect(useDeviceStore.getState().connection).toBe('connected')
    expect(getPasswordCredential('0123456789ABCDEF')).toBeNull()
    expect(device.passwordStatus?.authorized).toBe(true)
  })
  it('does not abort or retry a commit whose outcome is unknown', async () => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; requestV2:ReturnType<typeof vi.fn>; writeV2TransferChunk:ReturnType<typeof vi.fn> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
    const old = await derivePasswordCredential('0123456789ABCDEF','Old12345')
    savePasswordCredential('0123456789ABCDEF',old)
    internal.writeV2TransferChunk = vi.fn().mockResolvedValue(undefined)
    internal.requestV2 = vi.fn().mockImplementation(async (op:number) => {
      if (op===CommandV2.GET_DEVICE_PASSWORD_STATUS) return {payload:new Uint8Array([1,0,0,0,0,0,0,0,0,0,0,0])}
      if (op===CommandV2.TRANSFER_BEGIN) return {payload:new Uint8Array([1,7,64,0])}
      if (op===CommandV2.TRANSFER_COMMIT) throw new Error('commit response lost')
      throw new Error('unexpected request')
    })
    await expect(device.configureDevicePassword('New12345')).rejects.toThrow('commit response lost')
    expect(getPasswordCredential('0123456789ABCDEF')).toEqual(old)
    expect(internal.requestV2.mock.calls.filter(([op])=>op===CommandV2.TRANSFER_COMMIT)).toHaveLength(1)
    expect(internal.requestV2.mock.calls.some(([op])=>op===CommandV2.TRANSFER_ABORT)).toBe(false)
  })
  it('keeps the QR bearer separate from the managed NFC URL after rename', async () => {
    const device = new InkTagClient()
    const key = await derivePasswordCredential('0123456789ABCDEF','Old12345')
    savePasswordCredential('0123456789ABCDEF',key)
    device.passwordStatus = {enabled:true,authorized:true,failures:0,retryAfterMs:0,revision:1}
    useDeviceStore.getState().setCapabilities({nfc:'tap'})
    const qr = vi.spyOn(device,'writeDeviceInfoQr').mockResolvedValue(undefined)
    const nfc = vi.spyOn(device,'writeNfcWebLink').mockResolvedValue({status:'written',value:'',bytes:1})
    await device.syncManagedDeviceLinks('IKT_TL_NEW')
    expect(qr.mock.calls[0][1]).toContain('auth=')
    expect(nfc.mock.calls[0][0]).toBe('https://solar2ain.github.io/ink-tag/?ble=IKT_TL_NEW')
    nfc.mockRestore()
    const internal = device as unknown as { requestV2:ReturnType<typeof vi.fn> }
    internal.requestV2 = vi.fn()
    await expect(device.writeNfcWebLink(qr.mock.calls[0][1])).rejects.toThrow('不能写入 NFC')
    expect(internal.requestV2).not.toHaveBeenCalled()
  })
  it.each([false, true])('reads deferred input masks when password enabled=%s is ready, without another proof', async (enabled) => {
    const device = new InkTagClient()
    const internal = device as unknown as { v2Capabilities:number; syncTime:ReturnType<typeof vi.fn>; requestV2: ReturnType<typeof vi.fn>; finishPasswordConnection: () => Promise<void> }
    internal.v2Capabilities = CapabilityV2.DEVICE_PASSWORD | CapabilityV2.INPUT
    device.passwordStatus = {enabled, authorized:enabled, failures:0, retryAfterMs:0, revision:enabled ? 1 : 0}
    internal.syncTime = vi.fn().mockResolvedValue(undefined)
    const input = new Uint8Array([1,7,0x23,1,0,0,0,0,0,0,0x56,4,0,0])
    internal.requestV2 = vi.fn().mockImplementation(async (op:number) => {
      if (op === CommandV2.GET_INPUT_CONFIG) return {payload:input}
        // Independent detail failures no longer prevent successful AUTH/input.
      throw new Error('fixture details boundary')
    })
      await expect(internal.finishPasswordConnection()).resolves.toBeUndefined()
      expect(useDeviceStore.getState().logs.some(log => log.message.includes('fixture details boundary'))).toBe(true)
    expect(useDeviceStore.getState().settingsCapabilities.nfcActionMask).toBe(0x456)
    expect(useDeviceStore.getState().settingsCapabilities.buttonActionMask).toBe(0x123)
    expect(internal.requestV2.mock.calls.some(([op])=>op===CommandV2.AUTHORIZE_DEVICE_PASSWORD)).toBe(false)
    expect(internal.syncTime).toHaveBeenCalledTimes(1)
  })
})
