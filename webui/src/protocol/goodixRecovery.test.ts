import { afterEach, describe, expect, it, vi } from 'vitest'
import { goodixFixture as fixture, never, deferred } from './test-support/goodixOta'
import { assertGoodixOtaAttestation, syncClockAfterVerifiedOta } from './ota'

afterEach(() => vi.useRealTimers())

describe('Goodix bounded native stages', () => {
  it.each(['connect', 'service', 'channel', 'notify', 'control'] as const)('stops a pending %s before PROGRAM, including late completion', async stage => {
    vi.useFakeTimers()
    const f = fixture(), pending = deferred()
    let lateValue: any = undefined
    if (stage === 'connect') { f.server.connected = false; f.server.connect.mockReturnValueOnce(pending.promise); lateValue = f.server }
    if (stage === 'service') { f.server.getPrimaryService.mockReturnValueOnce(pending.promise); lateValue = f.service }
    if (stage === 'channel') { f.service.getCharacteristic.mockReturnValueOnce(pending.promise); lateValue = f.tx }
    if (stage === 'notify') { f.tx.startNotifications.mockReturnValueOnce(pending.promise); lateValue = f.tx }
    if (stage === 'control') f.ctrl.writeValueWithoutResponse.mockReturnValueOnce(pending.promise)
    const phases: string[] = []
    const result = expect(f.ota.transferDevice(f.device, f.pkg, vi.fn(), (s: string) => phases.push(s)))
      .rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(stage === 'service' ? 45001 : 11000)
    await result
    const count = f.rx.writeValueWithoutResponse.mock.calls.length
    pending.resolve(lateValue)
    await vi.advanceTimersByTimeAsync(60000)
    expect(f.commands).not.toContain(0x23)
    expect(f.rx.writeValueWithoutResponse).toHaveBeenCalledTimes(count)
    expect(phases.some(s => s.includes('开始'))).toBe(true)
    expect(f.server.connect.mock.calls.length).toBeLessThanOrEqual(1)
  })

  it('quarantines an abandoned discovery until settlement, then permits a new owner', async () => {
    vi.useFakeTimers()
    const f = fixture(), old = deferred(), newer = deferred()
    f.server.getPrimaryService.mockReturnValueOnce(old.promise).mockReturnValueOnce(newer.promise)
    const first = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(45001); await first
    await expect(f.ota.validateDevice(f.device)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_PENDING' })
    expect(f.server.getPrimaryService).toHaveBeenCalledTimes(1)
    old.resolve(f.service)
    await vi.advanceTimersByTimeAsync(1)
    const second = expect(f.ota.validateDevice(f.device)).resolves.toBe(f.device)
    await vi.advanceTimersByTimeAsync(1)
    expect(f.ctrl.writeValueWithoutResponse).not.toHaveBeenCalled()
    const disconnects = f.server.disconnect.mock.calls.length
    newer.resolve(f.service); await second
    expect(f.server.disconnect).toHaveBeenCalledTimes(disconnects)
  })

  it('bounds cleanup after successful PROGRAM_END without replay', async () => {
    vi.useFakeTimers()
    const f = fixture()
    f.tx.stopNotifications.mockImplementation(never)
    const result = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(5000); await result
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
    expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
  })

  it.each([false, true])('continues to App attestation after END ACK and a settled native disconnect (App handoff=%s)', async handoff => {
    vi.useFakeTimers()
    const f = fixture(), phases: string[] = []
    const sync = vi.fn(async () => undefined)
    const payload = { ...f.pkg, application: { ...f.pkg.application, size: f.pkg.firmware.length, checksum: 0 } }
    if (handoff) {
      f.target[5] = 1
      const disconnect = f.server.disconnect.getMockImplementation()!
      f.server.disconnect.mockImplementation(() => { disconnect(); f.target[5] = 0 })
    }
    f.tx.stopNotifications.mockImplementation(async () => {
      if (!f.commands.includes(0x25)) return
      f.server.connected = false
      throw new DOMException('GATT Server is disconnected', 'NetworkError')
    })
    const flow = (async () => {
      if (handoff) await f.ota.enterRecovery(f.server)
      return await f.ota.transferDevice(f.device, payload, vi.fn(), (s: string) => phases.push(s))
    })().then(async (result: any) => {
      expect(result).toMatchObject({ applicationMode: false, requiresRecovery: false })
      assertGoodixOtaAttestation({ version: 1, appBytes: payload.application.size, checksum: 0 }, payload)
      return await syncClockAfterVerifiedOta(sync)
    })
    const result = expect(flow).resolves.toBeNull()
    await vi.runAllTimersAsync(); await result
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
    expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
    expect(phases).toContain('Goodix OTA：PROGRAM_END 已确认，设备已断开，继续应用验身')
    expect(sync).toHaveBeenCalledTimes(1)
  })

  it('still rejects the wrong returning App before clock sync after settled cleanup', async () => {
    vi.useFakeTimers()
    const f = fixture(), sync = vi.fn(async () => undefined)
    const payload = { ...f.pkg, application: { ...f.pkg.application, size: f.pkg.firmware.length, checksum: 0 } }
    f.tx.stopNotifications.mockImplementation(async () => {
      f.server.connected = false
      throw new DOMException('GATT Server is disconnected', 'NetworkError')
    })
    const flow = f.ota.transferDevice(f.device, payload).then(async () => {
      assertGoodixOtaAttestation({ version: 1, appBytes: payload.application.size - 1, checksum: 0 }, payload)
      return await syncClockAfterVerifiedOta(sync)
    })
    const result = expect(flow).rejects.toThrow('应用与升级包不一致')
    await vi.runAllTimersAsync(); await result
    expect(sync).not.toHaveBeenCalled()
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
  })

  it.each(['connected-network-error', 'disconnected-permission-error'] as const)('does not forgive %s during cleanup', async failure => {
    vi.useFakeTimers()
    const f = fixture()
    f.tx.stopNotifications.mockImplementation(async () => {
      if (failure === 'disconnected-permission-error') f.server.connected = false
      throw new DOMException('cleanup refused', failure === 'connected-network-error' ? 'NetworkError' : 'NotAllowedError')
    })
    const result = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toThrow('cleanup refused')
    await vi.runAllTimersAsync(); await result
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
    expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
  })

  it.each(['rejected', 'missing-response'] as const)('does not bypass %s PROGRAM_END for cleanup or time sync', async failure => {
    vi.useFakeTimers()
    const f = fixture(), attest = vi.fn(), sync = vi.fn()
    const write = f.rx.writeValueWithoutResponse.getMockImplementation()!
    let ends = 0
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
      if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === 0x25) {
        ends++
        if (failure === 'rejected') {
          const reply = f.ota.frame(0x25, new Uint8Array([0]))
          const listener = f.tx.addEventListener.mock.calls.at(-1)![1]
          queueMicrotask(() => listener({ target: { value: new DataView(reply.buffer) } }))
        }
        return
      }
      return write(bytes)
    })
    const flow = f.ota.transferDevice(f.device, f.pkg).then(async () => { attest(); await syncClockAfterVerifiedOta(sync) })
    const result = expect(flow).rejects.toThrow()
    await vi.runAllTimersAsync(); await result
    expect(ends).toBe(1)
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
    expect(attest).not.toHaveBeenCalled()
    expect(sync).not.toHaveBeenCalled()
  })

  it('allows slow service discovery within the original 45-second budget', async () => {
    vi.useFakeTimers()
    const f = fixture(), service = deferred(), phases: string[] = []
    f.server.getPrimaryService.mockReturnValueOnce(service.promise)
    const result = expect(f.ota.transferDevice(f.device, f.pkg, vi.fn(), (s: string) => phases.push(s)))
      .resolves.toMatchObject({ applicationMode: false })
    await vi.advanceTimersByTimeAsync(11000)
    service.resolve(f.service)
    await vi.runAllTimersAsync(); await result
    expect(phases).toContain('Goodix OTA：发现升级服务完成（11000 ms）')
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
  })

  it.each(['notify', 'write', 'program', 'cleanup'] as const)('quarantines unresolved %s across wrappers of the same grant, then releases on settlement', async stage => {
    vi.useFakeTimers()
    const f = fixture(), native = deferred()
    if (stage === 'notify') f.tx.startNotifications.mockReturnValueOnce(native.promise)
    if (stage === 'write') f.ctrl.writeValueWithoutResponse.mockReturnValueOnce(native.promise)
    if (stage === 'program') {
      const write = f.rx.writeValueWithoutResponse.getMockImplementation()!
      f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
        if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === 0x23) return native.promise
        return write(bytes)
      })
    }
    if (stage === 'cleanup') f.tx.stopNotifications.mockReturnValueOnce(native.promise)
    const first = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(11000); await first
    const wrapper = { ...f.server, connected: false, connect: vi.fn(async () => wrapper) }
    const secondDevice = { id: f.device.id, gatt: wrapper }
    await expect(f.ota.validateDevice(secondDevice)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_WRITE_PENDING' })
    expect(wrapper.connect).not.toHaveBeenCalled()
    native.resolve(undefined)
    await vi.advanceTimersByTimeAsync(1)
    await expect(f.ota.validateDevice(secondDevice)).resolves.toBe(secondDevice)
    expect(wrapper.connect).toHaveBeenCalledTimes(1)
    expect(f.commands.filter(c => c === 0x23).length).toBeLessThanOrEqual(1)
  })

  it('keeps connect and slow service discovery within one 45-second budget', async () => {
    vi.useFakeTimers()
    const f = fixture(), connect = deferred()
    f.server.connected = false
    f.server.connect.mockReturnValueOnce(connect.promise)
    f.server.getPrimaryService.mockImplementation(never)
    let settled = false
    const result = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    void result.then(() => { settled = true })
    await vi.advanceTimersByTimeAsync(9000)
    f.server.connected = true
    connect.resolve(f.server)
    await vi.advanceTimersByTimeAsync(35999)
    expect(settled).toBe(false)
    await vi.advanceTimersByTimeAsync(2); await result
    expect(f.commands).toEqual([])
  })

  it.each(['queryRecovery', 'resumeApplication', 'transferServer', 'enterRecovery'] as const)('keeps public %s behind same-grant pending-write quarantine', async entry => {
    vi.useFakeTimers()
    const f = fixture(), native = deferred()
    const write = f.rx.writeValueWithoutResponse.getMockImplementation()!
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
      if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === 0x23) return native.promise
      return write(bytes)
    })
    const first = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(11000); await first
    const wrapper = { ...f.server, connected: true, device: { id: f.device.id } }
    const reads = f.server.getPrimaryService.mock.calls.length
    const writes = f.ctrl.writeValueWithoutResponse.mock.calls.length
    await expect(f.ota[entry](wrapper, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_WRITE_PENDING' })
    expect(f.server.getPrimaryService).toHaveBeenCalledTimes(reads)
    expect(f.ctrl.writeValueWithoutResponse).toHaveBeenCalledTimes(writes)
    native.resolve(undefined)
    await vi.advanceTimersByTimeAsync(1)
    const unlocked = expect(f.ota.queryRecovery(wrapper)).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await unlocked
  })

  it('retries only a definite pre-command native connection refusal', async () => {
    vi.useFakeTimers()
    const f = fixture()
    f.server.connected = false
    f.server.connect.mockRejectedValueOnce(new DOMException('not ready', 'NetworkError'))
    const result = expect(f.ota.transferDevice(f.device, f.pkg)).resolves.toMatchObject({ applicationMode: false })
    await vi.runAllTimersAsync(); await result
    expect(f.server.connect).toHaveBeenCalledTimes(2)
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
  })

  it('does not retry an explicit permission refusal', async () => {
    vi.useFakeTimers()
    const f = fixture()
    f.server.connected = false
    f.server.connect.mockRejectedValueOnce(new DOMException('denied', 'NotAllowedError'))
    const result = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toThrow('denied')
    await vi.runAllTimersAsync(); await result
    expect(f.server.connect).toHaveBeenCalledTimes(1)
    expect(f.commands).toEqual([])
  })
})

