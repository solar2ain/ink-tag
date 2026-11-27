import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { transferOta, type ParsedOtaPackage } from './ota'
import { useDeviceStore } from '../store/device'

const client = vi.hoisted(() => ({
  getDevice: vi.fn(), getGattServer: vi.fn(), getOtaRecoveryType: vi.fn(),
  getExpectedRecoveryIdentity: vi.fn(), getConnectionGeneration: vi.fn(), getVerifiedRecoveryName: vi.fn(),
  releaseForRecovery: vi.fn(), takeGoodixRecoveryForOta: vi.fn(),
  enterOtaRecovery: vi.fn(), queryRuntimeBuild: vi.fn(),
  disconnectAndWait: vi.fn(), reconnectDevice: vi.fn(),
  waitForPasswordAuthorization: vi.fn(), syncTime: vi.fn(), getRuntimeImageInfo: vi.fn(),
}))
vi.mock('./client', () => ({ inktagClient: client }))

const pairs = [
  ['goodix', 'gr5331_zkel', 'gr5331_zkc21v'],
  ['goodix', 'gr551x_zkel', 'gr551x_zkc42v'],
  ['goodix', 'gr5513_zkel_internal', 'gr5513_zkel_internal'],
  ['onmicro', 'om6626_zkcxxb', 'om6626_zkcxxb'],
  ['onmicro', 'om6626_hsela', 'om6626_hsela'],
] as const
let transport: ReturnType<typeof vi.fn>
beforeEach(() => {
  vi.useFakeTimers(); vi.resetAllMocks(); useDeviceStore.getState().resetDevice()
  client.getDevice.mockReturnValue({id: 'exact-grant'})
  client.getGattServer.mockReturnValue({connected: true})
  client.getOtaRecoveryType.mockReturnValue(null)
  client.getExpectedRecoveryIdentity.mockReturnValue({ deviceId: 'exact-grant', recoveryName: 'IKT_GR_FC34_OTA' })
  client.getConnectionGeneration.mockReturnValue(1)
  client.takeGoodixRecoveryForOta.mockReturnValue({ deviceId: 'exact-grant', recoveryName: 'IKT_GR_FC34_OTA' })
  transport = vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('window', {GoodixOta: { transferDevice: vi.fn().mockRejectedValue(new Error('legacy transfer forbidden')),
    enterRecovery: vi.fn().mockResolvedValue(undefined),
    acquireRecoverySession: vi.fn().mockResolvedValue(Object.freeze({})),
    transferRecoverySession: transport, releaseRecoverySession: vi.fn().mockResolvedValue(undefined) }, OnMicroOta: {transferServer: transport, applicationBodySize: () => 100}})
  vi.stubGlobal('document', {createElement: () => ({remove() {}}), head: {appendChild() {}}})
  const runtime = {build: 17, appBytes: 100, swapResult: 0, swapState: 4, swapGeneration: 2, startupConfirm: 0}
  client.queryRuntimeBuild.mockResolvedValue(runtime)
  client.getRuntimeImageInfo.mockReturnValue({version: 17, appBytes: 100, checksum: 123})
})
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals(); useDeviceStore.getState().resetDevice() })

function pkg(type: 'goodix' | 'onmicro', target: string): ParsedOtaPackage {
  return {type, summary: 'simulated signed package', payload: {
    manifest: {board: target, releaseVersion: 17, compatibilityEpoch: 17},
    application: {size: 100, checksum: 123},
  }}
}

function prepareOmRecovery() {
  const handoff = { deviceId: 'exact-grant', recoveryName: 'IKT_OM_FC34_OTA' }
  const recovery = { connected: true }
  client.getExpectedRecoveryIdentity.mockReturnValue(handoff)
  client.getVerifiedRecoveryName.mockReturnValue(handoff.recoveryName)
  client.reconnectDevice.mockImplementationOnce(async (_device, _timeout, requireRecovery) => {
    expect(requireRecovery).toBe(true)
    client.getOtaRecoveryType.mockReturnValue('onmicro')
    client.getGattServer.mockReturnValue(recovery)
  })
  return { handoff, recovery }
}

it.each(pairs)('rejects a cross-family %s package on %s before Recovery entry or any transport', async (type, identity, target) => {
  const other = pairs.find(([, , t]) => t !== target)![2]
  useDeviceStore.setState({connection: 'connected', boardId: identity})
  await expect(transferOta(pkg(type, other), vi.fn())).rejects.toThrow('固件系列不匹配')
  expect(client.queryRuntimeBuild).not.toHaveBeenCalled()
  expect(client.enterOtaRecovery).not.toHaveBeenCalled()
  expect(transport).not.toHaveBeenCalled()
  expect(client.syncTime).not.toHaveBeenCalled()
})

it.each(pairs)('rejects a wrong %s family after reconnect on %s before success or time sync', async (type, identity, target) => {
  useDeviceStore.setState({connection: 'connected', boardId: identity})
  client.queryRuntimeBuild.mockResolvedValueOnce({build: 16, appBytes: 99, swapState: 4, swapGeneration: 1})
    .mockResolvedValue({build: 17, appBytes: 100, swapState: 4, swapGeneration: 2})
  if (type === 'onmicro') prepareOmRecovery()
  client.reconnectDevice.mockImplementation(async () => {
    client.getOtaRecoveryType.mockReturnValue(null)
    useDeviceStore.setState({boardId: identity === 'om6626_hsela' ? 'om6626_zkcxxb' : 'om6626_hsela'})
  })
  const result = expect(transferOta(pkg(type, target), vi.fn())).rejects.toThrow('固件系列不匹配')
  await vi.runAllTimersAsync()
  await result
  expect(transport).toHaveBeenCalledOnce()
  expect(client.syncTime).not.toHaveBeenCalled()
})

