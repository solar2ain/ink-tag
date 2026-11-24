import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { inktagClient } from './client'
import { exitGoodixRecovery, exitTelinkRecovery, exitOnMicroRecovery } from './ota'
import { useDeviceStore } from '../store/device'

describe.each(['goodix', 'telink', 'onmicro'] as const)('%s guarded Recovery exit', (platform) => {
  const exitRecovery = platform === 'telink' ? exitTelinkRecovery : platform === 'onmicro' ? exitOnMicroRecovery : exitGoodixRecovery
  const device = { id: 'verified-gr' } as BluetoothDevice
  let resume: ReturnType<typeof vi.fn>
  let reconnect: ReturnType<typeof vi.spyOn>
  let disconnect: ReturnType<typeof vi.spyOn>
  beforeEach(() => {
    vi.useFakeTimers()
    useDeviceStore.getState().resetDevice()
    resume = vi.fn().mockResolvedValue(undefined)
    vi.stubGlobal('window', { GoodixOta: { resumeApplication: resume }, TelinkOta: { resumeApplication: resume }, OnMicroOta: { resumeApplication: resume } })
    vi.stubGlobal('document', { createElement: () => ({ remove() {} }), head: { appendChild() {} } })
    vi.spyOn(inktagClient, 'getDevice').mockReturnValue(device)
    vi.spyOn(inktagClient, 'getGattServer').mockReturnValue({ connected: true } as BluetoothRemoteGATTServer)
    vi.spyOn(inktagClient, 'getOtaRecoveryType').mockReturnValue(platform)
    disconnect = vi.spyOn(inktagClient, 'disconnectAndWait').mockResolvedValue(undefined)
    reconnect = vi.spyOn(inktagClient, 'reconnectDevice').mockResolvedValue(undefined)
    vi.spyOn(inktagClient, 'verifyReturnedApplication').mockImplementation(() => undefined)
    vi.spyOn(inktagClient, 'syncTimeAndVerify').mockResolvedValue(undefined)
  })
  afterEach(() => { vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })

  it('a device rejection is terminal and never disconnects, retries or forces reset', async () => {
    resume.mockRejectedValueOnce(new Error('SAFE_RESUME_APP denied'))
    await expect(exitRecovery()).rejects.toThrow('SAFE_RESUME_APP denied')
    expect(resume).toHaveBeenCalledTimes(1)
    expect(disconnect).not.toHaveBeenCalled()
    expect(reconnect).not.toHaveBeenCalled()
  })

  it('requires an application return rather than accepting a Recovery reconnect', async () => {
    const exit = exitRecovery()
    const result = expect(exit).rejects.toThrow('原应用尚未重连')
    await vi.runAllTimersAsync()
    await result
    expect(resume).toHaveBeenCalledTimes(1)
    expect(reconnect).toHaveBeenCalledTimes(20)
    expect(disconnect).toHaveBeenCalledTimes(21)
  })

  it('succeeds only after safe resume and an application reconnect', async () => {
    reconnect.mockImplementation(async () => {
      vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null)
    })
    const exit = exitRecovery()
    await vi.runAllTimersAsync()
    await expect(exit).resolves.toBeUndefined()
    expect(resume).toHaveBeenCalledTimes(1)
    expect(reconnect).toHaveBeenCalledTimes(1)
    expect(reconnect).toHaveBeenCalledWith(device, expect.any(Number))
    expect(inktagClient.syncTimeAndVerify).toHaveBeenCalledOnce()
  })

  it('preserves a returned App when clock verification fails and never repeats resume', async () => {
    reconnect.mockImplementation(async () => { vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null) })
    vi.mocked(inktagClient.syncTimeAndVerify).mockRejectedValue(new Error('clock readback failed'))
    const outcome = expect(exitRecovery()).rejects.toThrow('App 已返回，校时未完成')
    await vi.runAllTimersAsync(); await outcome
    expect(resume).toHaveBeenCalledOnce()
    expect(inktagClient.syncTimeAndVerify).toHaveBeenCalledOnce()
  })

  it('an explicit time opt-out still requires application verification', async () => {
    reconnect.mockImplementation(async () => { vi.mocked(inktagClient.getOtaRecoveryType).mockReturnValue(null) })
    const pending = exitRecovery({ syncTime: false })
    await vi.runAllTimersAsync(); await pending
    expect(inktagClient.verifyReturnedApplication).toHaveBeenCalledOnce()
    expect(inktagClient.syncTimeAndVerify).not.toHaveBeenCalled()
  })

  it('refuses to send a Recovery command while an OTA is active', async () => {
    useDeviceStore.getState().setAction('ota')
    await expect(exitRecovery()).rejects.toThrow('等待当前操作结束')
    expect(resume).not.toHaveBeenCalled()
  })
  it('an unknown write stays blocked on the same server even after the view remounts', async () => {
    const error = Object.assign(new Error('结果未知'), { code: 'RECOVERY_RESULT_UNKNOWN' })
    resume.mockRejectedValueOnce(error)
    await expect(exitRecovery()).rejects.toBe(error)
    await expect(exitRecovery()).rejects.toMatchObject({ code: 'RECOVERY_RESULT_UNKNOWN' })
    expect(resume).toHaveBeenCalledOnce()
    expect(disconnect).not.toHaveBeenCalled()
  })
})
