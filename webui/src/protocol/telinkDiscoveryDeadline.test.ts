import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { historicalOta } from './testFixtures/otaHistory'
import source from '../ota-drivers/telink_ota.js?raw'
import { telinkNativeTransportFixture } from './testFixtures/telinkNative'

function adapter(text = source) {
  const scope: { TelinkOta?: any } = {}
  new Function('window', text)(scope)
  return scope.TelinkOta!
}

function deferred() {
  let resolve!: (value: any) => void
  let reject!: (error: Error) => void
  const promise = new Promise<any>((done, fail) => { resolve = done; reject = fail })
  return { promise, resolve, reject }
}

const { firmware } = telinkNativeTransportFixture()
const stop = new Error('test boundary before image data')

function peer(hang = '') {
  const pending = deferred()
  const { compatibility, entry: entryBytes } = telinkNativeTransportFixture()
  const status = new Uint8Array([0, 0, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 1, 14, 0, 0])
  const read = vi.fn(async () => hang === 'status' ? pending.promise : new DataView(status.buffer))
  const write = vi.fn(async (_bytes: Uint8Array): Promise<any> => { throw stop })
  const char: any = Object.assign(new EventTarget(), { properties: { write: true, notify: true }, readValue: read,
    writeValueWithResponse: write, writeValueWithoutResponse: write,
    startNotifications: vi.fn(async () => char), stopNotifications: vi.fn(async () => char) })
  const notifyStatus = (value: Uint8Array) => {
    const event = new Event('characteristicvaluechanged')
    Object.defineProperty(event, 'target', { value: { value: new DataView(value.slice().buffer) } })
    char.dispatchEvent(event)
  }
  const compat = { properties: { read: true },
    readValue: vi.fn(async () => hang === 'compatibility-read' ? pending.promise : new DataView(compatibility.buffer)) }
  const entry = { properties: { read: true },
    readValue: vi.fn(async () => hang === 'entry-read' ? pending.promise : new DataView(entryBytes.buffer)) }
  const service = { getCharacteristic: vi.fn(async (uuid: string) => {
    if (uuid.endsWith('2b13')) return hang === 'compatibility-channel' ? pending.promise : compat
    if (uuid.endsWith('2b14')) return hang === 'entry-channel' ? pending.promise : entry
    return hang === 'channel' ? pending.promise : char
  }) }
  char.service = service
  const gatt: any = { connected: false, disconnect: vi.fn(() => { gatt.connected = false }),
    getPrimaryService: vi.fn(async () => hang === 'service' ? pending.promise : service) }
  gatt.connect = vi.fn(async () => {
    if (hang === 'connect') return pending.promise
    gatt.connected = true
    return gatt
  })
  const device = { id: 'exact-grant-80FA', gatt }
  const late = () => {
    gatt.connected = true
    pending.resolve(hang === 'connect' ? gatt : hang === 'service' ? service
      : hang === 'channel' ? char : hang === 'compatibility-channel' ? compat
        : hang === 'entry-channel' ? entry : hang === 'entry-read' ? new DataView(entryBytes.buffer)
          : hang === 'status' ? new DataView(status.buffer) : new DataView(compatibility.buffer))
  }
  return { device, gatt, service, char, entry, entryBytes, write, read, late, pending, notifyStatus }
}

beforeEach(() => vi.useFakeTimers())
afterEach(() => vi.useRealTimers())

