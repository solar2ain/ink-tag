import { afterEach, describe, expect, it, vi } from 'vitest'

import telinkSource from '../ota-drivers/telink_ota.js?raw'

type TelinkOtaTestAdapter = {
  fastBlockLimit(status: Uint8Array): number
  fastPacketEnd(start: number, total: number, blockLimit: number): number
  fastPacketWindow(start: number, total: number, blockLimit: number): Array<[number, number]>
  packetRequiresDeliveryAck(index: number): boolean
  characteristicSupportsWriteResponse(characteristic: unknown): boolean
  isTransientGattError(error: unknown): boolean
  dataPacket(index: number, payload: Uint8Array): Uint8Array
  queryRecovery(server: BluetoothRemoteGATTServer): Promise<{ status: number; raw: Uint8Array }>
  resumeApplication(server: BluetoothRemoteGATTServer): Promise<void>
  transferDevice(device: unknown, pkg: { firmware: Uint8Array }, progress: (...args: number[]) => void): Promise<void>
}

function loadAdapter(): TelinkOtaTestAdapter {
  const scope: { TelinkOta?: TelinkOtaTestAdapter } = {}
  new Function('window', telinkSource)(scope)
  if (!scope.TelinkOta) throw new Error('Telink OTA adapter did not register')
  return scope.TelinkOta
}

describe('TLSR8359 OTA flow-control window', () => {
  it('requires release-v1 Recovery large packets and preserves sector boundaries', () => {
    const ota = loadAdapter()
    const legacy = new Uint8Array(8)
    const fast = new Uint8Array([1, 0, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 1, 14, 0, 0])
    expect(() => ota.fastBlockLimit(legacy)).toThrow(/Recovery/)
    expect(ota.fastBlockLimit(fast)).toBe(14)
    expect(ota.fastPacketEnd(0, 1000, 14)).toBe(2)
    expect(ota.fastPacketEnd(1, 1000, 14)).toBe(15)
    expect(ota.fastPacketEnd(250, 1000, 14)).toBe(256)
    expect(ota.fastPacketEnd(256, 1000, 14)).toBe(257)
    expect(ota.fastPacketWindow(1, 1000, 14)).toEqual([[1, 15], [15, 29]])
    expect(ota.fastPacketWindow(250, 1000, 14)).toEqual([[250, 256]])
    expect(ota.fastPacketWindow(256, 1000, 14)).toEqual([[256, 257]])
    expect(ota.packetRequiresDeliveryAck(0)).toBe(true)
    expect(ota.packetRequiresDeliveryAck(256)).toBe(true)
    expect(ota.packetRequiresDeliveryAck(255)).toBe(false)
    expect(ota.characteristicSupportsWriteResponse({ properties: { write: true } })).toBe(true)
    expect(ota.characteristicSupportsWriteResponse({ properties: { write: false } })).toBe(false)
    expect(ota.isTransientGattError({ name: 'NetworkError', message: 'Unreachable' })).toBe(true)
    expect(ota.isTransientGattError(new Error('Recovery rejected image'))).toBe(false)
    expect(ota.dataPacket(1, new Uint8Array(14 * 16)).length).toBe(228)
  })
})

