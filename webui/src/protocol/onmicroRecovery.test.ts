import { afterEach, describe, expect, it, vi } from 'vitest'
import source from '../ota-drivers/onmicro_ota.js?raw'
import rejection from '../ota-drivers/ota_rejection.js?raw'

function adapter() {
  const scope: any = {}
  new Function('window', rejection)(scope)
  new Function('window', source)(scope)
  return scope.OnMicroOta
}
function profile() {
  const raw = new Uint8Array(20)
  const view = new DataView(raw.buffer)
  view.setUint32(0, 0x50524b49, true)
  raw[4] = 1
  view.setUint16(6, 20, true)
  view.setUint32(8, 0x31565a4f, true)
  view.setUint32(12, 0x4e000 - 96, true)
  view.setUint32(16, 0x4e000, true)
  return view
}
function device(reply = new Uint8Array([0x60, 7, 1])) {
  let listener: (event: any) => void
  const control = {
    startNotifications: vi.fn().mockResolvedValue(undefined),
    stopNotifications: vi.fn().mockResolvedValue(undefined),
    addEventListener: vi.fn((_name, fn) => { listener = fn }),
    removeEventListener: vi.fn(),
    writeValueWithResponse: vi.fn(async () => {
      listener({ target: { value: new DataView(reply.buffer) } })
    }),
  }
  const read = vi.fn().mockResolvedValue(profile())
  const getCharacteristic = vi.fn(async (uuid: string) => uuid.includes('5f03') ? { readValue: read } : control)
  const server = { getPrimaryService: vi.fn().mockResolvedValue({ getCharacteristic }) }
  return { server, read, control }
}
afterEach(() => vi.useRealTimers())
describe('OM existing guarded Recovery command', () => {
  it('releases locally after an acknowledged reboot without issuing a remote CCC stop', async () => {
    vi.useFakeTimers()
    const run = device()
    run.control.stopNotifications.mockReturnValueOnce(new Promise(() => {}) as any)
    const ota = adapter()
    const result = expect(ota.resumeApplication(run.server)).resolves.toBeUndefined()
    await vi.runAllTimersAsync(); await result
    expect(run.control.writeValueWithResponse).toHaveBeenCalledOnce()
    expect(run.control.removeEventListener).toHaveBeenCalledOnce()
    expect(run.control.stopNotifications).not.toHaveBeenCalled()
    await expect(ota.queryRecovery(run.server)).resolves.toBeDefined()
  })
  it('verifies the live fixed profile without writes or invented voltage', async () => {
    const { server, control } = device()
    await expect(adapter().queryRecovery(server)).resolves.toBeDefined()
    expect(control.startNotifications).not.toHaveBeenCalled()
    expect(control.writeValueWithResponse).not.toHaveBeenCalled()
  })
  it.each([0,1,2])('reads the exact extended policy %i and resumes once',async policy=>{
    const {server,read,control}=device()
    const extended=new Uint8Array(26)
    extended.set(new Uint8Array(profile().buffer));extended.set([73,75,83,80,1,policy],20)
    read.mockResolvedValue(new DataView(extended.buffer))
    await expect(adapter().queryRecovery(server)).resolves.toBeDefined()
    expect(control.writeValueWithResponse).not.toHaveBeenCalled()
    await adapter().resumeApplication(server)
    expect(control.writeValueWithResponse).toHaveBeenCalledExactlyOnceWith(new Uint8Array([7]))
  })
  it.each([25,27,52,6])('rejects malformed extended length %i before resume',async length=>{
    const {server,read,control}=device()
    const extended=new Uint8Array(length)
    extended.set(new Uint8Array(profile().buffer).slice(0,Math.min(length,20)))
    if(length>=26)extended.set([73,75,83,80,1,2],20)
    read.mockResolvedValue(new DataView(extended.buffer))
    await expect(adapter().resumeApplication(server)).rejects.toThrow(/Recovery/)
    expect(control.writeValueWithResponse).not.toHaveBeenCalled()
  })
  it('sends existing SAFE_RESUME_APP once and requires its successful ACK', async () => {
    const { server, control } = device()
    await adapter().resumeApplication(server)
    expect(control.writeValueWithResponse).toHaveBeenCalledExactlyOnceWith(new Uint8Array([7]))
    expect(control.stopNotifications).not.toHaveBeenCalled()
  })
  it('refuses an unknown profile before sending a command', async () => {
    const { server, read, control } = device()
    read.mockResolvedValue(new DataView(new ArrayBuffer(20)))
    await expect(adapter().resumeApplication(server)).rejects.toThrow(/Recovery/)
    expect(control.writeValueWithResponse).not.toHaveBeenCalled()
  })
  it('preserves a decoded power rejection without retries or reset', async () => {
    // Native DFU_OPERATION_FAILED + frozen version-1 IK diagnostic tail.
    const { server, control } = device(new Uint8Array([0x60, 7, 10, 0x49, 0x4b, 1, 2, 1, 1, 0, 0, 0, 0, 0x28, 0x0a, 0x28, 0x0a]))
    await expect(adapter().resumeApplication(server)).rejects.toThrow(/VOLTAGE_BELOW_MIN/)
    expect(control.writeValueWithResponse).toHaveBeenCalledOnce()
  })
  it('a write failure is unconfirmed and never replays', async () => {
    const { server, control } = device()
    control.writeValueWithResponse.mockRejectedValue(new DOMException('lost ACK', 'NetworkError'))
    await expect(adapter().resumeApplication(server)).rejects.toMatchObject({ code: 'RECOVERY_RESULT_UNKNOWN' })
    expect(control.writeValueWithResponse).toHaveBeenCalledOnce()
  })
  it('does not treat an ACK as completion while the native control write remains pending', async () => {
    vi.useFakeTimers()
    const run = device()
    const originalWrite = run.control.writeValueWithResponse.getMockImplementation()!
    let settle!: () => void
    run.control.writeValueWithResponse.mockImplementationOnce(async () => {
      await originalWrite()
      await new Promise<void>(resolve => { settle = resolve })
    })
    const ota = adapter()
    const result = expect(ota.resumeApplication(run.server)).rejects.toMatchObject({ code: 'RECOVERY_RESULT_UNKNOWN' })
    await vi.runAllTimersAsync(); await result
    expect(run.control.writeValueWithResponse).toHaveBeenCalledOnce()
    expect(run.control.stopNotifications).not.toHaveBeenCalled()
    await expect(ota.queryRecovery(run.server)).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
    settle(); await vi.runAllTimersAsync()
    await expect(ota.queryRecovery(run.server)).resolves.toBeDefined()
  })
  it.each([new Uint8Array([0x60, 7]), new Uint8Array([0x60, 4, 1])])(
    'retains cleanup and an unknown outcome for malformed or mismatched resume ACK %s', async reply => {
      const { server, control } = device(reply)
      await expect(adapter().resumeApplication(server)).rejects.toMatchObject({ code: 'RECOVERY_RESULT_UNKNOWN' })
      expect(control.writeValueWithResponse).toHaveBeenCalledOnce()
      expect(control.stopNotifications).toHaveBeenCalledOnce()
    })
})