it.each(['missing', 'unreadable', 'truncated', 'address'])(
  'rejects %s compiled entry proof before any upgrade write', async kind => {
    const a = adapter(), p = peer()
    if (kind === 'missing') {
      const current = p.service.getCharacteristic.getMockImplementation()!
      p.service.getCharacteristic.mockImplementation(async (uuid: string) => {
        if (uuid.endsWith('2b14')) throw new DOMException('No entry proof', 'NotFoundError')
        return current(uuid)
      })
    } else if (kind === 'unreadable') p.entry.properties.read = false
    else {
      const value = p.entryBytes.slice()
      if (kind === 'address') new DataView(value.buffer).setUint32(4, 0, true)
      p.entry.readValue.mockResolvedValue(new DataView(kind === 'truncated' ? value.slice(0, 19).buffer : value.buffer))
    }
    await expect(a.acquireRecoverySession({ device: p.device, generation: 1, isCurrent: () => true }, { firmware }))
      .rejects.toThrow(/entry|address|proof/)
    expect(p.write).not.toHaveBeenCalled()
  },
)

it('rediscovers once after a settled retiring App link drops, before any firmware write', async () => {
  const a = adapter(), p = peer()
  p.gatt.getPrimaryService.mockImplementationOnce(async () => {
    p.gatt.connected = false
    throw new DOMException('GATT Server is disconnected. Cannot retrieve services.', 'NetworkError')
  })
  const run = a.acquireRecoverySession({ device: p.device, generation: 1,
    isCurrent: () => true, afterAppEntry: true }, { firmware })
  await vi.advanceTimersByTimeAsync(901)
  const handle = await run
  expect(p.gatt.connect).toHaveBeenCalledTimes(2)
  expect(p.gatt.getPrimaryService).toHaveBeenCalledTimes(2)
  expect(p.write).not.toHaveBeenCalled()
  await a.releaseRecoverySession(handle)
})

it('does not rediscover a disconnected ordinary read without an App handoff', async () => {
  const a = adapter(), p = peer()
  p.gatt.getPrimaryService.mockImplementationOnce(async () => {
    p.gatt.connected = false
    throw new DOMException('GATT Server is disconnected', 'NetworkError')
  })
  await expect(a.queryRecoveryDevice(p.device)).rejects.toThrow('disconnected')
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  expect(p.write).not.toHaveBeenCalled()
})

it('limits settled App handoff discovery to one extra attempt', async () => {
  const a = adapter(), p = peer()
  p.gatt.getPrimaryService.mockImplementation(async () => {
    p.gatt.connected = false
    throw new DOMException('GATT Server is disconnected', 'NetworkError')
  })
  const run = a.acquireRecoverySession({ device: p.device, generation: 1,
    isCurrent: () => true, afterAppEntry: true }, { firmware })
  const rejected = expect(run).rejects.toThrow('disconnected')
  await vi.advanceTimersByTimeAsync(3000); await rejected
  expect(p.gatt.connect).toHaveBeenCalledTimes(2)
  expect(p.gatt.getPrimaryService).toHaveBeenCalledTimes(2)
  expect(p.write).not.toHaveBeenCalled()
})

it('starts the selected package once instead of trusting an unidentified receiving prefix', async () => {
  const a = adapter(), p = peer()
  p.read.mockResolvedValue(new DataView(new Uint8Array([
    1, 2, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 1, 14, 0, 0,
  ]).buffer))
  const pkg = { firmware }
  const handle = await a.acquireRecoverySession({ device: p.device, generation: 1,
    isCurrent: () => true }, pkg)
  await expect(a.transferRecoverySession(handle, pkg)).rejects.toThrow(stop)
  expect(p.write).toHaveBeenCalledOnce()
  expect(Array.from(p.write.mock.calls[0][0])).toEqual([1, 255])
})

