import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import { InkTagClient, ProtocolV2Error } from './client'
import { RESET_SETTINGS_PROOF_DATA, derivePasswordChangeKey, derivePasswordCredential } from './devicePassword'
import { CapabilityV2, CommandV2, RequestFlagV2, StatusV2, SystemActionV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'

type Internal = {
  protocolV2: boolean; v2Capabilities: number; device: BluetoothDevice | null
  requestV2: ReturnType<typeof vi.fn>; stopDeviceWork: ReturnType<typeof vi.fn>
  waitForV2DisplayIdle: ReturnType<typeof vi.fn>; refreshV2ResourceInfo: ReturnType<typeof vi.fn>
  readPasswordStatus: ReturnType<typeof vi.fn>; provePassword: ReturnType<typeof vi.fn>
  waitForDeviceRestart: ReturnType<typeof vi.fn>; rememberPasswordCredential: ReturnType<typeof vi.fn>
  passwordId: ReturnType<typeof vi.fn>; systemAction: ReturnType<typeof vi.fn>
  passwordNotify: ReturnType<typeof vi.fn>
}
const id = '0123456789ABCDEF'
const status = { enabled: true, authorized: true, revision: 7, failures: 0, retryAfterMs: 0 }
function fixture(password = false) {
  const client = new InkTagClient()
  const internal = client as unknown as Internal
  const gatt = { connected: true } as BluetoothRemoteGATTServer
  const device = Object.assign(new EventTarget(), { id: 'fixture-app', gatt }) as BluetoothDevice
  Object.assign(internal, { device, server: gatt, otaRecoveryType: null,
    runtimeImageInfo: { version: 1, appBytes: 69056, checksum: 42 } })
  useDeviceStore.setState({ deviceId: device.id, boardId: 'nrf52811_aesl' })
  vi.spyOn(client, 'syncTimeAndVerify').mockResolvedValue(undefined)
  internal.protocolV2 = true
  internal.v2Capabilities = CapabilityV2.LIFECYCLE | CapabilityV2.LIFECYCLE_V2 | (password ? CapabilityV2.DEVICE_PASSWORD : 0)
  internal.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array([4, 0]) })
  internal.stopDeviceWork = vi.fn().mockResolvedValue(undefined)
  internal.waitForV2DisplayIdle = vi.fn().mockResolvedValue(undefined)
  internal.refreshV2ResourceInfo = vi.fn().mockResolvedValue(undefined)
  internal.readPasswordStatus = vi.fn().mockResolvedValue(password ? status : null)
  internal.provePassword = vi.fn().mockResolvedValue(undefined)
  internal.rememberPasswordCredential = vi.fn()
  internal.waitForDeviceRestart = vi.fn().mockResolvedValue(undefined)
  internal.passwordId = vi.fn().mockReturnValue(id)
  internal.passwordNotify = vi.fn()
  return { client, internal }
}
describe('two lifecycle actions', () => {
  beforeEach(() => {
    useDeviceStore.getState().resetDevice()
    useDeviceStore.getState().clearLogs()
    useDeviceStore.setState({ settingsCapabilities: { ...useDeviceStore.getState().settingsCapabilities,
      lifecycle: true, experimentalTimedSleep: true } })
  })
  it.each([0, CapabilityV2.LIFECYCLE, CapabilityV2.LIFECYCLE_V2])('refuses old or incomplete capability %i', async (caps) => {
    const { client, internal } = fixture()
    internal.v2Capabilities = caps
    await expect(client.clearDeviceUserData()).rejects.toThrow('新语义')
    await expect(client.resetDeviceSettings()).rejects.toThrow('新')
    expect(internal.requestV2).not.toHaveBeenCalled()
  })
  it('only clear-images may cancel and retry after explicit BUSY', async () => {
    const { client, internal } = fixture()
    internal.requestV2.mockRejectedValueOnce(new ProtocolV2Error(StatusV2.BUSY))
    await expect(client.clearDeviceUserData()).resolves.toBe(4)
    expect(internal.stopDeviceWork).toHaveBeenCalledOnce()
    expect(internal.requestV2).toHaveBeenCalledTimes(2)
    expect(internal.requestV2).toHaveBeenCalledWith(CommandV2.SYSTEM_ACTION,
      new Uint8Array([SystemActionV2.CLEAR_USER_DATA]), RequestFlagV2.CONFIRM, 60000)
  })
  it('retains soft-reset and bounded timed-sleep behavior', async () => {
    const { client, internal } = fixture()
    await client.softReset()
    await client.enterTimedDeepSleep(60)
    expect(internal.requestV2.mock.calls.map(([, payload, flags]) => [payload[0], flags])).toEqual([
      [SystemActionV2.RESET, RequestFlagV2.NONE], [SystemActionV2.SLEEP, RequestFlagV2.NONE] ])
    await expect(client.enterTimedDeepSleep(0)).rejects.toThrow('5–3600')
  })
  it('requires fresh original-password W even for an authorized QR session', async () => {
    const { client, internal } = fixture(true)
    await expect(client.resetDeviceSettings()).rejects.toThrow('原密码')
    expect(internal.requestV2).not.toHaveBeenCalled()
    internal.readPasswordStatus.mockResolvedValueOnce(status).mockResolvedValue({ ...status, enabled: false })
    await client.resetDeviceSettings('Test1234')
    expect(internal.provePassword).toHaveBeenCalledWith(await derivePasswordChangeKey(id, 'Test1234'), 2, RESET_SETTINGS_PROOF_DATA)
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(internal.rememberPasswordCredential).toHaveBeenCalledWith(id, null)
  })
  it('authorizes V before fresh W when the session is not authorized', async () => {
    const { client, internal } = fixture(true)
    internal.readPasswordStatus.mockResolvedValueOnce({ ...status, authorized: false }).mockResolvedValue({ ...status, enabled: false })
    await client.resetDeviceSettings('Test1234')
    expect(internal.provePassword).toHaveBeenNthCalledWith(1, await derivePasswordCredential(id, 'Test1234'))
    expect(internal.provePassword).toHaveBeenNthCalledWith(2, await derivePasswordChangeKey(id, 'Test1234'), 2, RESET_SETTINGS_PROOF_DATA)
  })
  it('never sends action5 after W failure and never replays it on BUSY', async () => {
    const { client, internal } = fixture(true)
    internal.provePassword.mockRejectedValueOnce(new Error('W rejected'))
    await expect(client.resetDeviceSettings('Test1234')).rejects.toThrow('W rejected')
    expect(internal.requestV2).not.toHaveBeenCalled()
    internal.requestV2.mockRejectedValueOnce(new ProtocolV2Error(StatusV2.BUSY))
    await expect(client.resetDeviceSettings('Test1234')).rejects.toThrow()
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(internal.stopDeviceWork).not.toHaveBeenCalled()
    expect(internal.rememberPasswordCredential).not.toHaveBeenCalled()
  })
  it('does not forget the cached credential before disabled-password readback', async () => {
    const { client, internal } = fixture(true)
    await expect(client.resetDeviceSettings('Test1234')).rejects.toThrow('尚未确认')
    expect(internal.rememberPasswordCredential).not.toHaveBeenCalled()
  })
  it('reports a lost action5 reply as unknown without replay or credential removal', async () => {
    const { client, internal } = fixture(true)
    internal.requestV2.mockRejectedValueOnce(new Error('Device disconnected'))
    await expect(client.resetDeviceSettings('Test1234')).rejects.toThrow('结果未知')
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(internal.waitForDeviceRestart).not.toHaveBeenCalled()
    expect(internal.rememberPasswordCredential).not.toHaveBeenCalled()
    expect(internal.passwordNotify).toHaveBeenCalled()
  })
  it('verifies physical image count, not only base-visible rows', async () => {
    const { client, internal } = fixture()
    internal.v2Capabilities |= CapabilityV2.RESOURCES
    const info = new Uint8Array(30); info[0] = 1; info[17] = 1
    internal.requestV2.mockResolvedValueOnce({ payload: new Uint8Array([4, 0]) }).mockResolvedValueOnce({ payload: info })
    await expect(client.clearDeviceUserData()).rejects.toThrow('尚未确认')
    expect(internal.refreshV2ResourceInfo).not.toHaveBeenCalled()
  })
  it('contains no public factory-reset API', () => {
    expect('restoreDeviceDefaults' in new InkTagClient()).toBe(false)
  })
})

