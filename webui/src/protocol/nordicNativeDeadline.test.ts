import { readFileSync } from 'node:fs'
import { historicalOta } from './testFixtures/otaHistory'
import vm from 'node:vm'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'

const source = readFileSync(new URL('../ota-drivers/nordic_dfu.js', import.meta.url), 'utf8')
function adapter(text = source) {
  // This suite isolates native promise lifetimes after admission. Production
  // signature checks have actual signed/forged fixtures in signedPreflight.
  const window: any = { InkTagSignedOta: { nordic: vi.fn(async () => ({publisherVerified:true})), inspectNordic: vi.fn(async () => ({publisherVerified:true})) } }
  vm.runInNewContext(text, { window, Date, setTimeout, clearTimeout, performance, console,
    Uint8Array, DataView, DOMException, Map, WeakSet })
  return window.NordicDfu
}
function deferred() { let resolve!: (value?: any) => void; const promise = new Promise<any>(r => { resolve = r }); return { promise, resolve } }
function peer(hang = '') {
  const pending = deferred()
  const control: any = { addEventListener: vi.fn(), removeEventListener: vi.fn(),
    startNotifications: vi.fn(async () => hang === 'notify' ? pending.promise : control),
    stopNotifications: vi.fn(async () => hang === 'close' ? pending.promise : control),
    writeValueWithResponse: vi.fn(async () => {}) }
  const packet = { writeValueWithoutResponse: vi.fn(async () => {}) }
  const service = { getCharacteristic: vi.fn(async (uuid: string) => {
    const stage = uuid.startsWith('8ec90001') ? 'control' : 'packet'
    return hang === stage ? pending.promise : stage === 'control' ? control : packet
  }) }
  const gatt: any = { connected: true,
    disconnect: vi.fn(() => { gatt.connected = false }),
    getPrimaryService: vi.fn(async () => hang === 'service' ? pending.promise : service) }
  gatt.connect = vi.fn(async () => hang === 'connect' ? pending.promise : gatt)
  const device = { id: 'exact-F988', name: 'IKT_NRF_F988_OTA', gatt }
  gatt.device = device
  return { pending, control, packet, service, gatt, device }
}
beforeEach(() => vi.useFakeTimers())
afterEach(() => vi.useRealTimers())

it('rejects the freshly authenticated foreign board before connecting or writing', async () => {
  const a=adapter(),p=peer()
  await expect(a.transferDevice(p.device,{},()=>{},{expectedBoard:'aesl22_nrf'})).rejects.toThrow('不匹配')
  expect(p.gatt.connect).not.toHaveBeenCalled()
  expect(p.control.writeValueWithResponse).not.toHaveBeenCalled()
})