describe('Goodix firmware mutations are never replayed', () => {
  it.each([0x23, 0x24, 0x25])('keeps unknown opcode %i terminal after an App-to-Recovery handoff', async opcode => {
    vi.useFakeTimers()
    const f = fixture()
    f.target[5] = 1
    const originalDisconnect = f.server.disconnect.getMockImplementation()!
    f.server.disconnect.mockImplementation(() => { originalDisconnect(); f.target[5] = 0 })
    const native = f.rx.writeValueWithoutResponse.getMockImplementation()!
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
      // Each tested firmware fits one frame except FLASH, where failure at
      // its first fragment is already an unknown native write outcome.
      if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === opcode) throw new DOMException('lost native write', 'NetworkError')
      return native(bytes)
    })
    const flow = (async () => {
      await f.ota.enterRecovery(f.server)
      return await f.ota.transferDevice(f.device, f.pkg)
    })()
    const result = expect(flow).rejects.toThrow('lost native write')
    await vi.runAllTimersAsync(); await result
    expect(f.server.connect).toHaveBeenCalledTimes(1)
    expect(f.commands.filter(c => c === 0x23).length).toBeLessThanOrEqual(1)
    expect(f.commands.filter(c => c === 0x25).length).toBeLessThanOrEqual(1)
  })

  it.each(['pending-native', 'missing-response'] as const)('stops %s PROGRAM_START without a second mutation', async failure => {
    vi.useFakeTimers()
    const f = fixture()
    const native = f.rx.writeValueWithoutResponse.getMockImplementation()!
    let starts = 0
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
      if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === 0x23) {
        starts++
        if (failure === 'pending-native') return never()
        return
      }
      return native(bytes)
    })
    const result = expect(f.ota.transferDevice(f.device, f.pkg)).rejects.toThrow(/超时|timed out/)
    await vi.advanceTimersByTimeAsync(50000); await result
    expect(starts).toBe(1)
    expect(f.server.connect).not.toHaveBeenCalled()
    expect(f.commands).not.toContain(0x24)
    expect(f.commands).not.toContain(0x25)
  })
})