describe('OM shipping transfer retry boundary', () => {
  const pkg = () => ({
    firmware: new Uint8Array(128),
    application: { active_address: 0x2a000 },
    manifest: { board: 'om6626_zkcxxb', chip: 'OM6626', layout: 'layout-v1', compatibilityEpoch: 1, releaseVersion: 1,
      application: { image_type: 0x10, active_address: 0x2a000, slot_size: 0x4e000, size: 32, transfer_size: 128,
        recovery_state_addresses: [0x82000, 0x83000], recovery_request_storage: 'transaction-journal' },
      signature: { signed_board: 'om6626_zkcxxb', signed_layout: 'layout-v1', board_signature: 0x42584d4f,
        layout_signature: 0x31565a4f, verifier_signature: 0x314b4d4f } },
  })
  function transferFixture(fail: 'none' | 'select' | 'create' | 'data' | 'execute' | 'final-execute', kind: 'refusal' | 'timeout' | 'write-error') {
    const ota = adapter()
    let listener: ((event: any) => void) | undefined
    let data = new Uint8Array()
    let objectType = 0
    const emit = (bytes: Uint8Array) => listener?.({ target: { value: new DataView(bytes.buffer) } })
    const writeControl = vi.fn(async (bytes: Uint8Array) => {
      const opcode = bytes[0]
      if (opcode === 1) objectType = bytes[1]
      const fails = opcode === ({ none: -2, select: 6, create: 1, data: -1, execute: 4, 'final-execute': 4 })[fail]
        && (fail !== 'final-execute' || objectType === 2)
      if (fails && kind === 'write-error') throw new DOMException('unknown write result', 'NetworkError')
      if (fails && kind === 'timeout') return
      if (opcode === 1) data = new Uint8Array()
      const response = new Uint8Array(opcode === 3 ? 11 : 3)
      response.set([0x60, opcode, fails ? 0x0a : 1])
      if (opcode === 3) { const view = new DataView(response.buffer); view.setUint32(3, data.length, true); view.setUint32(7, ota.crc32(data), true) }
      emit(response)
    })
    const control = { startNotifications: vi.fn().mockResolvedValue(undefined), stopNotifications: vi.fn().mockResolvedValue(undefined),
      addEventListener: vi.fn((_name, fn) => { listener = fn }), removeEventListener: vi.fn(() => { listener = undefined }),
      writeValueWithResponse: writeControl }
    const writeData = vi.fn(async (bytes: Uint8Array) => {
      if (fail === 'data') throw new DOMException('unknown data result', 'NetworkError')
      data = new Uint8Array([...data, ...bytes])
    })
    const service = { getCharacteristic: vi.fn(async (uuid: string) => uuid.includes('5f03') ? { readValue: async () => profile() }
      : uuid.includes('5f02') ? { writeValueWithoutResponse: writeData } : control) }
    const gatt = { connected: false, connect: vi.fn(async () => { gatt.connected = true; return gatt }),
      disconnect: vi.fn(() => { gatt.connected = false }), getPrimaryService: vi.fn().mockResolvedValue(service) }
    return { ota, device: { id: 'OM-native-test', gatt }, service, control, writeControl, writeData }
  }
  it.each([
    ['select', 'refusal'], ['create', 'refusal'], ['execute', 'refusal'], ['data', 'write-error'],
    ['create', 'timeout'], ['execute', 'write-error'],
  ] as const)('does not restart any transfer after %s / %s', async (stage, kind) => {
    vi.useFakeTimers()
    const run = transferFixture(stage, kind)
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toThrow()
    await vi.runAllTimersAsync(); await result
    expect(run.device.gatt.connect).toHaveBeenCalledTimes(1)
    expect(run.writeControl.mock.calls.filter(([bytes]) => bytes[0] === 6)).toHaveLength(1)
    expect(run.writeData.mock.calls.length).toBeLessThanOrEqual(1)
    expect(run.control.removeEventListener).toHaveBeenCalledOnce()
  })
  it('keeps native cleanup after COMMAND EXECUTE when the final DATA EXECUTE is refused', async () => {
    const run = transferFixture('final-execute', 'refusal')
    await expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toMatchObject({ code: 'RECOVERY_REFUSED' })
    expect(run.writeControl.mock.calls.filter(([bytes]) => bytes[0] === 4)).toHaveLength(2)
    expect(run.control.stopNotifications).toHaveBeenCalledOnce()
  })
  it('retries connection failures only, before any DFU request', async () => {
    vi.useFakeTimers()
    const run = transferFixture('select', 'refusal')
    run.device.gatt.connect.mockRejectedValueOnce(new DOMException('not ready', 'NetworkError'))
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toThrow()
    await vi.runAllTimersAsync(); await result
    expect(run.device.gatt.connect).toHaveBeenCalledTimes(2)
    expect(run.writeControl).toHaveBeenCalledOnce()
    expect(run.writeData).not.toHaveBeenCalled()
  })
  it('retries a stale App service cache before any DFU request', async () => {
    vi.useFakeTimers()
    const run = transferFixture('select', 'refusal')
    run.device.gatt.getPrimaryService.mockRejectedValueOnce(new DOMException('old App services', 'NotFoundError'))
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toThrow()
    await vi.runAllTimersAsync(); await result
    expect(run.device.gatt.connect).toHaveBeenCalledTimes(2)
    expect(run.writeControl).toHaveBeenCalledOnce()
    expect(run.writeData).not.toHaveBeenCalled()
  })
  it('does not retry a positively read incompatible profile or issue DFU commands', async () => {
    vi.useFakeTimers()
    const run = transferFixture('select', 'refusal')
    run.device.gatt.getPrimaryService.mockResolvedValueOnce({ getCharacteristic: async () => ({ readValue: async () => new DataView(new ArrayBuffer(20)) }) })
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toThrow('preflight')
    await vi.runAllTimersAsync(); await result
    expect(run.device.gatt.connect).toHaveBeenCalledTimes(1)
    expect(run.writeControl).not.toHaveBeenCalled()
  })
  it.each(['connect', 'service', 'characteristic', 'profile', 'notify', 'control', 'data'] as const)(
    'bounds a pending native %s call and never starts a second write scope or late continuation', async stage => {
      vi.useFakeTimers()
      const run = transferFixture('none', 'refusal')
      let settle!: (value?: any) => void
      const pending = new Promise<any>(resolve => { settle = resolve })
      const read = vi.fn().mockResolvedValue(profile())
      const getCharacteristic = vi.fn(async (uuid: string) => uuid.includes('5f03') ? { readValue: read }
        : uuid.includes('5f02') ? { writeValueWithoutResponse: run.writeData } : run.control)
      run.service.getCharacteristic.mockImplementation(getCharacteristic)
      if (stage === 'connect') run.device.gatt.connect.mockReturnValueOnce(pending)
      if (stage === 'service') run.device.gatt.getPrimaryService.mockReturnValueOnce(pending)
      if (stage === 'characteristic') getCharacteristic.mockReturnValueOnce(pending)
      if (stage === 'profile') read.mockReturnValueOnce(pending)
      if (stage === 'notify') run.control.startNotifications.mockReturnValueOnce(pending)
      if (stage === 'control') run.writeControl.mockReturnValueOnce(pending)
      if (stage === 'data') run.writeData.mockReturnValueOnce(pending)
      const phases = vi.fn()
      const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn(), { onPhase: phases }))
        .rejects.toMatchObject({ code: 'OM_NATIVE_TIMEOUT' })
      await vi.runAllTimersAsync(); await result
      expect(phases.mock.calls.some(([phase]) => phase.includes('超时'))).toBe(true)
      const writes = run.writeControl.mock.calls.length + run.writeData.mock.calls.length
      const secondGrantObject = { id: run.device.id, gatt: { ...run.device.gatt } }
      await expect(run.ota.transferDevice(secondGrantObject, pkg(), vi.fn())).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
      const freshServer = { ...run.device.gatt, device: { id: run.device.id } }
      await expect(run.ota.queryRecovery(freshServer)).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
      await expect(run.ota.resumeApplication(freshServer)).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
      await expect(run.ota.transferServer(freshServer, pkg(), vi.fn())).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
      // Read-only entry via the original GATT object shares the same lease.
      await expect(run.ota.queryRecovery(run.device.gatt)).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
      run.device.gatt.connected = true
      settle(stage === 'connect' ? run.device.gatt : stage === 'service' ? run.service : stage === 'profile' ? profile() : undefined)
      await vi.runAllTimersAsync()
      expect(run.writeControl.mock.calls.length + run.writeData.mock.calls.length).toBe(writes)
      expect(run.device.gatt.disconnect).not.toHaveBeenCalled()
      expect(run.control.stopNotifications).not.toHaveBeenCalled()
      // After the OS promise settles, a fresh read-only scope is allowed.
      await expect(run.ota.queryRecovery(run.device.gatt)).resolves.toBeDefined()
      await expect(run.ota.queryRecovery(freshServer)).resolves.toBeDefined()
    })

  it('bounds nonterminal native stop and retains the grant until it settles', async () => {
    vi.useFakeTimers()
    const run = transferFixture('select', 'refusal')
    let settle!: () => void
    run.control.stopNotifications.mockReturnValueOnce(new Promise<void>(resolve => { settle = resolve }) as any)
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toMatchObject({ code: 'RECOVERY_REFUSED' })
    await vi.runAllTimersAsync(); await result
    const writes = run.writeControl.mock.calls.length
    await expect(run.ota.resumeApplication(run.device.gatt)).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
    settle(); await vi.runAllTimersAsync()
    expect(run.writeControl).toHaveBeenCalledTimes(writes)
    expect(run.device.gatt.disconnect).not.toHaveBeenCalled()
  })

  it('does not let a cleanup timeout replace a decoded device refusal', async () => {
    vi.useFakeTimers()
    const run = transferFixture('select', 'refusal')
    run.control.stopNotifications.mockReturnValueOnce(new Promise(() => {}) as any)
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toMatchObject({ code: 'RECOVERY_REFUSED' })
    await vi.runAllTimersAsync(); await result
    expect(run.writeControl).toHaveBeenCalledOnce()
    expect(run.writeData).not.toHaveBeenCalled()
    await expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toMatchObject({ code: 'OM_NATIVE_PENDING' })
  })

  it('leaves final App attestation to the caller without a CCC write after acknowledged DATA EXECUTE', async () => {
    vi.useFakeTimers()
    const run = transferFixture('none', 'refusal')
    run.control.stopNotifications.mockReturnValueOnce(new Promise(() => {}) as any)
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).resolves.toEqual({ activeAddress: 0x2a000 })
    await vi.runAllTimersAsync(); await result
    expect(run.control.stopNotifications).not.toHaveBeenCalled()
    expect(run.device.gatt.disconnect).not.toHaveBeenCalled()
  })

  it('allows native service discovery slower than five seconds within the original 45-second budget', async () => {
    vi.useFakeTimers()
    const run = transferFixture('none', 'refusal')
    run.device.gatt.getPrimaryService.mockReturnValueOnce(new Promise(resolve => setTimeout(() => resolve(run.service), 6000)))
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).resolves.toEqual({ activeAddress: 0x2a000 })
    await vi.runAllTimersAsync(); await result
    expect(run.writeData).toHaveBeenCalled()
  })

  it('does not reset the 45-second discovery budget between native service calls', async () => {
    vi.useFakeTimers()
    const run = transferFixture('none', 'refusal')
    run.device.gatt.getPrimaryService.mockImplementation(() => new Promise(resolve => setTimeout(() => resolve(run.service), 16000)))
    const result = expect(run.ota.transferDevice(run.device, pkg(), vi.fn())).rejects.toMatchObject({ code: 'OM_NATIVE_TIMEOUT' })
    await vi.runAllTimersAsync(); await result
    expect(run.writeControl).not.toHaveBeenCalled()
    expect(run.writeData).not.toHaveBeenCalled()
    expect(run.device.gatt.disconnect).not.toHaveBeenCalled()
  })

})
