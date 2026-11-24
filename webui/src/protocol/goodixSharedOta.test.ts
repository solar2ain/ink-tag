import { afterEach, beforeEach, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import { inktagClient } from './client'
import { transferOta } from './ota'
import { goodixFixture, deferred } from './test-support/goodixOta'
import { useDeviceStore } from '../store/device'
import { CapabilityV2, CommandV2, PROTOCOL_V2_VERSION, StatusV2 } from './generatedCommandsV2'

beforeEach(() => { vi.useFakeTimers(); useDeviceStore.getState().resetDevice() })
afterEach(() => { inktagClient.disconnect(); vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers(); useDeviceStore.getState().resetDevice() })

function stack(recovery = false) {
  const f = goodixFixture(), raw = inktagClient as any
  Object.assign(f.device, { name: recovery ? 'IKT_GR_FC34_OTA' : 'IKT_GR_FC34' })
  f.target[5] = recovery ? 0 : 1
  vi.stubGlobal('window', { GoodixOta: f.ota, setTimeout, clearTimeout })
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  const product = Object.assign(new EventTarget(), {
    startNotifications: vi.fn(async () => product),
    writeValueWithResponse: vi.fn(async (packet: Uint8Array) => {
      queueMicrotask(() => {
        const bytes = new Uint8Array([packet[0], packet[1], StatusV2.OK])
        const event = new Event('characteristicvaluechanged')
        Object.defineProperty(event, 'target', { value: { value: new DataView(bytes.buffer) } })
        product.dispatchEvent(event)
      })
    }),
  })
  const version = { readValue: vi.fn(async () => new DataView(new Uint8Array([PROTOCOL_V2_VERSION, 0, 2, 0]).buffer)) }
  const appService = { getCharacteristic: vi.fn(async (uuid: string) => uuid.includes('0002') ? product : version) }
  f.server.getPrimaryService.mockImplementation(async (uuid: string) => {
    if (uuid.startsWith('62750001')) {
      if (f.target[5] !== 1) throw new DOMException('no App service', 'NotFoundError')
      return appService as any
    }
    return f.service
  })
  f.server.disconnect.mockImplementation(() => {
    f.server.connected = false
    if (f.commands.includes(0x25)) f.target[5] = 1
    else if (f.commands.includes(0x41)) f.target[5] = 0
    f.device.dispatchEvent(new Event('gattserverdisconnected'))
  })
  const pkg = { ...f.pkg, application: { ...f.pkg.application, size: f.pkg.firmware.length, checksum: 0 } }
  const identity = () => {
    raw.runtimeImageInfo = { version: 1, appBytes: pkg.application.size, checksum: 0 }
    useDeviceStore.getState().setIdentity({ boardId: 'gr551x_zkel', bleAddress: 'A0:A3:B8:BA:FC:34' })
    useDeviceStore.getState().setCapabilities({ ota: 'goodix' })
  }
  vi.spyOn(raw, 'initializeV2Core').mockImplementation(async () => { identity() })
  vi.spyOn(raw, 'initializeV2Details').mockResolvedValue(undefined)
  vi.spyOn(raw, 'getDisplaySettings').mockResolvedValue(undefined)
  const sync = vi.spyOn(inktagClient, 'syncTime').mockResolvedValue(undefined)
  Object.assign(raw, { device: f.device, server: f.server, protocolV2: !recovery,
    characteristic: recovery ? null : product, otaRecoveryType: recovery ? 'goodix' : null,
    verifiedRecoveryName: recovery ? 'IKT_GR_FC34_OTA' : '', expectedFixedRecovery: null,
    disconnectRequested: false, passwordBootstrap: Promise.resolve(), v2Capabilities: 0,
    passwordDetailsGeneration: -1, v2RequestQueue: Promise.resolve(), writeQueue: Promise.resolve() })
  if (!recovery) product.addEventListener('characteristicvaluechanged', raw.onNotification)
  f.device.addEventListener('gattserverdisconnected', raw.onDisconnected)
  useDeviceStore.setState({ connection: 'connected', deviceId: f.device.id,
    deviceName: f.device.name, firmwareVersion: recovery ? 'Recovery' : '0.2.0', protocolVersion: recovery ? null : PROTOCOL_V2_VERSION })
  identity()
  return { ...f, raw, product, sync, pkg, parsed: { type: 'goodix' as const, payload: pkg, summary: 'host signed-package stand-in' } }
}

it.each([false, true])('actual shared entry/client and GR adapter complete one transaction with one post-attestation sync (direct Recovery=%s)', async recovery => {
  const f = stack(recovery), progress = vi.fn()
  const operation = transferOta(f.parsed, progress)
  const result = expect(operation).resolves.toBeUndefined()
  await vi.runAllTimersAsync(); await result
  expect(f.commands.filter(c => c === 0x41)).toHaveLength(recovery ? 0 : 1)
  expect(f.product.writeValueWithResponse.mock.calls.filter(([p]) => p[0] === CommandV2.SYSTEM_ACTION)).toHaveLength(recovery ? 0 : 1)
  expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
  expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
  expect(f.commands.filter(c => c === 0x42)).toHaveLength(recovery ? 1 : 2)
  expect(f.sync).toHaveBeenCalledTimes(1)
  expect(inktagClient.getDevice()).toBe(f.device)
  expect(inktagClient.getRuntimeImageInfo()).toMatchObject({ appBytes: f.pkg.application.size, checksum: 0 })
  expect(progress.mock.lastCall?.[0].message).toContain('应用已验身、重连并校时')
})

it('keeps ordinary password rejection ahead of GR vendor commit and programming', async () => {
  const f = stack()
  f.raw.v2Capabilities = CapabilityV2.DEVICE_PASSWORD
  f.raw.passwordStatus = { enabled: true, authorized: false }
  await expect(transferOta(f.parsed, vi.fn())).rejects.toThrow('密码')
  expect(f.product.writeValueWithResponse).not.toHaveBeenCalled()
  expect(f.commands).not.toContain(0x41)
  expect(f.commands).not.toContain(0x23)
  expect(f.sync).not.toHaveBeenCalled()
})

it('keeps a user disconnect during acquire terminal, without mode replay or later programming', async () => {
  const f = stack(true), pending = deferred()
  f.server.getPrimaryService.mockReturnValueOnce(pending.promise)
  const result = expect(transferOta(f.parsed, vi.fn())).rejects.toThrow()
  await vi.advanceTimersByTimeAsync(1)
  inktagClient.disconnect()
  pending.resolve(f.service)
  await vi.runAllTimersAsync(); await result
  expect(f.commands).not.toContain(0x23)
  expect(f.commands).not.toContain(0x41)
  expect(f.sync).not.toHaveBeenCalled()
})

it('rejects a returning wrong App before success or clock sync', async () => {
  const f = stack()
  vi.mocked(f.raw.initializeV2Core).mockImplementation(async () => {
    f.raw.runtimeImageInfo = { version: 1, appBytes: 5, checksum: 1 }
    useDeviceStore.getState().setIdentity({ boardId: 'gr551x_zkel' })
  })
  const result = expect(transferOta(f.parsed, vi.fn())).rejects.toThrow('应用与升级包不一致')
  await vi.runAllTimersAsync(); await result
  expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
  expect(f.sync).not.toHaveBeenCalled()
})

it('honors user disconnect during post-END reboot wait rather than reconnecting and syncing anyway', async () => {
  const f = stack(true)
  const result = expect(transferOta(f.parsed, vi.fn())).rejects.toThrow(/取消|变化/)
  await vi.advanceTimersByTimeAsync(101)
  expect(f.commands).toContain(0x25)
  const connects = f.server.connect.mock.calls.length
  inktagClient.disconnect()
  await vi.runAllTimersAsync(); await result
  expect(f.server.connect).toHaveBeenCalledTimes(connects)
  expect(f.sync).not.toHaveBeenCalled()
})

it('never commits vendor mode after cancellation during its native service discovery', async () => {
  const f = stack(), pending = deferred()
  f.server.getPrimaryService.mockReturnValueOnce(pending.promise)
  const result = expect(transferOta(f.parsed, vi.fn())).rejects.toThrow()
  await vi.advanceTimersByTimeAsync(1)
  inktagClient.disconnect()
  pending.resolve(f.service)
  await vi.runAllTimersAsync(); await result
  expect(f.ctrl.writeValueWithoutResponse).not.toHaveBeenCalled()
  expect(f.commands).not.toContain(0x41)
  expect(f.commands).not.toContain(0x23)
  expect(f.sync).not.toHaveBeenCalled()
})

it('does not replay a refused GR public prepare or send CANCEL/vendor mode', async () => {
  const f = stack()
  f.product.writeValueWithResponse.mockImplementation(async (packet: Uint8Array) => {
    queueMicrotask(() => f.raw.handleV2Notification(new Uint8Array([packet[0], packet[1], StatusV2.BUSY])))
  })
  const result = expect(transferOta(f.parsed, vi.fn())).rejects.toThrow()
  await vi.runAllTimersAsync(); await result
  expect(f.product.writeValueWithResponse).toHaveBeenCalledTimes(1)
  expect(f.commands).not.toContain(0x41)
  expect(f.commands).not.toContain(0x23)
  expect(f.sync).not.toHaveBeenCalled()
})

it('actual shared flow accepts settled disconnected cleanup only after END ACK and then attests/syncs once', async () => {
  const f = stack(true), progress = vi.fn()
  f.tx.stopNotifications.mockImplementation(async () => {
    expect(f.commands).toContain(0x25)
    f.target[5] = 1
    f.server.connected = false
    f.device.dispatchEvent(new Event('gattserverdisconnected'))
    throw new DOMException('GATT disconnected', 'NetworkError')
  })
  const result = expect(transferOta(f.parsed, progress)).resolves.toBeUndefined()
  await vi.runAllTimersAsync(); await result
  expect(f.sync).toHaveBeenCalledTimes(1)
  expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
  expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
  expect(progress.mock.lastCall?.[0].message).toContain('应用已验身、重连并校时')
})

it('never syncs a connection cancelled while returning App authorization is pending', async () => {
  const f = stack(true), authorization = deferred()
  vi.spyOn(inktagClient, 'waitForPasswordAuthorization').mockImplementation(() => authorization.promise)
  const result = expect(transferOta(f.parsed, vi.fn())).rejects.toThrow(/取消|变化/)
  await vi.advanceTimersByTimeAsync(3000)
  expect(inktagClient.getDevice()).toBe(f.device)
  expect(f.raw.protocolV2).toBe(true)
  inktagClient.disconnect()
  authorization.resolve(undefined)
  await vi.runAllTimersAsync(); await result
  expect(f.sync).not.toHaveBeenCalled()
})

it('rejects a second OTA action without replacing the first acquired owner', async () => {
  const f = stack(true), pending = deferred()
  f.server.getPrimaryService.mockReturnValueOnce(pending.promise)
  const first = expect(transferOta(f.parsed, vi.fn())).resolves.toBeUndefined()
  await vi.advanceTimersByTimeAsync(1)
  // Client has relinquished its link; a second request cannot reacquire it.
  await expect(transferOta(f.parsed, vi.fn())).rejects.toThrow(/先连接|等待/)
  expect(f.server.getPrimaryService).toHaveBeenCalledTimes(1)
  pending.resolve(f.service)
  await vi.runAllTimersAsync(); await first
  expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
  expect(f.sync).toHaveBeenCalledTimes(1)
})

it('ordinary client reconnect cannot bypass a timed-out native GR acquisition quarantine', async () => {
  const f = stack(true), pending = deferred()
  // This native fake implements only the exercised operations. Assert its
  // browser API type at the public-client boundary, not in production code.
  const browserDevice = f.device as unknown as BluetoothDevice
  f.server.getPrimaryService.mockReturnValueOnce(pending.promise)
  const transfer = expect(transferOta(f.parsed, vi.fn())).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(45001); await transfer
  expect(useDeviceStore.getState().action).toBe('idle')
  const connects = f.server.connect.mock.calls.length
  await expect(inktagClient.reconnectDevice(browserDevice)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_PENDING' })
  expect(f.server.connect).toHaveBeenCalledTimes(connects)
  pending.resolve(f.service)
  await vi.advanceTimersByTimeAsync(1)
  const read = expect(inktagClient.reconnectDevice(browserDevice)).resolves.toBeUndefined()
  await vi.runAllTimersAsync(); await read
  expect(f.server.connect).toHaveBeenCalledTimes(connects + 1)
  expect(f.commands).not.toContain(0x23)
  expect(f.sync).not.toHaveBeenCalled()
})