describe('TLSR shipping driver notification ownership', () => {
  afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers() })

  function transfer(options: { trusted?: boolean; receiverStart?: number; entryProof?: boolean; notify?: boolean; notifyCapability?: boolean; readEvents?: boolean; refusal?: number; startFailure?: boolean; bytes?: number; delayedAck?: boolean; aheadAck?: boolean; dropNotificationAt?: number; terminalNotify?: boolean; terminalDelay?: number; terminalStatus?: Uint8Array; endFailure?: boolean } = {}) {
    const firmware = new Uint8Array(options.bytes ?? (options.trusted ? 512 : 160))
    const fw = new DataView(firmware.buffer)
    const appBytes = firmware.length - 96
    fw.setUint32(24, appBytes - 4, true)
    fw.setUint32(appBytes + 12, 1, true)
    fw.setUint32(appBytes + 16, 0x53484c54, true)
    fw.setUint32(appBytes + 20, 0x31564c54, true)
    const context = new Uint8Array(20)
    context.set([0x49, 0x54, 0x43, 0x31])
    const cv = new DataView(context.buffer)
    cv.setUint32(4, 0x3f000, true); cv.setUint32(8, 1, true)
    cv.setUint32(12, 0x31564c54, true); cv.setUint32(16, 0x53484c54, true)
    const proof = (start: number) => {
      const raw = new Uint8Array(20), view = new DataView(raw.buffer)
      raw.set([0x49,0x41,0x42,0x31]); [start,0x3f000,0x40000,0x11000].forEach((n,i) => view.setUint32(4+i*4,n,true))
      return raw
    }
    if (options.trusted) firmware.set(proof(0x1000),0x140)
    const status = new Uint8Array([0, 0, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 1, 14, 1, 0])
    const events: Uint8Array[] = []
    const listeners = new Set<(event: { target: { value: DataView } }) => void>()
    const emit = (raw: Uint8Array = status) => {
      const snapshot = raw.slice()
      events.push(snapshot)
      const value = new DataView(snapshot.buffer)
      listeners.forEach(listener => listener({ target: { value } }))
      return value
    }
    let packetPending = false
    let endSent = false, readsAfterEnd = 0
    const lifecycle: string[] = []
    const write = vi.fn(async (bytes: Uint8Array) => {
      if (bytes[0] === 1 && bytes[1] === 255) status[0] = 1
      else if (bytes[0] === 2 && bytes[1] === 255) {
        endSent = true; status[0] = 2; lifecycle.push('end')
        if (options.endFailure) throw new DOMException('device disconnected during END', 'NetworkError')
        if (options.terminalNotify !== false) {
          const terminal = () => {
            lifecycle.push('terminal'); emit(options.terminalStatus ?? status)
          }
          if (options.terminalDelay) setTimeout(terminal, options.terminalDelay)
          else terminal()
        }
        return
      }
      else {
        if (packetPending) { status[0] = 3; emit(); return }
        status[0] = options.refusal ?? 1
        new DataView(status.buffer).setUint16(1, bytes[0] + bytes[1] * 256 + (bytes.length - 4) / 16 + (options.aheadAck ? 1 : 0), true)
        if ((bytes[0] | bytes[1] << 8) === options.dropNotificationAt) return
        if (options.delayedAck) {
          packetPending = true
          setTimeout(() => { packetPending = false; emit() }, 10)
          return
        }
      }
      if (options.notify !== false) emit()
    })
    const characteristic = {
      service: {} as { getCharacteristic: (uuid: string) => Promise<unknown> },
      properties: { notify: options.notifyCapability !== false, write: true },
      addEventListener: vi.fn((_name: string, listener: (event: { target: { value: DataView } }) => void) => listeners.add(listener)),
      removeEventListener: vi.fn((_name: string, listener: (event: { target: { value: DataView } }) => void) => listeners.delete(listener)),
      startNotifications: vi.fn(async () => { lifecycle.push('subscribe'); if (options.startFailure) throw new Error('CCCD failed') }),
      stopNotifications: vi.fn(async () => { lifecycle.push('unsubscribe') }),
      readValue: vi.fn(async () => {
        if (endSent) ++readsAfterEnd
        return options.readEvents === false ? new DataView(status.slice().buffer) : emit()
      }),
      writeValueWithResponse: write, writeValueWithoutResponse: write,
    }
    const service = { getCharacteristic: async (uuid: string) => {
      if (uuid.endsWith('2b13')) return { properties: { read: true }, readValue: async () => new DataView(context.buffer) }
      if (uuid.endsWith('2b14')) {
        if (options.entryProof === false || (!options.trusted && options.receiverStart === undefined)) throw new DOMException('not found','NotFoundError')
        const bytes = proof(options.receiverStart ?? 0x1000)
        return { properties: { read: true }, readValue: async () => new DataView(bytes.buffer) }
      }
      return characteristic
    } }
    characteristic.service = service
    const device = { id: 'exact-TL-grant', gatt: { getPrimaryService: async () => service, connected: false, disconnect() { lifecycle.push('disconnect'); this.connected = false }, async connect() {
      this.connected = true
      return this
    } } }
    return { device, firmware, events, write, characteristic, listeners, lifecycle, readsAfterEnd: () => readsAfterEnd }
  }

  it.each([[true, 0], [false, 0x1000]] as const)('refuses mixed signed/receiver entry %s/%s before BEGIN', async (trusted, receiverStart) => {
    const run = transfer({ trusted, receiverStart })
    await expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow(/Full/)
    expect(run.write).not.toHaveBeenCalled()
    expect(run.characteristic.startNotifications).not.toHaveBeenCalled()
  })

  it('refuses a new signed entry when the receiver has no address proof, before BEGIN', async () => {
    const run = transfer({ trusted: true, entryProof: false })
    await expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow(/Full/)
    expect(run.write).not.toHaveBeenCalled()
    expect(run.characteristic.startNotifications).not.toHaveBeenCalled()
  })

  it('accepts exact v1 address binding through the separate read-only entry proof', async () => {
    vi.useFakeTimers()
    const run = transfer({ trusted: true })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(run.write).toHaveBeenCalledTimes(6)
    expect(run.lifecycle).toEqual(['subscribe', 'end', 'terminal', 'unsubscribe', 'disconnect'])
  })

  it.each([true, false])('completes from IDLE with readValue events=%s, without replaying BEGIN or data', async readEvents => {
    vi.useFakeTimers()
    const run = transfer({ readEvents })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(run.write.mock.calls.map(([bytes]) => Array.from(bytes.slice(0, 2)))).toEqual([[1, 255], [0, 0], [2, 0], [2, 255]])
    expect(run.listeners.size).toBe(0)
    expect(run.device.gatt.connected).toBe(false)
  })

  it('falls back to indexed reads when notifications are absent and detaches the event queue', async () => {
    vi.useFakeTimers()
    const run = transfer({ notify: false })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(run.write).toHaveBeenCalledTimes(4)
    expect(run.listeners.size).toBe(0)
  })

  it('recovers after one missed notification without replay or a permanent polling slowdown', async () => {
    vi.useFakeTimers()
    const timers = vi.spyOn(globalThis, 'setTimeout')
    const run = transfer({ bytes: 9000, dropNotificationAt: 0, readEvents: false })
    const metrics = vi.fn()
    const adapter = loadAdapter() as any, pkg = { firmware: run.firmware }
    const handle = await adapter.acquireRecoverySession({ device: run.device, generation: 1,
      isCurrent: () => true, onDiagnostics: metrics }, pkg)
    const result = expect(adapter.transferRecoverySession(handle, pkg, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(metrics).toHaveBeenCalledWith(expect.objectContaining({ notificationFallbacks: 1, notificationRecoveries: 1 }))
    expect(timers.mock.calls.filter(([, ms]) => ms === 300)).toHaveLength(1)
    expect(run.write.mock.calls.filter(([b]) => b[0] === 1 && b[1] === 255)).toHaveLength(1)
    const indices = run.write.mock.calls.map(([b]) => b[0] | b[1] << 8).filter(n => n < 0xff00)
    expect(new Set(indices).size).toBe(indices.length)
    expect(run.listeners.size).toBe(0)
  })

  it.each([0, 6, 9])('preserves actual data refusal %s without replay, and detaches its listener', async refusal => {
    vi.useFakeTimers()
    const run = transfer({ refusal })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow('拒绝传输')
    await vi.runAllTimersAsync(); await result
    expect(run.write).toHaveBeenCalledTimes(2)
    expect(run.listeners.size).toBe(0)
  })

  it('cleans up a failed CCCD subscription without sending data', async () => {
    vi.useFakeTimers()
    const run = transfer({ startFailure: true })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow('CCCD failed')
    await vi.runAllTimersAsync(); await result
    expect(run.write).not.toHaveBeenCalled()
    expect(run.listeners.size).toBe(0)
  })
  it('waits for each packet commit when WinRT cannot queue a second write', async () => {
    vi.useFakeTimers()
    const run = transfer({ bytes: 768, delayedAck: true })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(run.write.mock.calls.map(([bytes]) => Array.from(bytes.slice(0, 2))))
      .toEqual([[1, 255], [0, 0], [2, 0], [16, 0], [30, 0], [44, 0], [2, 255]])
    expect(run.listeners.size).toBe(0)
  })

  it.each([false, true])('uses exact notification commits without fixed sector sleeps, delayed=%s', async delayedAck => {
    vi.useFakeTimers()
    const timers = vi.spyOn(globalThis, 'setTimeout')
    const run = transfer({ bytes: 9000, delayedAck })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(timers.mock.calls.filter(([, ms]) => ms === 300)).toHaveLength(0)
    expect(run.write.mock.calls.filter(([b]) => b[0] === 1 && b[1] === 255)).toHaveLength(1)
    expect(run.listeners.size).toBe(0)
  })

  it('refuses a no-NOTIFY peer before BEGIN instead of failing after programming', async () => {
    vi.useFakeTimers()
    const timers = vi.spyOn(globalThis, 'setTimeout')
    const run = transfer({ bytes: 9000, notifyCapability: false })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow(/必须支持终态通知/)
    await vi.runAllTimersAsync(); await result
    expect(timers.mock.calls.filter(([, ms]) => ms === 300)).toHaveLength(0)
    expect(run.characteristic.startNotifications).not.toHaveBeenCalled()
    expect(run.write).not.toHaveBeenCalled()
  })

  it('keeps the opening-sector erase allowance when its notification is lost', async () => {
    vi.useFakeTimers()
    const timers = vi.spyOn(globalThis, 'setTimeout')
    const run = transfer({ bytes: 9000, notify: false })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(timers.mock.calls.filter(([, ms]) => ms === 300)).toHaveLength(3)
    expect(run.listeners.size).toBe(0)
  })

  it('rejects a future indexed ACK without FINISH, BEGIN replay or reconnect', async () => {
    vi.useFakeTimers()
    const run = transfer({ aheadAck: true })
    const result = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow(/确认/)
    await vi.runAllTimersAsync(); await result
    expect(run.write.mock.calls.map(([b]) => Array.from(b.slice(0, 2)))).toEqual([[1, 255], [0, 0]])
    expect(run.listeners.size).toBe(0)
  })
  it('keeps the exact link until a delayed terminal notification, without reading COMPLETE', async () => {
    vi.useFakeTimers()
    const run = transfer({ terminalDelay: 1000, readEvents: true })
    let finished = false
    const pending = loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())
    const completion = pending.then(() => { finished = true })
    await vi.advanceTimersByTimeAsync(500)
    expect(finished).toBe(false)
    expect(run.device.gatt.connected).toBe(true)
    expect(run.readsAfterEnd()).toBe(0)
    await vi.runAllTimersAsync(); await completion
    expect(run.lifecycle).toEqual(['subscribe', 'end', 'terminal', 'unsubscribe', 'disconnect'])
    expect(run.listeners.size).toBe(0)
  })
  it('cached read=COMPLETE without a terminal notification is UNKNOWN and never replayed', async () => {
    vi.useFakeTimers()
    const adapter = loadAdapter(), run = transfer({ terminalNotify: false, readEvents: true })
    const pending = expect(adapter.transferDevice(run.device, { firmware: run.firmware }, vi.fn()))
      .rejects.toMatchObject({ code: 'TLSR_FINALIZATION_UNKNOWN' })
    await vi.runAllTimersAsync(); await pending
    expect(run.readsAfterEnd()).toBe(0)
    expect(run.write.mock.calls.filter(([b]) => b[0] === 2 && b[1] === 255)).toHaveLength(1)
    expect(adapter.isTransientGattError({ name: 'NetworkError', message: 'device disconnected',
      code: 'TLSR_FINALIZATION_UNKNOWN' })).toBe(false)
  })
  it('a disconnect before terminal ACK is UNKNOWN, never a completion', async () => {
    vi.useFakeTimers()
    const run = transfer({ terminalNotify: false })
    const original = run.write.getMockImplementation()!
    run.write.mockImplementation(async bytes => {
      await original(bytes)
      if (bytes[0] === 2 && bytes[1] === 255) run.device.gatt.connected = false
    })
    const pending = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn()))
      .rejects.toMatchObject({ code: 'TLSR_FINALIZATION_UNKNOWN' })
    await vi.runAllTimersAsync(); await pending
    expect(run.characteristic.stopNotifications).not.toHaveBeenCalled()
    expect(run.readsAfterEnd()).toBe(0)
    expect(run.write.mock.calls.filter(([b]) => b[0] === 2 && b[1] === 255)).toHaveLength(1)
  })
  it('an unknown END write never becomes a transient DATA retry', async () => {
    vi.useFakeTimers()
    const run = transfer({ endFailure: true })
    const pending = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn()))
      .rejects.toMatchObject({ code: 'TLSR_FINALIZATION_UNKNOWN' })
    await vi.runAllTimersAsync(); await pending
    expect(run.write.mock.calls.filter(([b]) => b[0] === 1 && b[1] === 255)).toHaveLength(1)
    expect(run.write.mock.calls.filter(([b]) => b[0] === 2 && b[1] === 255)).toHaveLength(1)
  })
  it.each([new Uint8Array(13), new Uint8Array(14),
    new Uint8Array([2, 11, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 1, 14, 1, 0])])('rejects malformed/wrong-index terminal data', async terminalStatus => {
    vi.useFakeTimers()
    const run = transfer({ terminalStatus })
    const pending = expect(loadAdapter().transferDevice(run.device, { firmware: run.firmware }, vi.fn())).rejects.toThrow(/Recovery/)
    await vi.runAllTimersAsync(); await pending
    expect(run.readsAfterEnd()).toBe(0)
    expect(run.write.mock.calls.filter(([b]) => b[0] === 2 && b[1] === 255)).toHaveLength(1)
  })
})