it('reproduces the old native discovery promise staying unresolved beyond the init budget', async () => {
  const old = historicalOta('16f0bfa9-nordic_dfu.js.txt')
  const a = adapter(old), p = peer('service'); let settled = false
  void new a.SecureDfuTransport(p.gatt).open(false).then(() => settled = true, () => settled = true)
  await vi.advanceTimersByTimeAsync(120000)
  expect(settled).toBe(false); expect(p.control.writeValueWithResponse).not.toHaveBeenCalled()
})
it.each(['service', 'control', 'packet', 'notify'])('bounds %s and never continues a late native result', async stage => {
  const a = adapter(), p = peer(stage), transport = new a.SecureDfuTransport(p.gatt)
  const run = transport.open(false), rejected = expect(run).rejects.toMatchObject({ code: 'NORDIC_NATIVE_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(45000); await rejected
  const calls = p.service.getCharacteristic.mock.calls.length
  p.pending.resolve(stage === 'service' ? p.service : stage === 'packet' ? p.packet : p.control)
  await vi.advanceTimersByTimeAsync(0)
  expect(p.service.getCharacteristic).toHaveBeenCalledTimes(calls)
  expect(p.control.addEventListener).not.toHaveBeenCalled()
  expect(p.control.writeValueWithResponse).not.toHaveBeenCalled()
  expect(p.gatt.connected).toBe(false)
})
it('retains the just-validated same-grant connection instead of disconnect/reconnect teardown', async () => {
  const a = adapter(), p = peer()
  await a.validateBootloaderDevice(p.device, p.device.name)
  expect(p.gatt.disconnect).not.toHaveBeenCalled()
  const boundary = new Error('verified transfer boundary')
  a.SecureDfuTransport.prototype.transfer = vi.fn().mockRejectedValue(boundary)
  await expect(a.transferDevice(p.device, {}, () => {})).rejects.toBe(boundary)
  expect(p.gatt.connect).not.toHaveBeenCalled()
  expect(a.SecureDfuTransport.prototype.transfer).toHaveBeenCalledOnce()
})
it('does not restart the45s verification budget after a resolved missing-channel retry', async () => {
  const a = adapter(), p = peer()
  p.gatt.connected = false
  p.gatt.connect.mockImplementationOnce(async () => {
    p.gatt.connected = true
    return p.gatt
  }).mockImplementation(() => p.pending.promise)
  p.service.getCharacteristic.mockImplementationOnce(async () => {
    await new Promise(resolve => setTimeout(resolve, 44000))
    throw new DOMException('No Characteristics matching UUID', 'NotFoundError')
  })
  const run = a.validateBootloaderDevice(p.device, p.device.name)
  const rejected = expect(run).rejects.toMatchObject({ code: 'NORDIC_NATIVE_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(45000); await rejected
  expect(p.gatt.connect).toHaveBeenCalledTimes(2)
  expect(p.control.writeValueWithResponse).not.toHaveBeenCalled()
})
it('does not retry a hung connect or disconnect a newer owner on late completion', async () => {
  const a = adapter(), p = peer('connect'); p.gatt.connected = false
  const run = a.transferDevice(p.device, {}, () => {}), rejected = expect(run).rejects.toMatchObject({ code: 'NORDIC_NATIVE_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(10000); await rejected
  expect(p.gatt.connect).toHaveBeenCalledOnce()
  new a.SecureDfuTransport(p.gatt) // a newer operation owns this exact grant
  p.gatt.connected = true
  const disconnects = p.gatt.disconnect.mock.calls.length
  p.pending.resolve(p.gatt); await vi.advanceTimersByTimeAsync(0)
  expect(p.gatt.disconnect).toHaveBeenCalledTimes(disconnects)
  expect(p.gatt.getPrimaryService).not.toHaveBeenCalled()
})
it('bounds a native control write even if its product response already arrived, without replay', async () => {
  const a = adapter(), p = peer(), transport = new a.SecureDfuTransport(p.gatt)
  transport.control = p.control
  p.control.writeValueWithResponse.mockImplementationOnce(() => {
    transport.handleNotification({ target: { value: new DataView(new Uint8Array([96, 4, 1]).buffer) } })
    return p.pending.promise
  })
  const run = transport.request(4), rejected = expect(run).rejects.toMatchObject({ code: 'NORDIC_NATIVE_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(15000); await rejected
  await transport.close()
  expect(p.control.writeValueWithResponse).toHaveBeenCalledOnce()
  expect(p.control.stopNotifications).not.toHaveBeenCalled()
  expect(p.gatt.connected).toBe(false)
  expect(() => new a.SecureDfuTransport(p.gatt)).toThrow('上一笔 Nordic 原生写入尚未返回')
  p.pending.resolve(); await vi.advanceTimersByTimeAsync(0)
  expect(p.control.writeValueWithResponse).toHaveBeenCalledOnce()
  expect(() => new a.SecureDfuTransport(p.gatt)).not.toThrow()
})
it('does not restart a firmware object after a native packet timeout', async () => {
  const a = adapter(), p = peer(), transport = new a.SecureDfuTransport(p.gatt)
  transport.packet = p.packet
  transport.request = vi.fn().mockResolvedValue(new Uint8Array())
  p.packet.writeValueWithoutResponse.mockReturnValue(p.pending.promise)
  const run = transport.sendFirmware(new Uint8Array(8), { maxSize: 8, offset: 0 })
  const rejected = expect(run).rejects.toMatchObject({ code: 'NORDIC_NATIVE_TIMEOUT' })
  await vi.advanceTimersByTimeAsync(15000); await rejected
  expect(transport.request).toHaveBeenCalledOnce() // one CREATE, no EXECUTE/re-CREATE
  expect(p.packet.writeValueWithoutResponse).toHaveBeenCalledOnce()
})
it('keeps signed INIT response budget60s and bounds a hung notification cleanup', async () => {
  const a = adapter(), p = peer('close'), transport = new a.SecureDfuTransport(p.gatt)
  transport.control = p.control
  const run = transport.request(4, new Uint8Array(), 60000)
  await vi.advanceTimersByTimeAsync(17000)
  transport.handleNotification({ target: { value: new DataView(new Uint8Array([96, 4, 1]).buffer) } })
  await expect(run).resolves.toHaveLength(0)
  const close = transport.close(); await vi.advanceTimersByTimeAsync(3000); await close
  expect(p.control.stopNotifications).toHaveBeenCalledOnce()
  expect(p.gatt.connected).toBe(false)
})
it('reads object checkpoints with only SELECT controls, never PRN/CREATE/EXECUTE/ABORT or packet data', async () => {
  const a = adapter(), p = peer()
  p.control.writeValueWithResponse.mockImplementation(async (bytes: Uint8Array) => {
    expect(bytes[0]).toBe(6)
    const reply = new Uint8Array(15); reply.set([96, 6, 1])
    const view = new DataView(reply.buffer)
    view.setUint32(3, 4096, true); view.setUint32(7, bytes[1] === 1 ? 128 : 0, true)
    const listener = p.control.addEventListener.mock.calls[0][1]
    listener({ target: { value: new DataView(reply.buffer) } })
  })
  await expect(a.queryRecoveryState(p.gatt)).resolves.toEqual({
    command: { maxSize: 4096, offset: 128, crc: 0 }, data: { maxSize: 4096, offset: 0, crc: 0 },
  })
  expect(p.control.writeValueWithResponse.mock.calls.map((call: any[]) => Array.from(call[0]))).toEqual([[6, 1], [6, 2]])
  expect(p.packet.writeValueWithoutResponse).not.toHaveBeenCalled()
})
