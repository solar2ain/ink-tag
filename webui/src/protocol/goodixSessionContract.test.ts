import { afterEach, describe, expect, it, vi } from 'vitest'
import { goodixFixture, deferred } from './test-support/goodixOta'
import { assertGoodixOtaAttestation, syncClockAfterVerifiedOta } from './ota'

afterEach(() => vi.useRealTimers())

const context = (f: ReturnType<typeof goodixFixture>, isCurrent = () => true) =>
  ({ device: f.device, generation: 7, isCurrent, onStage: vi.fn() })

async function acquire(f: ReturnType<typeof goodixFixture>, ctx = context(f)) {
  const result = f.ota.acquireRecoverySession(ctx, f.pkg)
  await vi.advanceTimersByTimeAsync(101)
  return await result
}

describe('Goodix retained-session contract', () => {
  it('quarantines timed-out discovery until native settlement, with no App-mode fallback or replay', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), oldDiscovery = deferred()
    f.server.getPrimaryService.mockReturnValueOnce(oldDiscovery.promise)
    const first = expect(f.ota.acquireRecoverySession(context(f), f.pkg))
      .rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(45001)
    await first
    await expect(f.ota.acquireRecoverySession(context(f), f.pkg))
      .rejects.toMatchObject({ code: 'GOODIX_NATIVE_PENDING' })
    expect(f.server.getPrimaryService).toHaveBeenCalledTimes(1)
    expect(f.commands).toEqual([])
    oldDiscovery.resolve(f.service)
    await vi.advanceTimersByTimeAsync(1)
    const handle = await acquire(f)
    await f.ota.releaseRecoverySession(handle)
    expect(f.commands).not.toContain(0x41)
    expect(f.commands).not.toContain(0x23)
  })

  it('retains the verified live transport and transfers without rediscovery, reopen, CCCD or target query', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), handle = await acquire(f)
    expect(Object.keys(handle)).toEqual([])
    expect(Object.isFrozen(handle)).toBe(true)
    expect(f.tx.stopNotifications).not.toHaveBeenCalled()
    const before = {
      services: f.server.getPrimaryService.mock.calls.length,
      channels: f.service.getCharacteristic.mock.calls.length,
      notices: f.tx.startNotifications.mock.calls.length,
      control: f.ctrl.writeValueWithoutResponse.mock.calls.length,
      targets: f.commands.filter(c => c === 0x42).length,
    }
    const result = expect(f.ota.transferRecoverySession(handle, f.pkg)).resolves.toMatchObject({ endAcknowledged: true })
    await vi.runAllTimersAsync()
    await result
    expect(f.server.getPrimaryService).toHaveBeenCalledTimes(before.services)
    expect(f.service.getCharacteristic).toHaveBeenCalledTimes(before.channels)
    expect(f.tx.startNotifications).toHaveBeenCalledTimes(before.notices)
    expect(f.ctrl.writeValueWithoutResponse).toHaveBeenCalledTimes(before.control)
    expect(f.commands.filter(c => c === 0x42)).toHaveLength(before.targets)
    expect(f.tx.stopNotifications).toHaveBeenCalledTimes(1)
    expect(f.server.connect).not.toHaveBeenCalled()
    expect(f.commands).not.toContain(0x41)
  })

  it('retained safety: known END ACK plus settled disconnect only permits shared App attestation, then one sync', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), sync = vi.fn(async () => undefined), order: string[] = []
    const pkg = { ...f.pkg, application: { ...f.pkg.application, size: f.pkg.firmware.length, checksum: 0 } }
    f.tx.stopNotifications.mockImplementation(async () => {
      expect(f.commands).toContain(0x25)
      f.server.connected = false
      throw new DOMException('GATT Server is disconnected', 'NetworkError')
    })
    const result = expect(f.ota.transferDevice(f.device, pkg).then(async () => {
      order.push('adapter-end')
      // Shared entry must read the real App before invoking this helper. This
      // fixture supplies that read; it does not qualify real reconnect behavior.
      assertGoodixOtaAttestation({ version: 1, appBytes: pkg.application.size, checksum: 0 }, pkg)
      order.push('attested')
      await syncClockAfterVerifiedOta(sync)
      order.push('synced')
    })).resolves.toBeUndefined()
    await vi.runAllTimersAsync()
    await result
    expect(order).toEqual(['adapter-end', 'attested', 'synced'])
    expect(sync).toHaveBeenCalledTimes(1)
    expect(f.commands.filter(c => c === 0x23)).toHaveLength(1)
    expect(f.commands.filter(c => c === 0x25)).toHaveLength(1)
  })

  it.each(['acquireRecoverySession', 'transferDevice'] as const)('refuses App role at %s without implicit vendor mode', async entry => {
    vi.useFakeTimers()
    const f = goodixFixture()
    f.target[5] = 1
    const args = entry === 'transferDevice' ? [f.device, f.pkg] : [context(f), f.pkg]
    const result = expect(f.ota[entry](...args)).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
    await vi.runAllTimersAsync(); await result
    expect(f.commands).not.toContain(0x41)
    expect(f.commands).not.toContain(0x23)
  })

  it.each(['object', 'firmware', 'imageInfo', 'address', 'manifest'] as const)('rejects a changed %s package before programming', async change => {
    vi.useFakeTimers()
    const f = goodixFixture(), handle = await acquire(f)
    let pkg = f.pkg
    if (change === 'object') pkg = { ...f.pkg }
    if (change === 'firmware') pkg.firmware[0] ^= 1
    if (change === 'imageInfo') pkg.imageInfo[0] ^= 1
    if (change === 'address') pkg.application.active_address++
    if (change === 'manifest') pkg.manifest.compatibilityEpoch++
    await expect(f.ota.transferRecoverySession(handle, pkg)).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
    expect(f.commands).not.toContain(0x23)
    await f.ota.releaseRecoverySession(handle)
    expect(f.server.disconnect).toHaveBeenCalledTimes(1)
  })

  it.each(['generation', 'device-id', 'server', 'disconnect-cycle'] as const)('rejects %s changes before programming', async change => {
    vi.useFakeTimers()
    const f = goodixFixture()
    let current = true
    const handle = await acquire(f, context(f, () => current))
    if (change === 'generation') current = false
    if (change === 'device-id') f.device.id = 'another-grant'
    if (change === 'server') f.device.gatt = { ...f.server }
    if (change === 'disconnect-cycle') {
      f.device.dispatchEvent(new Event('gattserverdisconnected'))
      f.server.connected = true // a reconnect does not revive the old lease
    }
    await expect(f.ota.transferRecoverySession(handle, f.pkg)).rejects.toThrow()
    expect(f.commands).not.toContain(0x23)
  })

  it('consumes once and makes owner release idempotent', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), handle = await acquire(f)
    const first = expect(f.ota.transferRecoverySession(handle, f.pkg)).resolves.toMatchObject({ endAcknowledged: true })
    await expect(f.ota.transferRecoverySession(handle, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
    await vi.runAllTimersAsync(); await first
    await f.ota.releaseRecoverySession(handle)
    await f.ota.releaseRecoverySession(handle)
    expect(f.tx.stopNotifications).toHaveBeenCalledTimes(1)
    expect(f.server.disconnect).toHaveBeenCalledTimes(1)
    await expect(f.ota.transferRecoverySession(handle, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
    await expect(f.ota.releaseRecoverySession({})).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
  })

  it('does not let a concurrent same-grant entry replace an acquired owner', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), handle = await acquire(f)
    await expect(f.ota.queryRecovery(f.server)).rejects.toMatchObject({ code: 'GOODIX_SESSION_BUSY' })
    expect(f.server.getPrimaryService).toHaveBeenCalledTimes(1)
    await f.ota.releaseRecoverySession(handle)
  })

  it('keeps release cleanup pending quarantine until native settlement and never closes the next owner', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), pending = deferred(), handle = await acquire(f)
    f.tx.stopNotifications.mockReturnValueOnce(pending.promise)
    const release = expect(f.ota.releaseRecoverySession(handle)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(3001); await release
    await expect(f.ota.acquireRecoverySession(context(f), f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_WRITE_PENDING' })
    pending.resolve(undefined)
    await vi.advanceTimersByTimeAsync(1)
    const next = await acquire(f)
    const disconnects = f.server.disconnect.mock.calls.length
    // Releasing the old handle again returns its terminal result, with no close.
    await expect(f.ota.releaseRecoverySession(handle)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    expect(f.server.disconnect).toHaveBeenCalledTimes(disconnects)
    expect(f.tx.stopNotifications).toHaveBeenCalledTimes(1)
    await f.ota.releaseRecoverySession(next)
    expect(f.tx.stopNotifications).toHaveBeenCalledTimes(2)
  })

  it('rejects target/layout mismatch at acquire before PROGRAM', async () => {
    vi.useFakeTimers()
    const f = goodixFixture()
    f.target[46] ^= 1
    const result = expect(f.ota.acquireRecoverySession(context(f), f.pkg)).rejects.toThrow('board-qualified')
    await vi.runAllTimersAsync(); await result
    expect(f.commands).not.toContain(0x23)
    expect(f.commands).not.toContain(0x41)
  })

  it('uses a private payload snapshot if a progress callback mutates the caller package', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), handle = await acquire(f)
    const flash: number[] = []
    const write = f.rx.writeValueWithoutResponse.getMockImplementation()!
    // The fixture firmware is 1300 zero bytes: two 1024-byte FLASH blocks.
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => { flash.push(...bytes); return write(bytes) })
    const result = expect(f.ota.transferRecoverySession(handle, f.pkg, () => { f.pkg.firmware.fill(0xff) }))
      .resolves.toMatchObject({ endAcknowledged: true })
    await vi.runAllTimersAsync(); await result
    expect(flash).not.toContain(0xff)
    expect(f.commands.filter(c => c === 0x24)).toHaveLength(2)
  })

  it('cancel/release during an unknown write neither closes CCCD nor permits another acquire', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), handle = await acquire(f), pending = deferred()
    const write = f.rx.writeValueWithoutResponse.getMockImplementation()!
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
      if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === 0x23) return pending.promise
      return write(bytes)
    })
    const transfer = expect(f.ota.transferRecoverySession(handle, f.pkg)).rejects.toThrow()
    await vi.advanceTimersByTimeAsync(1)
    await f.ota.releaseRecoverySession(handle)
    expect(f.tx.stopNotifications).not.toHaveBeenCalled()
    await expect(f.ota.acquireRecoverySession(context(f), f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_WRITE_PENDING' })
    await vi.advanceTimersByTimeAsync(8001); await transfer
    expect(f.commands).not.toContain(0x24)
    pending.resolve(undefined)
    await vi.advanceTimersByTimeAsync(1)
    const next = await acquire(f)
    await f.ota.releaseRecoverySession(next)
  })

  it('rejects a foreign GATT/grant pair before any native access', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), ctx = { ...context(f), device: { id: 'foreign-grant', gatt: f.server } }
    await expect(f.ota.acquireRecoverySession(ctx, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
    expect(f.server.getPrimaryService).not.toHaveBeenCalled()
    expect(f.ctrl.writeValueWithoutResponse).not.toHaveBeenCalled()
  })

  it('a stale generation cannot close a connection now owned by the shared caller', async () => {
    vi.useFakeTimers()
    const f = goodixFixture()
    let current = true
    const handle = await acquire(f, context(f, () => current))
    current = false
    await f.ota.releaseRecoverySession(handle)
    expect(f.tx.stopNotifications).not.toHaveBeenCalled()
    expect(f.server.disconnect).not.toHaveBeenCalled()
  })

  it('a late native completion cannot disconnect a newer shared-caller generation', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), pending = deferred()
    let current = true
    f.server.getPrimaryService.mockReturnValueOnce(pending.promise)
    const result = expect(f.ota.acquireRecoverySession(context(f, () => current), f.pkg))
      .rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(45001); await result
    current = false
    f.server.connected = true
    const disconnects = f.server.disconnect.mock.calls.length
    pending.resolve(f.service)
    await vi.advanceTimersByTimeAsync(1)
    expect(f.server.disconnect).toHaveBeenCalledTimes(disconnects)
    expect(f.ctrl.writeValueWithoutResponse).not.toHaveBeenCalled()
  })

  it('rejects a stale generation during a progress callback without sending the next block', async () => {
    vi.useFakeTimers()
    const f = goodixFixture()
    let current = true
    const handle = await acquire(f, context(f, () => current))
    const result = expect(f.ota.transferRecoverySession(handle, f.pkg, () => { current = false }))
      .rejects.toMatchObject({ code: 'GOODIX_SESSION_ABANDONED' })
    await vi.runAllTimersAsync(); await result
    expect(f.commands.filter(c => c === 0x24)).toHaveLength(1)
    expect(f.commands).not.toContain(0x25)
  })

  it.each(['connect', 'service', 'channel'] as const)('quarantines a pending native %s across fresh wrappers of the same grant', async stage => {
    vi.useFakeTimers()
    const f = goodixFixture(), pending = deferred()
    if (stage === 'connect') { f.server.connected = false; f.server.connect.mockReturnValueOnce(pending.promise) }
    if (stage === 'service') f.server.getPrimaryService.mockReturnValueOnce(pending.promise)
    if (stage === 'channel') f.service.getCharacteristic.mockReturnValueOnce(pending.promise)
    const first = expect(f.ota.acquireRecoverySession(context(f), f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(45001); await first
    const wrapper = { ...f.server, connected: false, connect: vi.fn() }
    const ctx = { ...context(f), device: { id: f.device.id, gatt: wrapper } }
    await expect(f.ota.acquireRecoverySession(ctx, f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_PENDING' })
    expect(wrapper.connect).not.toHaveBeenCalled()
    pending.resolve(stage === 'connect' ? f.server : stage === 'service' ? f.service : f.tx)
    await vi.advanceTimersByTimeAsync(1)
    const handle = await acquire(f)
    await f.ota.releaseRecoverySession(handle)
  })

  it('keeps an unknown vendor mode commit terminal and quarantined without starting PROGRAM', async () => {
    vi.useFakeTimers()
    const f = goodixFixture(), pending = deferred()
    f.target[5] = 1
    const write = f.rx.writeValueWithoutResponse.getMockImplementation()!
    let modes = 0
    f.rx.writeValueWithoutResponse.mockImplementation(async bytes => {
      if (bytes[0] === 0x44 && bytes[1] === 0x47 && bytes[2] === 0x41) { modes++; return pending.promise }
      return write(bytes)
    })
    const entry = expect(f.ota.enterRecovery(f.server)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_STAGE_TIMEOUT' })
    await vi.advanceTimersByTimeAsync(9001); await entry
    await expect(f.ota.acquireRecoverySession(context(f), f.pkg)).rejects.toMatchObject({ code: 'GOODIX_NATIVE_WRITE_PENDING' })
    expect(modes).toBe(1)
    expect(f.commands).not.toContain(0x23)
    pending.resolve(undefined)
    await vi.advanceTimersByTimeAsync(1)
    expect(modes).toBe(1)
  })

  it.each(['expected-disconnect', 'connected-network-error', 'permission-error'] as const)('handles settled mode cleanup %s without treating native mode completion as Recovery proof', async kind => {
    vi.useFakeTimers()
    const f = goodixFixture()
    f.target[5] = 1
    f.tx.stopNotifications.mockImplementation(async () => {
      if (kind !== 'connected-network-error') f.server.connected = false
      throw new DOMException('mode cleanup', kind === 'permission-error' ? 'NotAllowedError' : 'NetworkError')
    })
    const entry = f.ota.enterRecovery(f.server)
    const result = kind === 'expected-disconnect' ? expect(entry).resolves.toBeUndefined() : expect(entry).rejects.toThrow('mode cleanup')
    await vi.runAllTimersAsync(); await result
    expect(f.commands.filter(c => c === 0x41)).toHaveLength(1)
    expect(f.commands).not.toContain(0x23)
    // Native completion + disconnect cannot substitute for a live role check.
    f.tx.stopNotifications.mockResolvedValue(undefined)
    const probe = expect(f.ota.acquireRecoverySession(context(f), f.pkg)).rejects.toMatchObject({ code: 'GOODIX_SESSION_INVALID' })
    await vi.runAllTimersAsync(); await probe
    expect(f.commands.filter(c => c === 0x41)).toHaveLength(1)
    expect(f.commands).not.toContain(0x23)
  })
})