describe('TLSR guarded Recovery maintenance', () => {
  const status = (code = 0) => new Uint8Array([code, 1, 6, 1, 4, 40, 3, 0, 0x49, 0x54, 1, 14, 0, 0])
  function recovery(reads: Uint8Array[], notify = true) {
    type Listener = (event: { target: { value: DataView } }) => void
    const listeners = new Set<Listener>()
    const events: string[] = []
    const emit = (value: Uint8Array) => {
      const event = { target: { value: new DataView(value.buffer, value.byteOffset, value.byteLength) } }
      for (const listener of listeners) listener(event)
    }
    const write = vi.fn(async () => {
      events.push('control')
      if (notify) { emit(status(1)); for (const value of reads) emit(value) }
    })
    const characteristic = { readValue: vi.fn(async () => {
      events.push('read')
      const value = reads.length > 1 ? reads.shift()! : reads[0]!
      return new DataView(value.buffer, value.byteOffset, value.byteLength)
    }), writeValueWithResponse: write,
      addEventListener: (_: string, listener: Listener) => { events.push('listen'); listeners.add(listener) },
      removeEventListener: (_: string, listener: Listener) => { events.push('detach'); listeners.delete(listener) },
      startNotifications: vi.fn(async () => { events.push('subscribe') }),
      stopNotifications: vi.fn(async () => { events.push('unsubscribe') }),
    }
    const getCharacteristic = vi.fn().mockResolvedValue(characteristic)
    const server = { connected: true, getPrimaryService: vi.fn().mockResolvedValue({ getCharacteristic }) } as unknown as BluetoothRemoteGATTServer
    return { server, characteristic, getCharacteristic, write, events, listeners, emit }
  }
  it('LOW_POWER is readable Recovery state, not a connection rejection or voltage measurement', async () => {
    const { server, write } = recovery([status(9)])
    await expect(loadAdapter().queryRecovery(server)).resolves.toMatchObject({ status: 9, raw: status(9) })
    expect(write).not.toHaveBeenCalled()
    expect(server.getPrimaryService).toHaveBeenCalledWith('00010203-0405-0607-0809-0a0b0c0d1912')
  })
  it.each([new Uint8Array(8), new Uint8Array(14), new Uint8Array([0, 0, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 4, 14, 0, 0])])('rejects unknown role/layout before any write', async (raw) => {
    const { server, write } = recovery([raw])
    await expect(loadAdapter().resumeApplication(server)).rejects.toThrow(/Recovery/)
    expect(write).not.toHaveBeenCalled()
  })
  it('sends exactly one 03FF and accepts only explicit SAFE_RESUME_COMPLETE', async () => {
    const { server, write, events, listeners } = recovery([status(), status(8)])
    await loadAdapter().resumeApplication(server)
    expect(write).toHaveBeenCalledTimes(1)
    expect(write).toHaveBeenCalledWith(new Uint8Array([3, 255]))
    expect(events).toEqual(['read', 'listen', 'subscribe', 'control', 'detach', 'unsubscribe'])
    expect(listeners.size).toBe(0)
  })
  it.each([[9, 'LOW_POWER'], [10, 'UNSAFE_RESUME'], [2, '状态']])('a device refusal/status %s never retries or forces reset', async (code, text) => {
    const { server, write } = recovery([status(), status(Number(code))])
    await expect(loadAdapter().resumeApplication(server)).rejects.toThrow(String(text))
    expect(write).toHaveBeenCalledTimes(1)
  })
  it('a missing ACK is unconfirmed and never replays the command', async () => {
    const { server, write } = recovery([status()])
    write.mockRejectedValueOnce(new DOMException('disconnected', 'NetworkError'))
    await expect(loadAdapter().resumeApplication(server)).rejects.toThrow(/结果未知/)
    expect(write).toHaveBeenCalledTimes(1)
  })
  it('a stale subscription event cannot replace the current terminal notification', async () => {
    vi.useFakeTimers()
    const run = recovery([status()], false)
    run.characteristic.startNotifications.mockImplementation(async () => { run.emit(status(8)) })
    const result = expect(loadAdapter().resumeApplication(run.server)).rejects.toThrow(/结果未知/)
    await vi.runAllTimersAsync(); await result
    expect(run.write).toHaveBeenCalledTimes(1)
    expect(run.characteristic.readValue).toHaveBeenCalledTimes(1)
    expect(run.listeners.size).toBe(0)
    run.emit(status(8)) // detached late status cannot change the outcome
  })
  it('accepts a coalesced terminal without RECEIVING and ignores a late initial status', async () => {
    const run = recovery([status()], false)
    run.write.mockImplementation(async () => { run.emit(status(8)); run.emit(status(1)) })
    await expect(loadAdapter().resumeApplication(run.server)).resolves.toBeUndefined()
    expect(run.write).toHaveBeenCalledTimes(1)
  })
  it('does not replay a previous read terminal result', async () => {
    const run = recovery([status(8)], false)
    await expect(loadAdapter().resumeApplication(run.server)).rejects.toThrow(/上一次/)
    expect(run.write).not.toHaveBeenCalled()
    expect(run.characteristic.startNotifications).not.toHaveBeenCalled()
  })
  it.each([new Uint8Array(), status(8).slice(0, 13), new Uint8Array(14),
    new Uint8Array([8, 0, 0, 0, 0, 0, 0, 0, 0x49, 0x54, 4, 14, 0, 0])])('malformed notification fails closed', async raw => {
    const run = recovery([status(), raw])
    await expect(loadAdapter().resumeApplication(run.server)).rejects.toThrow(/结果未知/)
    expect(run.write).toHaveBeenCalledTimes(1)
    expect(run.listeners.size).toBe(0)
  })
  it('subscription failure does not send a control request', async () => {
    const run = recovery([status()])
    run.characteristic.startNotifications.mockRejectedValueOnce(new Error('subscription failed'))
    await expect(loadAdapter().resumeApplication(run.server)).rejects.toThrow(/结果未知/)
    expect(run.write).not.toHaveBeenCalled()
    expect(run.listeners.size).toBe(0)
  })
  it('cleanup failure preserves LOW_POWER and does not replay', async () => {
    const run = recovery([status(), status(9)])
    run.characteristic.stopNotifications.mockRejectedValueOnce(new Error('cleanup failed'))
    await expect(loadAdapter().resumeApplication(run.server)).rejects.toThrow(/LOW_POWER/)
    expect(run.write).toHaveBeenCalledTimes(1)
  })
  it('disconnect without a terminal notification is never an ACK', async () => {
    vi.useFakeTimers()
    const run = recovery([status()], false)
    run.write.mockImplementation(async () => { Object.defineProperty(run.server, 'connected', { value: false }) })
    const result = expect(loadAdapter().resumeApplication(run.server)).rejects.toThrow(/结果未知/)
    await vi.runAllTimersAsync(); await result
    expect(run.characteristic.stopNotifications).not.toHaveBeenCalled()
    expect(run.write).toHaveBeenCalledTimes(1)
  })
})
