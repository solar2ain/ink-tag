import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { inktagClient } from './client'
import { transferOta } from './ota'
import { useDeviceStore } from '../store/device'

const device = { id: 'same-grant', name: 'IKT_NRF_6775' } as BluetoothDevice
const payload = { firmware: new Uint8Array([1, 2, 3]), firmwareCrc: 0x55, initPacket: new Uint8Array([9]) }
let query: ReturnType<typeof vi.spyOn>, sync: ReturnType<typeof vi.spyOn>, reconnect: ReturnType<typeof vi.spyOn>
let transfer: ReturnType<typeof vi.fn>
beforeEach(() => {
  vi.useFakeTimers(); useDeviceStore.getState().resetDevice()
  transfer = vi.fn().mockResolvedValue(undefined)
  vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
  vi.stubGlobal('window', { NordicDfu: { transferDevice: transfer } })
  vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
  vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({ connected: true } as BluetoothRemoteGATTServer)
  vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue('nordic')
  reconnect = vi.spyOn(inktagClient, 'reconnectDevice').mockImplementation(async () => {
    vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
  })
  query = vi.spyOn(inktagClient, 'queryV2ApplicationIdentity').mockResolvedValue({ size: 3, checksum: 0x55 })
  vi.spyOn(inktagClient, 'waitForPasswordAuthorization').mockResolvedValue(undefined)
  vi.spyOn(inktagClient, 'disconnectAndWait').mockResolvedValue(undefined)
  sync = vi.spyOn(inktagClient, 'syncTime').mockResolvedValue(undefined)
})
afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })
it.each([['nrf52832_priceetag',200],['aesl22_nrf',20],['',20]])('direct-Recovery packet limit uses the admitted family and entry: %s', async (boardId,packetSize) => {
  useDeviceStore.setState({boardId})
  const run=transferOta({type:'nordic',payload:{...payload,verified:{board:boardId}},summary:''},()=>{})
  await vi.runAllTimersAsync();await run
  expect(transfer).toHaveBeenCalledOnce()
  expect(transfer.mock.calls[0]?.[3]).toEqual({expectedBoard:boardId,maxPacketSize:packetSize,onPhase:expect.any(Function)})
})
it.each(['aesl22_nrf_extra','nrf52811','IKT_NRF_F988','aesl22_nrf'])('rejects unknown or cross-family target %s before any transfer', async boardId => {
  useDeviceStore.setState({boardId})
  const entry=vi.spyOn(inktagClient,'enterOtaRecovery')
  await expect(transferOta({type:'nordic',payload:{...payload,verified:{board:'nrf52832_priceetag'}},summary:''},()=>{})).rejects.toThrow('不匹配')
  expect(entry).not.toHaveBeenCalled();expect(transfer).not.toHaveBeenCalled()
  expect(query).not.toHaveBeenCalled();expect(sync).not.toHaveBeenCalled()
})
function unknownFinalAck() {
  return Object.assign(new Error('final EXECUTE response unknown; no replay'), { code: 'NORDIC_FINAL_EXECUTE_ACK_UNKNOWN' })
}
it('only a marked missing final ACK may reach same-device image attestation and one sync', async () => {
  transfer.mockRejectedValue(unknownFinalAck())
  const progress = vi.fn(), run = transferOta({ type: 'nordic', payload, summary: '' }, progress)
  await vi.runAllTimersAsync(); await run
  expect(transfer).toHaveBeenCalledOnce(); expect(reconnect).toHaveBeenCalledWith(device, 10000)
  expect(query).toHaveBeenCalledOnce(); expect(sync).toHaveBeenCalledOnce()
  expect(query.mock.invocationCallOrder[0]).toBeLessThan(sync.mock.invocationCallOrder[0])
  expect(progress.mock.calls.some(([event]) => event.message.includes('ACK 未知') && event.message.includes('尚不能判定成功'))).toBe(true)
})
it.each(['wrong-image', 'read-failed', 'no-app'])('never passes a missing final ACK with %s', async failure => {
  transfer.mockRejectedValue(unknownFinalAck())
  if (failure === 'wrong-image') query.mockResolvedValue({ size: 2, checksum: 0x55 })
  if (failure === 'read-failed') query.mockRejectedValue(new Error('identity read failed'))
  if (failure === 'no-app') reconnect.mockRejectedValue(new Error('not in range'))
  const progress = vi.fn(), run = transferOta({ type: 'nordic', payload, summary: '' }, progress)
  const rejected = expect(run).rejects.toThrow()
  await vi.runAllTimersAsync(); await rejected
  expect(transfer).toHaveBeenCalledOnce(); expect(sync).not.toHaveBeenCalled()
  expect(progress.mock.calls.some(([event]) => event.message.includes('OTA 完成'))).toBe(false)
})
it.each(['Secure DFU 0x4 失败: 0x8', 'GATT write TimeoutError', 'middle object timed out'])('preserves an unmarked refusal or failure: %s', async message => {
  transfer.mockRejectedValue(new Error(message))
  const run = transferOta({ type: 'nordic', payload, summary: '' }, () => {})
  const rejected = expect(run).rejects.toThrow(message)
  await vi.runAllTimersAsync(); await rejected
  expect(transfer).toHaveBeenCalledOnce(); expect(query).not.toHaveBeenCalled(); expect(sync).not.toHaveBeenCalled()
})
it('queries the actual running V2 image before reporting success and synchronizing once', async () => {
  const progress = vi.fn()
  const run = transferOta({ type: 'nordic', payload, summary: '' }, progress)
  await vi.runAllTimersAsync(); await run
  expect(query).toHaveBeenCalledOnce(); expect(sync).toHaveBeenCalledOnce()
  expect(query.mock.invocationCallOrder[0]).toBeLessThan(sync.mock.invocationCallOrder[0])
  expect(progress).toHaveBeenLastCalledWith({ percent: 100, message: 'OTA 完成，应用已验身、重连并校时' })
})