it('never rediscovers an unknown native timeout even after an App handoff', async () => {
  const a = adapter(), p = peer('service')
  const run = a.acquireRecoverySession({ device: p.device, generation: 1,
    isCurrent: () => true, afterAppEntry: true }, { firmware })
  const rejected = expect(run).rejects.toMatchObject({ code: 'TLSR_RECOVERY_DISCOVERY_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(46000); await rejected
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  expect(p.gatt.getPrimaryService).toHaveBeenCalledOnce()
  expect(p.write).not.toHaveBeenCalled()
  p.late(); await vi.advanceTimersByTimeAsync(0)
  expect(p.gatt.connected).toBe(false)
})

it('reproduces the original outer deadline failing to bound a native connect promise', async () => {
  const old = historicalOta('044bf511-telink_ota.js.txt')
  const p = peer('connect'); let settled = false
  void adapter(old).transferDevice(p.device, { firmware }, () => {}).then(() => { settled = true }, () => { settled = true })
  await vi.advanceTimersByTimeAsync(120000)
  expect(settled).toBe(false)
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  expect(p.write).not.toHaveBeenCalled()
})

it.each(['connect', 'service', 'channel', 'compatibility-channel', 'compatibility-read', 'entry-channel', 'entry-read', 'status'])(
  'bounds a hung %s, logs its phase and never continues or retries a late result', async (stage) => {
    const a = adapter(), p = peer(stage), logs: string[] = []
    const run = a.transferDevice(p.device, { firmware }, () => {}, { onPhase: (message: string) => logs.push(message) })
    const rejected = expect(run).rejects.toMatchObject({ code: 'TLSR_RECOVERY_DISCOVERY_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(46000); await rejected
    expect(logs.some(x => x.includes('超时'))).toBe(true)
    expect(p.gatt.connect).toHaveBeenCalledOnce()
    expect(p.write).not.toHaveBeenCalled()
    const calls = p.service.getCharacteristic.mock.calls.length
    p.late(); await vi.advanceTimersByTimeAsync(0)
    expect(p.gatt.connected).toBe(false)
    expect(p.service.getCharacteristic).toHaveBeenCalledTimes(calls)
    expect(p.write).not.toHaveBeenCalled()
  },
)

it('quarantines the same grant until the abandoned native call truly settles', async () => {
  const a = adapter(), p = peer('connect')
  const run = a.transferDevice(p.device, { firmware }, () => {})
  const rejected = expect(run).rejects.toMatchObject({ code: 'TLSR_RECOVERY_DISCOVERY_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(12000); await rejected
  expect(() => a.cancelRecoveryDiscovery({ ...p.device })).toThrow(/尚未结束/)
  await expect(a.transferDevice(p.device, { firmware }, () => {})).rejects.toMatchObject({ code: 'TLSR_NATIVE_PENDING' })
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  p.late(); await vi.advanceTimersByTimeAsync(0)
  expect(p.gatt.connected).toBe(false)
  expect(() => a.assertDeviceAvailable(p.device)).not.toThrow()
  expect(p.write).not.toHaveBeenCalled()
})

it('also releases an abandoned connection on late native rejection without replay', async () => {
  const a = adapter(), p = peer('connect')
  const run = a.transferDevice(p.device, { firmware }, () => {})
  const rejected = expect(run).rejects.toMatchObject({ code: 'TLSR_RECOVERY_DISCOVERY_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(12000); await rejected
  p.gatt.connected = true
  p.pending.reject(new DOMException('late failure', 'NetworkError'))
  await vi.advanceTimersByTimeAsync(0)
  expect(p.gatt.connected).toBe(false)
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  expect(p.write).not.toHaveBeenCalled()
})

it('does not replay even a settled native transport failure', async () => {
  const a = adapter(), p = peer()
  p.gatt.connect.mockRejectedValue(new DOMException('unreachable', 'NetworkError'))
  const run = a.transferDevice(p.device, { firmware }, () => {})
  const rejected = expect(run).rejects.toMatchObject({ name: 'NetworkError' })
  await vi.runAllTimersAsync(); await rejected
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  expect(p.write).not.toHaveBeenCalled()
})

it('keeps permission refusal terminal and a valid peer reaches only the normal write boundary', async () => {
  const a = adapter(), denied = peer()
  denied.gatt.connect.mockRejectedValue(new DOMException('denied', 'SecurityError'))
  const first = a.transferDevice(denied.device, { firmware }, () => {})
  const failed = expect(first).rejects.toMatchObject({ name: 'SecurityError' })
  await vi.runAllTimersAsync(); await failed
  expect(denied.gatt.connect).toHaveBeenCalledOnce()
  expect(denied.write).not.toHaveBeenCalled()
  const valid = peer(), next = a.transferDevice(valid.device, { firmware }, () => {})
  const boundary = expect(next).rejects.toBe(stop)
  await vi.runAllTimersAsync(); await boundary
  expect(valid.write).toHaveBeenCalledExactlyOnceWith(new Uint8Array([1, 255]))
})


it('acquires an opaque exact live verified link without reconnect or repeated service discovery', async () => {
  const a = adapter(), p = peer(); p.gatt.connected = true; p.gatt.device = p.device
  await a.queryRecovery(p.gatt)
  const reads = p.gatt.getPrimaryService.mock.calls.length
  const pkg = { firmware: firmware.slice() }
  const handle = await a.acquireRecoverySession({ device: p.device, generation: 3, isCurrent: () => true }, pkg)
  expect(Object.keys(handle)).toEqual([])
  expect(p.gatt.connect).not.toHaveBeenCalled()
  expect(p.gatt.disconnect).not.toHaveBeenCalled()
  expect(p.gatt.getPrimaryService).toHaveBeenCalledTimes(reads)
  expect(p.write).not.toHaveBeenCalled()
  await a.releaseRecoverySession(handle); await a.releaseRecoverySession(handle)
  expect(p.gatt.disconnect).toHaveBeenCalledOnce()
})

it.each(['package', 'generation'])('rejects changed %s before any upgrade control write', async kind => {
  const a = adapter(), p = peer(); let current = true
  const pkg = { firmware: firmware.slice() }
  const handle = await a.acquireRecoverySession({ device: p.device, generation: 3, isCurrent: () => current }, pkg)
  if (kind === 'package') pkg.firmware[65] ^= 1
  else current = false
  await expect(a.transferRecoverySession(handle, pkg)).rejects.toMatchObject({ code: 'TLSR_SESSION_INVALID' })
  expect(p.write).not.toHaveBeenCalled()
  await expect(a.transferRecoverySession(handle, pkg)).rejects.toMatchObject({ code: 'TLSR_SESSION_INVALID' })
  await expect(a.transferRecoverySession({}, pkg)).rejects.toMatchObject({ code: 'TLSR_SESSION_INVALID' })
})

it.each(['BEGIN', 'DATA', 'END', 'CCC'])('quarantines pending %s and never replays an unknown operation', async stage => {
  const a = adapter(), p = peer(), pending = deferred()
  const status = new Uint8Array([stage === 'BEGIN' ? 0 : 1, 0, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 1, 14, 0, 0])
  p.read.mockImplementation(async () => new DataView(status.slice().buffer))
  p.char.startNotifications = vi.fn(() => stage === 'CCC' ? pending.promise : Promise.resolve(p.char))
  p.write.mockImplementation(async (bytes: Uint8Array) => {
    if (bytes[0] === 1 && bytes[1] === 255) {
      if (stage === 'BEGIN') return pending.promise
      status[0] = 1; new DataView(status.buffer).setUint16(1, 0, true)
      return
    }
    if (stage === 'DATA' || (bytes[0] === 2 && bytes[1] === 255)) return pending.promise
    const index = bytes[0] | bytes[1] << 8
    new DataView(status.buffer).setUint16(1, index + (bytes.length - 4) / 16, true)
    p.notifyStatus(status)
  })
  const pkg = { firmware: firmware.slice() }
  const handle = await a.acquireRecoverySession({ device: p.device, generation: 4, isCurrent: () => true }, pkg)
  const run = a.transferRecoverySession(handle, pkg)
  const expectedCode = stage === 'CCC' ? 'TLSR_RECOVERY_DISCOVERY_TIMEOUT'
    : stage === 'END' ? 'TLSR_FINALIZATION_UNKNOWN' : 'TLSR_OTA_RESULT_UNKNOWN'
  const rejected = expect(run).rejects.toMatchObject({ code: expectedCode })
  await vi.advanceTimersByTimeAsync(14000); await rejected
  expect(() => a.assertDeviceAvailable(p.device)).toThrow(/尚未结束/)
  expect(() => a.assertDeviceAvailable({ id: 'different-grant' })).not.toThrow()
  await expect(a.acquireRecoverySession({ device: p.device, generation: 5, isCurrent: () => true }, pkg)).rejects.toMatchObject({ code: 'TLSR_NATIVE_PENDING' })
  const writes = p.write.mock.calls.length
  pending.resolve(undefined); await vi.advanceTimersByTimeAsync(0)
  expect(p.write).toHaveBeenCalledTimes(writes)
  expect(() => a.assertDeviceAvailable(p.device)).not.toThrow()
  expect(p.gatt.connect).toHaveBeenCalledOnce()
})

it('reproduces the fixed-five-second service budget rejecting a still-pending native discovery', async () => {
  const old = historicalOta('16f0bfa9-telink_ota.js.txt')
  const p = peer('service'), run = adapter(old).transferDevice(p.device, { firmware }, () => {})
  const rejected = expect(run).rejects.toMatchObject({ code: 'TLSR_RECOVERY_DISCOVERY_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(6000); await rejected
  expect(p.write).not.toHaveBeenCalled(); expect(p.gatt.connect).toHaveBeenCalledOnce()
})

it('lets one slow service operation finish inside the original total45s budget, read-only', async () => {
  const a = adapter(), p = peer('service'), logs: string[] = []
  const run = a.queryRecoveryDevice(p.device, { onPhase: (s: string) => logs.push(s) })
  await vi.advanceTimersByTimeAsync(12000)
  expect(p.gatt.connect).toHaveBeenCalledOnce(); expect(p.read).not.toHaveBeenCalled()
  p.pending.resolve(p.service); await vi.advanceTimersByTimeAsync(0)
  await expect(run).resolves.toMatchObject({ status: 0 })
  expect(logs.some(s => s.includes('发现升级服务完成（12000 ms）'))).toBe(true)
  expect(p.write).not.toHaveBeenCalled(); expect(p.gatt.connected).toBe(false)
})

it('never writes or restarts the45s budget when a read-only native service stays unresolved', async () => {
  const a = adapter(), p = peer('service'), run = a.queryRecoveryDevice(p.device)
  const rejected = expect(run).rejects.toMatchObject({ code: 'TLSR_RECOVERY_DISCOVERY_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(45000); await rejected
  expect(p.gatt.connect).toHaveBeenCalledOnce(); expect(p.write).not.toHaveBeenCalled()
  p.late(); await vi.advanceTimersByTimeAsync(0)
  expect(p.read).not.toHaveBeenCalled(); expect(p.gatt.connected).toBe(false)
})


it('closes a cancelled generation late connect before releasing its exact-grant quarantine', async () => {
  const a=adapter(), p=peer('connect'); let current=true
  const pkg={firmware:firmware.slice()}
  const run=a.acquireRecoverySession({device:p.device,generation:7,isCurrent:()=>current},pkg)
  const failed=expect(run).rejects.toMatchObject({code:'TLSR_RECOVERY_DISCOVERY_TIMEOUT'})
  await vi.advanceTimersByTimeAsync(12000); await failed
  current=false
  expect(()=>a.assertDeviceAvailable(p.device)).toThrow()
  p.late(); await vi.advanceTimersByTimeAsync(0)
  expect(p.gatt.connected).toBe(false)
  expect(()=>a.assertDeviceAvailable(p.device)).not.toThrow()
  expect(p.write).not.toHaveBeenCalled()
})