describe('reboot connection observation', () => {
  type RestartInternal = {
    protocolV2: boolean
    device: BluetoothDevice | null
    server: BluetoothRemoteGATTServer | null
    characteristic: BluetoothRemoteGATTCharacteristic | null
    connectionGeneration: number
    requestV2: ReturnType<typeof vi.fn>
  }

  function restartFixture() {
    const client = new InkTagClient()
    const internal = client as unknown as RestartInternal
    const gatt = { connected: true } as BluetoothRemoteGATTServer
    const device = Object.assign(new EventTarget(), { id: 'restart-target', gatt }) as BluetoothDevice
    internal.protocolV2 = true
    internal.device = device
    internal.server = gatt
    internal.characteristic = {} as BluetoothRemoteGATTCharacteristic
    Object.assign(internal, { runtimeImageInfo: { version: 1, appBytes: 69056, checksum: 42 } })
    internal.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array([SystemActionV2.RESET]) })
    useDeviceStore.setState({ connection: 'connected', deviceId: device.id, boardId: 'nrf52811_aesl' })
    const reconnect = vi.spyOn(client, 'reconnectDevice').mockImplementation(async () => { Object.assign(gatt, { connected: true }) })
    const sync = vi.spyOn(client, 'syncTimeAndVerify').mockResolvedValue(undefined)
    return { client, internal, device, gatt, reconnect, sync }
  }

  beforeEach(() => {
    vi.useFakeTimers()
    vi.stubGlobal('window', globalThis)
    useDeviceStore.getState().resetDevice()
    useDeviceStore.getState().clearLogs()
  })
  afterEach(() => {
    vi.restoreAllMocks()
    vi.unstubAllGlobals()
    vi.useRealTimers()
  })

  it('reports an unconfirmed reboot, retains the live connection and never replays RESET', async () => {
    const { client, internal, reconnect } = restartFixture()
    const outcome = client.softReset().catch(error => error as Error)
    await vi.advanceTimersByTimeAsync(20000)
    const error = await outcome
    expect(error).toBeInstanceOf(Error)
    expect((error as Error).message).toContain('蓝牙连接中断')
    expect((error as Error).message).toContain('操作结果尚未确认')
    expect((error as Error).message).not.toContain('软重启后设备未')
    expect(useDeviceStore.getState().connection).toBe('connected')
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(reconnect).not.toHaveBeenCalled()
    expect(vi.getTimerCount()).toBe(0)
  })

  it('continues to reconnect if GATT is down even when the disconnect event was not observed', async () => {
    const { client, internal, gatt, device, reconnect } = restartFixture()
    const outcome = client.softReset()
    await vi.advanceTimersByTimeAsync(1)
    Object.assign(gatt, { connected: false })
    await vi.advanceTimersByTimeAsync(20499)
    await outcome
    expect(reconnect).toHaveBeenCalledExactlyOnceWith(device, expect.any(Number))
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(vi.getTimerCount()).toBe(0)
  })

  it('keeps the ordinary disconnect-event reconnection path bounded', async () => {
    const { client, internal, gatt, device, reconnect } = restartFixture()
    const outcome = client.softReset()
    await vi.advanceTimersByTimeAsync(1)
    Object.assign(gatt, { connected: false })
    device.dispatchEvent(new Event('gattserverdisconnected'))
    await vi.advanceTimersByTimeAsync(500)
    await outcome
    expect(reconnect).toHaveBeenCalledOnce()
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(vi.getTimerCount()).toBe(0)
  })

  it('does not restore the old connection state over a newer connection attempt', async () => {
    const { client, internal, reconnect } = restartFixture()
    const outcome = client.softReset().catch(error => error as Error)
    await vi.advanceTimersByTimeAsync(1)
    internal.connectionGeneration++
    useDeviceStore.setState({ connection: 'connecting', deviceId: 'another-target' })
    await vi.advanceTimersByTimeAsync(19999)
    expect(await outcome).toBeInstanceOf(Error)
    expect(useDeviceStore.getState().connection).toBe('connecting')
    expect(useDeviceStore.getState().deviceId).toBe('another-target')
    expect(reconnect).not.toHaveBeenCalled()
    expect(internal.requestV2).toHaveBeenCalledOnce()
  })

  it('waits for same-device verification and clock before reporting completion', async () => {
    const { client, device, gatt, reconnect, sync } = restartFixture()
    let finishSync!: () => void
    sync.mockImplementation(() => new Promise(resolve => { finishSync = resolve }))
    const phases: string[] = []
    let done = false
    const operation = client.softReset({ onPhase: message => phases.push(message) }).then(() => { done = true })
    await vi.advanceTimersByTimeAsync(1)
    Object.assign(gatt, { connected: false }); device.dispatchEvent(new Event('gattserverdisconnected'))
    await vi.advanceTimersByTimeAsync(500)
    expect(done).toBe(false)
    expect(sync).toHaveBeenCalledOnce()
    expect(reconnect.mock.invocationCallOrder[0]).toBeLessThan(sync.mock.invocationCallOrder[0])
    expect(phases.at(-1)).toBe('正在校时')
    finishSync(); await operation
    expect(done).toBe(true)
  })

  it('waits for fresh password identity bootstrap before comparing the returned identity', async () => {
    const { client, internal, device, gatt, reconnect, sync } = restartFixture()
    const activation = { ...useDeviceStore.getState().activation, activationId: id }
    useDeviceStore.setState({ activation })
    let finishBootstrap!: () => void
    reconnect.mockImplementation(async () => {
      Object.assign(gatt, { connected: true })
      useDeviceStore.setState({ activation: { ...activation, activationId: undefined } })
      Object.assign(internal, { passwordBootstrap: new Promise<void>(resolve => {
        finishBootstrap = () => { useDeviceStore.setState({ activation }); resolve() }
      }) })
    })
    const operation = client.softReset()
    await vi.advanceTimersByTimeAsync(1)
    Object.assign(gatt, { connected: false }); device.dispatchEvent(new Event('gattserverdisconnected'))
    await vi.advanceTimersByTimeAsync(500)
    expect(sync).not.toHaveBeenCalled()
    finishBootstrap(); await operation
    expect(sync).toHaveBeenCalledOnce()
  })

  it.each(['opt-out', 'clock-failed', 'wrong-image'] as const)('handles %s without repeating RESET', async mode => {
    const { client, internal, device, gatt, reconnect, sync } = restartFixture()
    if (mode === 'clock-failed') sync.mockRejectedValue(new Error('SET_TIME ACK lost'))
    if (mode === 'wrong-image') reconnect.mockImplementation(async () => {
      Object.assign(gatt, { connected: true })
      Object.assign(internal, { runtimeImageInfo: { version: 1, appBytes: 69056, checksum: 43 } })
    })
    const result = client.softReset({ syncTime: mode !== 'opt-out' }).then(() => null, error => error as Error)
    await vi.advanceTimersByTimeAsync(1)
    Object.assign(gatt, { connected: false }); device.dispatchEvent(new Event('gattserverdisconnected'))
    await vi.advanceTimersByTimeAsync(500)
    const error = await result
    if (mode === 'opt-out') { expect(error).toBeNull(); expect(sync).not.toHaveBeenCalled() }
    if (mode === 'clock-failed') { expect(error?.message).toContain('已重连，但校时未完成'); expect(sync).toHaveBeenCalledOnce() }
    if (mode === 'wrong-image') { expect(error?.message).toContain('不一致'); expect(sync).not.toHaveBeenCalled() }
    expect(internal.requestV2).toHaveBeenCalledOnce()
  })
})