it('tracks Recovery transfer, App return and attestation across the intentional App disconnect', async () => {
  const phases: Array<string | null> = []
  const unsubscribe = useDeviceStore.subscribe((state, previous) => {
    if (state.otaPhase !== previous.otaPhase) phases.push(state.otaPhase)
  })
  transfer.mockImplementationOnce(async (_device, _payload, progress) => {
    useDeviceStore.getState().resetDevice(true)
    expect(useDeviceStore.getState().connection).not.toBe('connected')
    progress(1, 3, performance.now())
    expect(useDeviceStore.getState().otaPhase).toBe('recovery-transfer')
  })
  query.mockImplementationOnce(async () => {
    expect(useDeviceStore.getState().otaPhase).toBe('attesting-app')
    return { size: 3, checksum: 0x55 }
  })
  try {
    const run = transferOta({ type: 'nordic', payload, summary: '' }, () => {})
    await vi.runAllTimersAsync(); await run
    expect(phases).toEqual(['recovery-transfer', 'reconnecting-app', 'attesting-app', null])
    expect(transfer).toHaveBeenCalledOnce()
    expect(sync).toHaveBeenCalledOnce()
  } finally { unsubscribe() }
})
it.each([{ size: 2, checksum: 0x55 }, { size: 3, checksum: 0x54 }, { size: 0, checksum: 0x55 }])('rejects a mismatched running Nordic image %j before clock sync', async identity => {
  query.mockResolvedValue(identity)
  const progress = vi.fn(), run = transferOta({ type: 'nordic', payload, summary: '' }, progress)
  const rejected = expect(run).rejects.toThrow('nRF 应用核验不一致')
  await vi.runAllTimersAsync(); await rejected
  expect(query).toHaveBeenCalledOnce(); expect(sync).not.toHaveBeenCalled()
  expect(progress.mock.calls.some(([event]) => event.message.includes('验身完成') || event.message.includes('OTA 完成'))).toBe(false)
  expect(transfer).toHaveBeenCalledOnce()
})
it('does not attest or sync if the App never returns', async () => {
  reconnect.mockRejectedValue(new Error('not in range'))
  const run = transferOta({ type: 'nordic', payload, summary: '' }, () => {})
  const rejected = expect(run).rejects.toThrow('nRF 应用未重新上线')
  await vi.runAllTimersAsync(); await rejected
  expect(query).not.toHaveBeenCalled(); expect(sync).not.toHaveBeenCalled()
  expect(transfer).toHaveBeenCalledOnce()
})
it('preserves a V2 identity read failure without success, retrying the write, or sync', async () => {
  query.mockRejectedValue(new Error('identity read failed'))
  const run = transferOta({ type: 'nordic', payload, summary: '' }, () => {})
  const rejected = expect(run).rejects.toThrow('identity read failed')
  await vi.runAllTimersAsync(); await rejected
  expect(query).toHaveBeenCalledOnce(); expect(sync).not.toHaveBeenCalled()
  expect(transfer).toHaveBeenCalledOnce()
})