it.each(pairs)('keeps the normal %s update path for %s with one clock sync after attestation', async (type, identity, target) => {
  useDeviceStore.setState({connection: 'connected', boardId: identity})
  const om = type === 'onmicro' ? prepareOmRecovery() : null
  if (om) {
    client.queryRuntimeBuild.mockResolvedValueOnce({build: 16, appBytes: 99, swapState: 4, swapGeneration: 1})
    client.reconnectDevice.mockImplementation(async () => client.getOtaRecoveryType.mockReturnValue(null))
  }
  const progress = vi.fn()
  const operation = transferOta(pkg(type, target), progress)
  await vi.runAllTimersAsync()
  await operation
  expect(transport).toHaveBeenCalledOnce()
  expect(client.syncTime).toHaveBeenCalledOnce()
  if (om) {
    expect(client.releaseForRecovery).toHaveBeenCalledExactlyOnceWith(om.handoff)
    expect(transport).toHaveBeenCalledExactlyOnceWith(om.recovery, expect.anything(), expect.any(Function), expect.anything())
    expect(client.enterOtaRecovery.mock.invocationCallOrder[0]).toBeLessThan(client.releaseForRecovery.mock.invocationCallOrder[0])
    expect(client.releaseForRecovery.mock.invocationCallOrder[0]).toBeLessThan(client.reconnectDevice.mock.invocationCallOrder[0])
    expect(client.reconnectDevice.mock.invocationCallOrder[0]).toBeLessThan(transport.mock.invocationCallOrder[0])
  }
  expect(progress.mock.lastCall?.[0].message).toContain('应用已验身、重连并校时')
})

it('does not require the retired raw build query after a current GR App returns', async () => {
  useDeviceStore.setState({connection:'connected',boardId:'gr551x_zkel'})
  client.queryRuntimeBuild.mockRejectedValue(new Error('设备未回传固件与 A/B 交换状态'))
  const operation=expect(transferOta(pkg('goodix','gr551x_zkc42v'),vi.fn())).resolves.toBeUndefined()
  await vi.runAllTimersAsync()
  await operation
  expect(client.queryRuntimeBuild).not.toHaveBeenCalled()
  expect(client.syncTime).toHaveBeenCalledOnce()
})

it('preserves an OM baseline timeout without sending a package or prescribing a destructive flash', async () => {
  useDeviceStore.setState({connection: 'connected', boardId: 'om6626_zkcxxb'})
  client.queryRuntimeBuild.mockRejectedValue(new Error('等待命令 0x03 响应超时'))
  await expect(transferOta(pkg('onmicro', 'om6626_zkcxxb'), vi.fn()))
    .rejects.toThrow('无法读取升级前的 Recovery 状态：等待命令 0x03 响应超时')
  expect(client.enterOtaRecovery).not.toHaveBeenCalled()
  expect(transport).not.toHaveBeenCalled()
  expect(client.syncTime).not.toHaveBeenCalled()
})

it('does not declare OM success when the Recovery generation has not advanced', async () => {
  useDeviceStore.setState({connection: 'connected', boardId: 'om6626_zkcxxb'})
  prepareOmRecovery()
  client.reconnectDevice.mockImplementation(async () => client.getOtaRecoveryType.mockReturnValue(null))
  const result = expect(transferOta(pkg('onmicro', 'om6626_zkcxxb'), vi.fn()))
    .rejects.toThrow('没有推进 Recovery 代次')
  await vi.runAllTimersAsync()
  await result
  expect(transport).toHaveBeenCalledOnce()
  expect(client.syncTime).not.toHaveBeenCalled()
})

it('offers manual reselection without writing when OM never presents the expected Recovery', async () => {
  useDeviceStore.setState({connection: 'connected', boardId: 'om6626_zkcxxb'})
  client.releaseForRecovery.mockImplementation(async () => client.getDevice.mockReturnValue(null))
  client.reconnectDevice.mockRejectedValue(new DOMException('service not found', 'NotFoundError'))
  const result = expect(transferOta(pkg('onmicro', 'om6626_zkcxxb'), vi.fn()))
    .rejects.toMatchObject({code: 'RECOVERY_SELECT_REQUIRED'})
  await vi.runAllTimersAsync()
  await result
  expect(client.enterOtaRecovery).toHaveBeenCalledOnce()
  expect(transport).not.toHaveBeenCalled()
})

it('does not declare GR success when the reconnected App is still the old image', async () => {
  useDeviceStore.setState({connection: 'connected', boardId: 'gr5331_zkel'})
  client.getRuntimeImageInfo.mockReturnValue({version: 17, appBytes: 99, checksum: 122})
  const result = expect(transferOta(pkg('goodix', 'gr5331_zkc21v'), vi.fn()))
    .rejects.toThrow('GR 重连后的应用与升级包不一致')
  await vi.runAllTimersAsync()
  await result
  expect(transport).toHaveBeenCalledOnce()
  expect(client.queryRuntimeBuild).not.toHaveBeenCalled()
  expect(client.syncTime).not.toHaveBeenCalled()
})
