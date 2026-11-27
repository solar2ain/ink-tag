import { beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('../data/db', () => ({
  db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } },
}))
vi.mock('./ota', () => ({ verifyGoodixRecovery: vi.fn().mockResolvedValue(undefined), verifyTelinkRecovery: vi.fn().mockResolvedValue({ status: 9 }), verifyOnMicroRecovery: vi.fn().mockResolvedValue(undefined), verifyDialogRecovery: vi.fn().mockResolvedValue({ safeResume: false }), verifyNordicRecovery: vi.fn().mockResolvedValue({ safeResume: false }) }))

import { displayWorkDetailFromLog, InkTagClient, isOptionalFindProbeUnavailable, isTransientGattBootstrapError, ProtocolV2Error } from './client'
import { BluetoothConnectionTimeoutError } from './connectionTimeout'
import { CapabilityV2, CommandV2, DisplayActionV2, EventV2, RequestFlagV2, SettingsSectionV2, StatusV2, SystemActionV2, TestOperationV2, TransferObjectV2 } from './generatedCommandsV2'
import { DISPLAY_SETTINGS_BOOT_REFRESH, DISPLAY_SETTINGS_BRAND_HIDDEN, DISPLAY_SETTINGS_PROTOCOL_VERSION } from './deviceSettings'
import { encodeScreenConfig } from './screen'
import { useDeviceStore } from '../store/device'
import { verifyGoodixRecovery, verifyTelinkRecovery, verifyOnMicroRecovery, verifyDialogRecovery, verifyNordicRecovery } from './ota'
import type { ScreenConfig } from '../types'
import { db, type DevicePreference } from '../data/db'

describe('InkTag BLE connection lifecycle', () => {
  it('takes ownership from abandoned TLSR discovery before starting an ordinary connection', async () => {
    const cancel = vi.fn(), oldAdapter = window.TelinkOta
    window.TelinkOta = { cancelRecoveryDiscovery: cancel }
    const connect = vi.fn().mockRejectedValue(new DOMException('denied', 'SecurityError'))
    const device = Object.assign(new EventTarget(), { id: 'same-exact-grant', gatt: { connected: false, connect } }) as unknown as BluetoothDevice
    try {
      await expect(new InkTagClient().reconnectDevice(device)).rejects.toMatchObject({ name: 'SecurityError' })
      expect(cancel).toHaveBeenCalledExactlyOnceWith(device)
      expect(cancel.mock.invocationCallOrder[0]).toBeLessThan(connect.mock.invocationCallOrder[0])
    } finally { window.TelinkOta = oldAdapter }
  })
  it.each(['chooser', 'remembered'] as const)('leaves OTA ownership intact when %s interactive connection is requested', async (entry) => {
    const device = { id: 'exact-grant', name: 'IKT_NRF_6775' } as BluetoothDevice
    const requestDevice = vi.fn().mockResolvedValue(device)
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: { requestDevice } })
    const client = new InkTagClient()
    const interactive = vi.fn().mockResolvedValue(undefined)
    ;(client as unknown as { connectInteractiveDevice: typeof interactive }).connectInteractiveDevice = interactive
    useDeviceStore.setState({ connection: 'idle', action: 'ota', actionProgress: .5 })
    if (entry === 'chooser') await client.connect()
    else await client.connectKnownDevice(device)
    expect(requestDevice).not.toHaveBeenCalled()
    expect(interactive).not.toHaveBeenCalled()
    expect(useDeviceStore.getState()).toMatchObject({ connection: 'idle', action: 'ota', actionProgress: .5 })
  })

  it('allows the OTA-owned attestation reconnect without opening another chooser', async () => {
    const device = { id: 'exact-grant' } as BluetoothDevice
    const client = new InkTagClient()
    const reconnect = vi.fn().mockResolvedValue(undefined)
    ;(client as unknown as { connectDevice: typeof reconnect }).connectDevice = reconnect
    useDeviceStore.getState().setAction('ota', 1)
    await client.reconnectDevice(device, 3210)
    expect(reconnect).toHaveBeenCalledExactlyOnceWith(device, 3210, false, false)
    expect(useDeviceStore.getState().action).toBe('ota')
  })

  it('does not turn generic display stop into an OTA cancellation or clear its busy state', async () => {
    const client = new InkTagClient()
    const stop = vi.fn().mockResolvedValue(undefined)
    ;(client as unknown as { stopDeviceWork: typeof stop }).stopDeviceWork = stop
    useDeviceStore.getState().setAction('ota', .5)
    await client.stop()
    expect(stop).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().action).toBe('ota')
  })

  it('sends the actual CANCEL request even when browser action is idle', async () => {
    const client = new InkTagClient()
    const request = vi.fn().mockResolvedValue({ payload: new Uint8Array() })
    ;(client as unknown as { requestV2: typeof request }).requestV2 = request
    useDeviceStore.setState({ connection: 'connected', action: 'idle' })
    await client.stop()
    expect(request).toHaveBeenCalledExactlyOnceWith(CommandV2.CANCEL)
    expect(useDeviceStore.getState().action).toBe('idle')
  })

  it('rejects legacy Find enable before issuing any request', async () => {
    const client = new InkTagClient()
    const request = vi.fn()
    ;(client as unknown as { requestV2: typeof request }).requestV2 = request
    await expect(client.configureFind(true)).rejects.toThrow('Find v2')
    expect(request).not.toHaveBeenCalled()
    expect('provisionFindOwner' in client).toBe(false)
    expect('stageFindOwnerRotation' in client).toBe(false)
  })

  beforeEach(() => {
    vi.useRealTimers()
    Object.defineProperty(globalThis, 'window', {
      configurable: true,
      value: globalThis,
    })
    Object.defineProperty(globalThis.navigator, 'bluetooth', {
      configurable: true,
      value: {},
    })
    useDeviceStore.getState().resetDevice()
    useDeviceStore.getState().clearLogs()
    vi.mocked(verifyGoodixRecovery).mockReset().mockResolvedValue(undefined)
    vi.mocked(verifyTelinkRecovery).mockReset().mockResolvedValue({ status: 9 })
    vi.mocked(verifyOnMicroRecovery).mockReset().mockResolvedValue(undefined)
    vi.mocked(verifyDialogRecovery).mockReset().mockResolvedValue({ safeResume: false })
    vi.mocked(verifyNordicRecovery).mockReset().mockResolvedValue({ safeResume: false })
  })

  it('returns to idle when the browser cancels selection without opening GATT', async () => {
    const requestDevice = vi.fn().mockRejectedValue(new DOMException('No device selected', 'NotFoundError'))
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: { requestDevice } })
    const client = new InkTagClient()
    const connectDevice = vi.fn()
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice

    await client.connect({ onlyInkTag: true, preferredName: 'IKT_TL_71EB' })

    expect(requestDevice).toHaveBeenCalledTimes(1)
    expect(connectDevice).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('idle')
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'info', message: '已取消设备选择' })
  })

  it('reports a missing App service after selection as a connection failure', async () => {
    const server = { getPrimaryService: vi.fn().mockRejectedValue(new DOMException('No InkTag service', 'NotFoundError')) }
    const device = Object.assign(new EventTarget(), { id: 'selected-grant', name: 'IKT_GR_1234',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() } }) as unknown as BluetoothDevice
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true,
      value: { requestDevice: vi.fn().mockResolvedValue(device) } })

    await new InkTagClient().connect()

    expect(useDeviceStore.getState().connection).toBe('error')
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'error', message: '连接失败：NotFoundError: No InkTag service' })
  })

  it('reports a browser permission rejection without retrying selection or GATT', async () => {
    const requestDevice = vi.fn().mockRejectedValue(new DOMException('Permission denied', 'SecurityError'))
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: { requestDevice } })
    const client = new InkTagClient()
    const connectDevice = vi.fn()
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice

    await client.connect()

    expect(requestDevice).toHaveBeenCalledTimes(1)
    expect(connectDevice).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('error')
    expect(useDeviceStore.getState().logs.at(-1)).toMatchObject({ level: 'error' })
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('SecurityError')
  })

  it('does not label a pending browser chooser as a connected device', async () => {
    let cancel!: (error: DOMException) => void
    const requestDevice = vi.fn().mockReturnValue(new Promise((_resolve, reject) => { cancel = reject }))
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: { requestDevice } })
    const client = new InkTagClient()
    const connectDevice = vi.fn()
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice

    const selection = client.connect()
    expect(useDeviceStore.getState().connection).toBe('selecting')
    expect(connectDevice).not.toHaveBeenCalled()
    cancel(new DOMException('No device selected', 'NotFoundError'))
    await selection
    expect(useDeviceStore.getState().connection).toBe('idle')
  })

  it('keeps unsupported browsers offline without attempting a connection', async () => {
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: undefined })
    const client = new InkTagClient()
    const connectDevice = vi.fn()
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice

    await client.connect()

    expect(connectDevice).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('unsupported')
  })

  it('tracks native layout drawing and physical screen refresh as separate phases', () => {
    expect(displayWorkDetailFromLog(
      'calendar',
      'gui:black drv=13 active=13 busy=0',
    )).toBe('设备正在绘制日历布局。')
    expect(displayWorkDetailFromLog(
      'calendar',
      'gui:refresh drv=13 active=13 busy=0',
    )).toBe('布局绘制完成，设备正在刷新墨水屏。')
    expect(displayWorkDetailFromLog(
      'device-info',
      'gui:area6 drv=16 active=16 busy=0',
    )).toBe('设备正在绘制设备信息页布局。')
  })

  it('retries remembered GATT failures with longer backoff and stops on success', async () => {
    vi.useFakeTimers()
    const client = new InkTagClient()
    const connectDevice = vi.fn()
      .mockRejectedValueOnce(new DOMException(
        'GATT Server is disconnected. Cannot retrieve services.',
        'NetworkError',
      ))
      .mockRejectedValueOnce(new DOMException(
        'GATT Server is disconnected. Cannot retrieve services.',
        'NetworkError',
      ))
      .mockResolvedValueOnce(undefined)
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice
    const device = { id: 'remembered-gr', name: 'InkTag_GR_2557' } as BluetoothDevice

    const connection = client.connectKnownDevice(device)
    await vi.advanceTimersByTimeAsync(1000)
    await vi.advanceTimersByTimeAsync(2000)
    await connection

    expect(connectDevice).toHaveBeenCalledTimes(3)
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('自动重试')
    expect(isTransientGattBootstrapError(new Error('application handshake failed'))).toBe(false)
  })

  it('keeps the base connection when an advertised optional Find probe is unavailable', () => {
    expect(isOptionalFindProbeUnavailable(new ProtocolV2Error(StatusV2.DENIED))).toBe(true)
    expect(isOptionalFindProbeUnavailable(new ProtocolV2Error(StatusV2.UNSUPPORTED))).toBe(true)
    expect(isOptionalFindProbeUnavailable(new ProtocolV2Error(StatusV2.TIMEOUT))).toBe(false)
    expect(isOptionalFindProbeUnavailable(new Error('GATT disconnected'))).toBe(false)
  })

  it('authorizes an enabled Find identity before sending the exact disable request', async () => {
    const client = new InkTagClient() as unknown as {
      readFindConfig: ReturnType<typeof vi.fn>
      authorizeOwnerAction: ReturnType<typeof vi.fn>
      requestV2: ReturnType<typeof vi.fn>
      configureFind: (enabled: boolean) => Promise<{ enabled: boolean }>
    }
    client.readFindConfig = vi.fn().mockResolvedValue({ enabled: true })
    client.authorizeOwnerAction = vi.fn().mockResolvedValue(undefined)
    client.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array([1, 0, 1, 1, 0]) })

    expect((await client.configureFind(false)).enabled).toBe(false)
    expect(client.authorizeOwnerAction).toHaveBeenCalledWith(
      CommandV2.SET_FIND_CONFIG,
      new Uint8Array([1, 0, 1]),
      RequestFlagV2.NONE,
    )
    expect(client.requestV2).toHaveBeenCalledWith(
      CommandV2.SET_FIND_CONFIG,
      new Uint8Array([1, 0, 1]),
      RequestFlagV2.NONE,
    )
    expect(client.authorizeOwnerAction.mock.invocationCallOrder[0]).toBeLessThan(
      client.requestV2.mock.invocationCallOrder[0],
    )
  })

  it('uses an unlocked offline owner only for the same ordinary Find disable admission', async () => {
    const client = new InkTagClient() as unknown as {
      readFindConfig: ReturnType<typeof vi.fn>; authorizeOwnerAction: ReturnType<typeof vi.fn>
      requestV2: ReturnType<typeof vi.fn>; configureFind: (enabled: boolean, key: Uint8Array) => Promise<{ enabled: boolean }>
    }
    client.readFindConfig = vi.fn().mockResolvedValue({ enabled: true })
    client.authorizeOwnerAction = vi.fn().mockResolvedValue(undefined)
    client.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array([1, 0, 1, 1, 0]) })
    const key = new Uint8Array(16).fill(7)
    expect((await client.configureFind(false, key)).enabled).toBe(false)
    expect(client.authorizeOwnerAction).toHaveBeenCalledWith(CommandV2.SET_FIND_CONFIG,
      new Uint8Array([1, 0, 1]), RequestFlagV2.NONE, undefined, key)
    expect(client.requestV2).toHaveBeenCalledWith(CommandV2.SET_FIND_CONFIG, new Uint8Array([1, 0, 1]), RequestFlagV2.NONE)
  })

  it('constructs a fresh device-bound owner proof from an unlocked key without a backend request', async () => {
    const client = new InkTagClient() as unknown as {
      v2Capabilities: number; readFindConfig: ReturnType<typeof vi.fn>; requestV2: ReturnType<typeof vi.fn>
      configureFind: (enabled: boolean, key: Uint8Array) => Promise<{ enabled: boolean }>
    }
    useDeviceStore.setState({ activation: { ...useDeviceStore.getState().activation, activationId: '0123456789ABCDEF' } })
    client.v2Capabilities = CapabilityV2.OWNER_AUTHORIZATION | CapabilityV2.FIND
    client.readFindConfig = vi.fn().mockResolvedValue({ enabled: true })
    client.requestV2 = vi.fn(async opcode => ({ payload: opcode === CommandV2.GET_OWNER_CHALLENGE
      ? new Uint8Array([1, ...Array.from({ length: 16 }, (_, i) => i + 1)])
      : opcode === CommandV2.SET_FIND_CONFIG ? new Uint8Array([1, 0, 1, 1, 0]) : new Uint8Array(0) }))
    await client.configureFind(false, new Uint8Array(16).fill(7))
    expect(client.requestV2.mock.calls.map(c => c[0])).toEqual([
      CommandV2.GET_OWNER_CHALLENGE, CommandV2.AUTHORIZE_OWNER_ACTION, CommandV2.SET_FIND_CONFIG,
    ])
    const proof = client.requestV2.mock.calls[1][1] as Uint8Array
    expect(proof).toHaveLength(50); expect(Array.from(proof.slice(0, 2))).toEqual([1, 2])
    expect(proof).not.toEqual(new Uint8Array(50).fill(7))
  })

  it('encodes a status-light diagnostic as the shared RGB bit mask', async () => {
    const client = new InkTagClient()
    const requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array(0), transactionId: 1 })
    ;(client as unknown as { protocolV2: boolean; requestV2: typeof requestV2 }).protocolV2 = true
    ;(client as unknown as { requestV2: typeof requestV2 }).requestV2 = requestV2

    Object.assign(client, { v2Capabilities: CapabilityV2.TEST })
    requestV2.mockResolvedValueOnce({ payload: new Uint8Array([1, 1, TestOperationV2.LED_CHANNEL]), transactionId: 1 })
    await client.testLed(0x05)

    expect(requestV2).toHaveBeenCalledWith(
      CommandV2.RUN_TEST,
      new Uint8Array([TestOperationV2.LED_CHANNEL, 0x05]),
    )
    await expect(client.testLed(8)).rejects.toThrow('0–7')
  })

  it('requires the complete display settings v3 record', async () => {
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      getDisplaySettings: InkTagClient['getDisplaySettings']
      requestV2: ReturnType<typeof vi.fn>
    }
    client.protocolV2 = true
    client.requestV2 = vi.fn()
      .mockResolvedValueOnce({
        payload: new Uint8Array([
          SettingsSectionV2.DISPLAY, DISPLAY_SETTINGS_PROTOCOL_VERSION, 1,
          DISPLAY_SETTINGS_BOOT_REFRESH | DISPLAY_SETTINGS_BRAND_HIDDEN,
          0, 0, 0, 0, 0,
        ]),
        transactionId: 1,
      })

    await expect(client.getDisplaySettings()).resolves.toMatchObject({
      supported: true,
      complete: true,
      bootRefreshEnabled: true,
      brandVisibilitySupported: true,
      brandVisible: false,
      countdownName: '',
      countdownDate: '',
    })
  })

  it('preserves boot refresh while changing the Pro brand visibility flag', async () => {
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      setBrandVisible: InkTagClient['setBrandVisible']
      requestV2: ReturnType<typeof vi.fn>
    }
    client.protocolV2 = true
    client.requestV2 = vi.fn()
      .mockResolvedValueOnce({
        payload: new Uint8Array([
          SettingsSectionV2.DISPLAY, DISPLAY_SETTINGS_PROTOCOL_VERSION, 0,
          DISPLAY_SETTINGS_BOOT_REFRESH,
          0, 0, 0, 0, 0,
        ]),
        transactionId: 1,
      })
      .mockImplementationOnce(async (_opcode: number, payload: Uint8Array) => ({
        payload: payload.slice(),
        transactionId: 2,
      }))

    await expect(client.setBrandVisible(false)).resolves.toBe(false)
    expect(client.requestV2).toHaveBeenNthCalledWith(
      2,
      CommandV2.SET_SETTINGS,
      new Uint8Array([
        SettingsSectionV2.DISPLAY, DISPLAY_SETTINGS_PROTOCOL_VERSION, 0,
        DISPLAY_SETTINGS_BOOT_REFRESH | DISPLAY_SETTINGS_BRAND_HIDDEN,
        0, 0, 0, 0, 0,
      ]),
    )
  })

  it('gates and encodes the bounded V2 ghost-clean test action', async () => {
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      testGhostClean: InkTagClient['testGhostClean']
      requestV2: ReturnType<typeof vi.fn>
      waitForV2Completion: ReturnType<typeof vi.fn>
    }
    client.protocolV2 = true
    client.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array(0), transactionId: 1 })
    client.waitForV2Completion = vi.fn().mockResolvedValue(new Uint8Array(0))

    await expect(client.testGhostClean(1)).rejects.toThrow('未提供残影清洁测试')
    useDeviceStore.getState().setSettingsCapabilities({
      ...useDeviceStore.getState().settingsCapabilities,
      protocolVersion: 2,
      displayActionMask: 1 << DisplayActionV2.GHOST_CLEAN,
    })
    await expect(client.testGhostClean(0)).rejects.toThrow('1–10')
    await expect(client.testGhostClean(11)).rejects.toThrow('1–10')
    await client.testGhostClean(10)
    expect(client.requestV2).toHaveBeenCalledWith(
      CommandV2.DISPLAY_ACTION,
      new Uint8Array([DisplayActionV2.GHOST_CLEAN, 10]),
      RequestFlagV2.CONFIRM,
      10000,
      expect.any(Number),
    )
  })

  it('writes the device-information QR through the transactional V2 transfer', async () => {
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      v2WritePayload: number
      writeDeviceInfoQr: InkTagClient['writeDeviceInfoQr']
      requestV2: ReturnType<typeof vi.fn>
    }
    client.protocolV2 = true
    client.v2WritePayload = 64
    client.requestV2 = vi.fn().mockResolvedValue({
      payload: new Uint8Array([TransferObjectV2.DEVICE_INFO_QR, 0xff, 64, 0]),
      transactionId: 1,
    })
    useDeviceStore.getState().setSettingsCapabilities({
      ...useDeviceStore.getState().settingsCapabilities,
      deviceInfoQr: true,
    })

    await client.writeDeviceInfoQr(0, 'https://github.com/solar2ain/ink-tag')

    const calls = client.requestV2.mock.calls
    expect(calls[0][0]).toBe(CommandV2.TRANSFER_BEGIN)
    expect(calls[0][1][0]).toBe(TransferObjectV2.DEVICE_INFO_QR)
    expect(calls[0][1][2]).toBe(1)
    expect(calls[0][1][3]).toBe(0)
    expect(calls.filter((call) => call[0] === CommandV2.TRANSFER_WRITE).length).toBeGreaterThan(0)
    expect(calls.at(-1)?.[0]).toBe(CommandV2.TRANSFER_COMMIT)
    expect(new Set(calls.map((call) => call[4]))).toEqual(new Set([1]))
  })

  it('regenerates the managed QR and NFC link from the exact BLE name', async () => {
    const client = new InkTagClient() as unknown as {
      syncManagedDeviceLinks: InkTagClient['syncManagedDeviceLinks']
      writeDeviceInfoQr: ReturnType<typeof vi.fn>
      writeNfcWebLink: ReturnType<typeof vi.fn>
    }
    client.writeDeviceInfoQr = vi.fn().mockResolvedValue(undefined)
    client.writeNfcWebLink = vi.fn().mockResolvedValue({ status: 'written' })
    useDeviceStore.getState().setCapabilities({ nfc: 'hardware' })

    const result = await client.syncManagedDeviceLinks('InkTag_客厅')

    const value = 'https://solar2ain.github.io/ink-tag/?ble=InkTag_%E5%AE%A2%E5%8E%85'
    expect(result.value).toBe(value)
    expect(client.writeDeviceInfoQr).toHaveBeenCalledWith(0, value)
    expect(client.writeNfcWebLink).toHaveBeenCalledWith(value)
  })

  it.each([StatusV2.UNKNOWN_COMMAND, StatusV2.UNSUPPORTED])(
    'treats an unavailable NFC capability probe (%s) as an unsupported optional link',
    async (status) => {
      const client = new InkTagClient() as unknown as {
        writeNfcWebLink: InkTagClient['writeNfcWebLink']
        requestV2: ReturnType<typeof vi.fn>
      }
      client.requestV2 = vi.fn().mockRejectedValue(new ProtocolV2Error(status))

      await expect(client.writeNfcWebLink('https://solar2ain.github.io/ink-tag/'))
        .resolves.toEqual({ status: 'unsupported', value: 'https://solar2ain.github.io/ink-tag/' })
    },
  )

  it('reports a present but unsupported NFC link as an incomplete synchronization', async () => {
    const client = new InkTagClient()
    const qr = vi.spyOn(client, 'writeDeviceInfoQr').mockResolvedValue(undefined)
    const nfc = vi.spyOn(client, 'writeNfcWebLink').mockResolvedValue({ status: 'unsupported', value: '' })
    useDeviceStore.getState().setCapabilities({ nfc: 'hardware' })

    await expect(client.syncManagedDeviceLinks('IKT_OM_63CF'))
      .rejects.toThrow('NFC 链接：当前设备不支持更新')
    expect(qr).toHaveBeenCalledOnce()
    expect(nfc).toHaveBeenCalledOnce()
  })

  it('does not attempt to synchronize an absent NFC link', async () => {
    const client = new InkTagClient()
    vi.spyOn(client, 'writeDeviceInfoQr').mockResolvedValue(undefined)
    const nfc = vi.spyOn(client, 'writeNfcWebLink')
    useDeviceStore.getState().setCapabilities({ nfc: 'none' })

    await expect(client.syncManagedDeviceLinks('IKT_OM_63CF'))
      .resolves.toMatchObject({ nfc: { status: 'not-present' } })
    expect(nfc).not.toHaveBeenCalled()
  })

  it('recovers a lost transfer-write ACK from the device offset', async () => {
    const client = new InkTagClient() as unknown as {
      writeV2TransferChunk: (transactionId: number, objectType: number, totalSize: number, offset: number, chunk: Uint8Array) => Promise<void>
      requestV2: ReturnType<typeof vi.fn>
    }
    const chunk = new Uint8Array([1, 2, 3, 4])
    const status = new Uint8Array(11)
    status.set([1, TransferObjectV2.DIRECT_FRAME, 7])
    new DataView(status.buffer).setUint32(3, chunk.byteLength, true)
    new DataView(status.buffer).setUint32(7, chunk.byteLength, true)
    client.requestV2 = vi.fn()
      .mockRejectedValueOnce(new ProtocolV2Error(StatusV2.TIMEOUT))
      .mockResolvedValueOnce({ payload: status, transactionId: 7 })

    await client.writeV2TransferChunk(
      7, TransferObjectV2.DIRECT_FRAME, chunk.byteLength, 0, chunk,
    )

    expect(client.requestV2.mock.calls.map((call) => call[0])).toEqual([
      CommandV2.TRANSFER_WRITE,
      CommandV2.GET_TRANSFER_STATUS,
    ])
  })

  it('frames compressed transfer writes with decoded length and decoded ACK offsets', async () => {
    const client = new InkTagClient() as unknown as {
      writeV2TransferChunk: (
        transactionId: number, objectType: number, totalSize: number, offset: number,
        chunk: Uint8Array, decodedLength?: number, timeout?: number,
      ) => Promise<void>
      requestV2: ReturnType<typeof vi.fn>
    }
    const encoded = new Uint8Array([0x87, 0xff])
    const status = new Uint8Array(11)
    status.set([1, TransferObjectV2.DIRECT_FRAME, 12])
    new DataView(status.buffer).setUint32(3, 10, true)
    new DataView(status.buffer).setUint32(7, 10, true)
    client.requestV2 = vi.fn()
      .mockRejectedValueOnce(new ProtocolV2Error(StatusV2.TIMEOUT))
      .mockResolvedValueOnce({ payload: status, transactionId: 12 })

    await client.writeV2TransferChunk(
      12, TransferObjectV2.DIRECT_FRAME, 10, 0, encoded, 10, 90000,
    )

    const payload = client.requestV2.mock.calls[0][1] as Uint8Array
    expect(new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint32(0, true)).toBe(0)
    expect(new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint16(4, true)).toBe(10)
    expect(payload.subarray(6)).toEqual(encoded)
    expect(client.requestV2.mock.calls[0][3]).toBe(90000)
  })

  it('uses transfer status and completion when a display commit ACK is delayed', async () => {
    const client = new InkTagClient() as unknown as {
      commitV2Transfer: (
        transactionId: number, objectType: number, totalSize: number,
        completion: Promise<Uint8Array>, timeout?: number,
      ) => Promise<{ payload: Uint8Array; completionConsumed: boolean }>
      requestV2: ReturnType<typeof vi.fn>
    }
    const status = new Uint8Array(11)
    status[0] = 0
    client.requestV2 = vi.fn()
      .mockRejectedValueOnce(new ProtocolV2Error(StatusV2.TIMEOUT))
      .mockResolvedValueOnce({ payload: status, transactionId: 17 })

    await expect(client.commitV2Transfer(
      17, TransferObjectV2.DIRECT_FRAME, 1024,
      Promise.resolve(new Uint8Array([1])), 100,
    )).resolves.toEqual({ payload: new Uint8Array(0), completionConsumed: true })
    expect(client.requestV2.mock.calls.map((call) => call[0])).toEqual([
      CommandV2.TRANSFER_COMMIT,
      CommandV2.GET_TRANSFER_STATUS,
    ])
  })

  it('releases device busy state but keeps a failed display operation visible', async () => {
    const client = new InkTagClient()
    const timeout = new ProtocolV2Error(StatusV2.TIMEOUT)
    ;(client as unknown as { protocolV2: boolean }).protocolV2 = true
    client.syncTime = vi.fn().mockRejectedValue(timeout)

    await expect(client.runAction('calendar')).rejects.toBe(timeout)

    const state = useDeviceStore.getState()
    expect(state.action).toBe('idle')
    expect(state.operationFailure?.action).toBe('calendar')
    expect(state.operationFailure?.detail).toContain('超时')
    expect(state.operationFailure?.retry).toBeTypeOf('function')
  })

  it('keeps a busy display conflict available for explicit keep-or-replace', async () => {
    const client = new InkTagClient()
    const busy = new ProtocolV2Error(StatusV2.BUSY)
    ;(client as unknown as { protocolV2: boolean }).protocolV2 = true
    client.syncTime = vi.fn().mockRejectedValue(busy)

    await expect(client.runAction('calendar')).rejects.toBe(busy)

    const state = useDeviceStore.getState()
    expect(state.action).toBe('calendar')
    expect(state.actionDetail).toContain('本次操作没有排队')
    expect(state.operationFailure).toBeNull()
  })

  it('reports whether this browser can restore devices granted to the current origin', async () => {
    const remembered = { id: 'remembered-gr', name: 'InkTag_GR_2557' } as BluetoothDevice
    Object.defineProperty(globalThis.navigator, 'bluetooth', {
      configurable: true,
      value: { getDevices: vi.fn().mockResolvedValue([remembered]) },
    })
    const client = new InkTagClient()

    expect(client.canRestoreKnownDevices()).toBe(true)
    await expect(client.getKnownDevices()).resolves.toEqual([remembered])

    Object.defineProperty(globalThis.navigator, 'bluetooth', {
      configurable: true,
      value: {},
    })
    expect(client.canRestoreKnownDevices()).toBe(false)
    await expect(client.getKnownDevices()).resolves.toEqual([])
  })

  it('leaves connecting state and permits a retry after a stalled GATT connect', async () => {
    vi.useFakeTimers()
    const connect = vi.fn(() => new Promise<BluetoothRemoteGATTServer>(() => undefined))
    const device = {
      id: 'om-timeout',
      name: 'InkTag_OM_TEST',
      gatt: { connected: false, connect, disconnect: vi.fn() },
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()

    const stalled = client.reconnectDevice(device, 100)
    const rejected = expect(stalled).rejects.toBeInstanceOf(BluetoothConnectionTimeoutError)
    await vi.advanceTimersByTimeAsync(100)
    await rejected
    expect(useDeviceStore.getState().connection).toBe('idle')

    connect.mockRejectedValueOnce(new Error('device still rebooting'))
    await expect(client.reconnectDevice(device, 100)).rejects.toThrow('device still rebooting')
    expect(connect).toHaveBeenCalledTimes(2)
  })

  it('keeps OTA progress while the expected reboot clears device identity', () => {
    const store = useDeviceStore.getState()
    store.setConnection('connecting')
    store.setAction('ota', 0.75, '设备正在重启')
    store.resetDevice(true)
    const state = useDeviceStore.getState()
    expect(state.connection).toBe('idle')
    expect(state.action).toBe('ota')
    expect(state.actionProgress).toBe(0.75)
    expect(state.actionDetail).toBe('设备正在重启')
  })

  it('downgrades image-store capability when the connected hardware reports no safe region', () => {
    const store = useDeviceStore.getState()
    store.setCapabilities({ imageStore: true })
    store.setImageStoreInfo({
      width: 400,
      height: 300,
      bytesPerImage: 30000,
      requiredSectors: 8,
      totalSectors: 132,
      freeSectors: 132,
      largestFreeRun: 132,
      fit: 16,
      contiguousFit: 16,
      saved: 0,
      compatible: 0,
      playable: 0,
      incompatible: 0,
      slideshow: false,
      intervalSeconds: 3600,
    })
    const client = new InkTagClient()
    const notify = client as unknown as {
      handleImageStoreNotification: (message: string) => void
    }

    notify.handleImageStoreNotification('img:unsupported')

    const state = useDeviceStore.getState()
    expect(state.capabilities.imageStore).toBe(false)
    expect(state.imageStoreInfo).toBeNull()
    expect(state.imageStoreItems).toEqual([])
    expect(state.logs.at(-1)?.message).toContain('没有安全可用的图片槽')
  })

  it('retries five times then explains that a remembered device may be offline', async () => {
    vi.useFakeTimers()
    const disconnect = vi.fn()
    const getPrimaryService = vi.fn(() => new Promise<BluetoothRemoteGATTService>(() => undefined))
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const device = {
      id: 'remembered-device',
      name: 'InkTag_OM_OLD',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect },
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()

    const stalled = client.connectKnownDevice(device)
    await vi.runAllTimersAsync()
    await stalled

    expect(getPrimaryService).toHaveBeenCalledTimes(5)
    expect(disconnect).toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('error')
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('浏览器记住设备不代表它当前在线')
  })

  it('rediscovers a remembered device after the range-cache error without opening a chooser', async () => {
    vi.useFakeTimers()
    const client = new InkTagClient()
    const connectDevice = vi.fn()
      .mockRejectedValueOnce(new DOMException('Bluetooth Device is no longer in range.', 'NetworkError'))
      .mockResolvedValueOnce(undefined)
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice
    const watch = vi.fn().mockResolvedValue(undefined)
    const requestDevice = vi.fn()
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: { requestDevice } })
    const device = Object.assign(new EventTarget(), { id: 'cached-tl', name: 'IKT_TL_TEST', watchAdvertisements: watch }) as unknown as BluetoothDevice
    const pending = client.connectKnownDevice(device)
    await vi.advanceTimersByTimeAsync(8000)
    expect(connectDevice).toHaveBeenCalledTimes(1)
    expect(useDeviceStore.getState().connection).toBe('connecting')
    device.dispatchEvent(new Event('advertisementreceived'))
    await vi.advanceTimersByTimeAsync(1000)
    await pending
    expect(watch).toHaveBeenCalledTimes(1)
    expect(watch.mock.calls[0][0].signal.aborted).toBe(true)
    expect(connectDevice).toHaveBeenCalledTimes(2)
    expect(requestDevice).not.toHaveBeenCalled()
  })

  it('bounds range-error retries when advertisement watching is unsupported', async () => {
    vi.useFakeTimers()
    const client = new InkTagClient()
    const connectDevice = vi.fn().mockRejectedValue(new DOMException('Bluetooth Device is no longer in range.', 'NetworkError'))
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice
    const pending = client.connectKnownDevice({ id: 'cached', name: 'IKT_GR_TEST' } as BluetoothDevice)
    await vi.runAllTimersAsync()
    await pending
    expect(connectDevice).toHaveBeenCalledTimes(5)
    expect(useDeviceStore.getState().connection).toBe('error')
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('广播及其他连接占用')
  })

  it.each(['advertisement', 'backoff'])('cancels remembered reconnection during %s without further GATT attempts', async (phase) => {
    vi.useFakeTimers()
    const client = new InkTagClient()
    const connectDevice = vi.fn().mockRejectedValue(new DOMException('Bluetooth Device is no longer in range.', 'NetworkError'))
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice
    const watch = vi.fn().mockResolvedValue(undefined)
    const device = Object.assign(new EventTarget(), { id: 'cancelled', ...(phase === 'advertisement' ? { watchAdvertisements: watch } : {}) }) as unknown as BluetoothDevice
    const pending = client.connectKnownDevice(device)
    await vi.advanceTimersByTimeAsync(100)
    client.disconnect()
    await pending
    await vi.runAllTimersAsync()
    expect(connectDevice).toHaveBeenCalledTimes(1)
    expect(useDeviceStore.getState().connection).toBe('idle')
    expect(useDeviceStore.getState().logs.some((entry) => entry.message.startsWith('连接失败：'))).toBe(false)
    if (phase === 'advertisement') expect(watch.mock.calls[0][0].signal.aborted).toBe(true)
  })

  it('does not retry protocol, authentication or permission refusals', async () => {
    const client = new InkTagClient()
    const connectDevice = vi.fn().mockRejectedValue(new DOMException('User denied access', 'SecurityError'))
    ;(client as unknown as { connectDevice: typeof connectDevice }).connectDevice = connectDevice
    await client.connectKnownDevice({ id: 'refused' } as BluetoothDevice)
    expect(connectDevice).toHaveBeenCalledTimes(1)
    expect(useDeviceStore.getState().connection).toBe('error')
  })

  it('keeps a user-requested disconnect idle while connection initialization is pending', async () => {
    let rejectConnection: ((error: Error) => void) | undefined
    const device = {
      id: 'pending-device',
      name: 'InkTag_GR_3A57',
      gatt: { connected: false },
    } as unknown as BluetoothDevice
    const client = new InkTagClient()
    const internal = client as unknown as {
      connectDevice: () => Promise<void>
    }
    internal.connectDevice = vi.fn(() => new Promise<void>((_resolve, reject) => {
      rejectConnection = reject
    }))

    const pending = client.connectKnownDevice(device)
    client.disconnect()
    rejectConnection?.(new Error('蓝牙传输已重置'))
    await pending

    const state = useDeviceStore.getState()
    expect(state.connection).toBe('idle')
    expect(state.logs.some((entry) => entry.message.startsWith('连接失败：'))).toBe(false)
  })

  it('disconnects a GATT connect that resolves after the UI abandoned it', async () => {
    let resolveConnect: ((server: BluetoothRemoteGATTServer) => void) | undefined
    const lateDisconnect = vi.fn()
    const server = {
      connected: true,
      disconnect: lateDisconnect,
    } as unknown as BluetoothRemoteGATTServer
    const connect = vi.fn(() => new Promise<BluetoothRemoteGATTServer>((resolve) => {
      resolveConnect = resolve
    }))
    const gatt = { connected: false, connect, disconnect: vi.fn() }
    const device = {
      id: 'late-winrt-connect',
      name: 'InkTag_OM_63CF',
      gatt,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()

    const pending = client.reconnectDevice(device, 1000)
    await vi.waitFor(() => expect(connect).toHaveBeenCalledOnce())
    client.disconnect()
    resolveConnect?.(server)
    await expect(pending).rejects.toThrow('蓝牙连接已取消')

    expect(lateDisconnect).toHaveBeenCalledOnce()
    expect(useDeviceStore.getState().connection).toBe('idle')
  })

  it('does not report disconnected while the browser still owns GATT', async () => {
    const disconnect = vi.fn()
    const gatt = {
      connected: true,
      connect: vi.fn(),
      disconnect,
    }
    const device = {
      id: 'sticky-gatt',
      name: 'InkTag_OM_63CF',
      gatt,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()
    ;(client as unknown as { device: BluetoothDevice | null }).device = device
    useDeviceStore.getState().setConnection('connected')

    client.disconnect()
    expect(disconnect).toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('connected')

    gatt.connected = false
    ;(client as unknown as { onDisconnected: (event: Event) => void }).onDisconnected({ target: device } as unknown as Event)
    expect(useDeviceStore.getState().connection).toBe('idle')
  })

  it('waits for the WinRT handover guard before connecting another device', async () => {
    vi.useFakeTimers()
    const gatt = {
      connected: true,
      connect: vi.fn(),
      disconnect: vi.fn(() => { gatt.connected = false }),
    }
    const device = {
      id: 'switch-source',
      name: 'InkTag_TL_9903',
      gatt,
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()
    ;(client as unknown as { device: BluetoothDevice | null }).device = device
    useDeviceStore.getState().setConnection('connected')

    let released = false
    const pending = client.disconnectAndWait().then(() => { released = true })
    await vi.advanceTimersByTimeAsync(299)
    expect(released).toBe(false)
    await vi.advanceTimersByTimeAsync(1)
    await pending

    expect(gatt.disconnect).toHaveBeenCalledOnce()
    expect(useDeviceStore.getState().connection).toBe('idle')
  })

  it('requires an exact V2 screen readback and never falls back to legacy writes', async () => {
    const requested: ScreenConfig = {
      schemaVersion: 1, driver: 0x13, recipe: 0, color: 2, transform: 0, nativeUiScale: 0,
      width: 400, height: 300, visibleX: 0, visibleY: 0,
      visibleWidth: 400, visibleHeight: 300,
    }
    const stale: ScreenConfig = {
      schemaVersion: 1, driver: 0x21, recipe: 0, color: 1, transform: 0, nativeUiScale: 0,
      width: 152, height: 152, visibleX: 0, visibleY: 0,
      visibleWidth: 152, visibleHeight: 152,
    }
    const client = new InkTagClient()
    const request = vi.fn().mockResolvedValueOnce({ payload: encodeScreenConfig(requested) })
    const rawWrite = vi.fn()
    Object.assign(client, { protocolV2: true, requestV2: request, writePacket: rawWrite, refreshV2ResourceInfo: vi.fn().mockResolvedValue(undefined) })
    useDeviceStore.getState().resetDevice()
    useDeviceStore.setState({ screen: stale })
    await expect(client.setScreen(requested)).resolves.toEqual(requested)
    expect(useDeviceStore.getState().screen).toEqual(requested)
    expect(request).toHaveBeenNthCalledWith(1, CommandV2.SET_SCREEN_PROFILE, encodeScreenConfig(requested))
    expect(rawWrite).not.toHaveBeenCalled()
    Object.assign(client, { protocolV2: false, requestV2: InkTagClient.prototype['requestV2'] })
    await expect(client.setScreen(requested)).rejects.toThrow('设备未启用当前 v1 协议')
    expect(rawWrite).not.toHaveBeenCalled()
  })

  it.each(['syncTime', 'requestTelemetry', 'simulateButton', 'stopSlideshow'] as const)(
    '%s refuses an uninitialized V2 session without sending an old command', async (operation) => {
      const client = new InkTagClient()
      const writePacket = vi.fn()
      Object.assign(client, { writePacket })
      const result = operation === 'simulateButton' ? client.simulateButton('single') : client[operation]()
      await expect(result).rejects.toThrow('设备未启用当前 v1 协议')
      expect(writePacket).not.toHaveBeenCalled()
      expect('write' in client).toBe(false)
      expect('setPins' in client).toBe(false)
    },
  )

  it('fails fast when every V2 transaction number is occupied', () => {
    const client = new InkTagClient() as unknown as {
      v2Pending: Map<number, unknown>
      nextV2Transaction: () => number
    }
    for (let transaction = 1; transaction <= 255; transaction += 1) {
      client.v2Pending.set(transaction, {})
    }

    expect(() => client.nextV2Transaction()).toThrow('没有可用的 v1 事务号')
  })

  it('records a matched application response for every visible V2 request', async () => {
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      characteristic: BluetoothRemoteGATTCharacteristic
      requestV2: (opcode: number) => Promise<{ payload: Uint8Array; transactionId: number }>
      handleV2Notification: (bytes: Uint8Array) => void
    }
    useDeviceStore.getState().clearLogs()
    client.protocolV2 = true
    client.characteristic = {
      writeValueWithResponse: vi.fn(async (value: BufferSource) => {
        const request = new Uint8Array(value as ArrayBuffer)
        expect(request[2]).toBe(0x10)
        client.handleV2Notification(new Uint8Array([
          request[0], request[1], StatusV2.OK, 0xa5,
        ]))
      }),
    } as unknown as BluetoothRemoteGATTCharacteristic

    await expect(client.requestV2(CommandV2.HELLO)).resolves.toEqual({
      payload: new Uint8Array([0xa5]),
      transactionId: 1,
    })

    const messages = useDeviceStore.getState().logs.map((entry) => entry.message)
    expect(messages).toContain('v1 请求：op=0x01 tx=1 flags=0x10 payload=0 B')
    expect(messages.some((message) => (
      message.includes('v1 响应：op=0x01 tx=1 status=OK payload=1 B')
    ))).toBe(true)
  })

  it('records an explicit V2 timeout when the application response is missing', async () => {
    vi.useFakeTimers()
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      characteristic: BluetoothRemoteGATTCharacteristic
      requestV2: (opcode: number, payload: Uint8Array, flags: number, timeout: number) => Promise<unknown>
    }
    useDeviceStore.getState().clearLogs()
    client.protocolV2 = true
    client.characteristic = {
      writeValueWithResponse: vi.fn().mockResolvedValue(undefined),
    } as unknown as BluetoothRemoteGATTCharacteristic

    const request = client.requestV2(CommandV2.HELLO, new Uint8Array(0), RequestFlagV2.NONE, 25)
    const rejected = expect(request).rejects.toBeInstanceOf(ProtocolV2Error)
    await vi.advanceTimersByTimeAsync(25)
    await rejected

    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('v1 响应超时：op=0x01 tx=1')
  })

  it.each([true,false])('reads Recovery state with a tagged v1 frame, never a single raw byte (valid=%s)', async valid => {
    const client=new InkTagClient() as unknown as {
      protocolV2:boolean;characteristic:BluetoothRemoteGATTCharacteristic
      queryRuntimeBuild:(timeout?:number)=>Promise<unknown>
      handleV2Notification:(bytes:Uint8Array)=>void
    }
    client.protocolV2=true
    const payload=new Uint8Array(valid ? 39:23), view=new DataView(payload.buffer)
    payload[0]=1
    view.setUint32(15,216400,true)
    if(valid) {payload.set([1,0,4,0],23);view.setUint32(27,101,true);view.setUint32(31,2,true)}
    const write=vi.fn(async(value:BufferSource)=>{
      const frame=new Uint8Array(value as ArrayBuffer)
      expect([...frame]).toEqual([CommandV2.GET_DEVICE_INFO,1,0x10])
      client.handleV2Notification(new Uint8Array([frame[0],frame[1],StatusV2.OK,...payload]))
    })
    client.characteristic={writeValueWithResponse:write} as unknown as BluetoothRemoteGATTCharacteristic
    if(valid) await expect(client.queryRuntimeBuild()).resolves.toMatchObject({build:101,swapState:4,swapGeneration:2})
    else await expect(client.queryRuntimeBuild()).rejects.toThrow('v1 Recovery 状态')
    expect(write).toHaveBeenCalledOnce()
  })

  const recoveryFamilies = [
    ['goodix', 'GR', 'a6ed0401-d344-460a-8075-b9e8ec90d71b', verifyGoodixRecovery],
    ['onmicro', 'OM', '000001bf-5f00-0000-0000-4f4d2d4f5441', verifyOnMicroRecovery],
    ['telink', 'TL', '00010203-0405-0607-0809-0a0b0c0d1912', verifyTelinkRecovery],
    ['dialog', 'DA', '0000fef5-0000-1000-8000-00805f9b34fb', verifyDialogRecovery],
    ['nordic', 'NRF', '0000fe59-0000-1000-8000-00805f9b34fb', verifyNordicRecovery],
  ] as const

  it.each(recoveryFamilies)('%s shared entry records the exact V2 identity before switching role', async (type, platform) => {
    const client = new InkTagClient() as unknown as {
      protocolV2: boolean
      device: BluetoothDevice
      enterOtaRecovery: () => Promise<void>
      requestV2: ReturnType<typeof vi.fn>
      expectedFixedRecovery: { type: string; deviceId: string; name: string } | null
    }
    client.protocolV2 = true
    client.device = { id: 'browser-device' } as BluetoothDevice
    useDeviceStore.getState().setIdentity({ bleAddress: 'AA:BB:CC:DD:12:34' })
    useDeviceStore.getState().setCapabilities({ ota: type })
    client.requestV2 = vi.fn().mockImplementation(async () => ({
      payload: new Uint8Array(0),
      transactionId: 1,
    }))

    await client.enterOtaRecovery()

    expect(client.requestV2).toHaveBeenNthCalledWith(
      1,
      CommandV2.SYSTEM_ACTION,
      new Uint8Array([SystemActionV2.OTA_RECOVERY]),
      RequestFlagV2.NONE,
      12000,
    )
    expect(client.expectedFixedRecovery).toEqual({
      type,
      deviceId: 'browser-device',
      name: `IKT_${platform}_1234_OTA`,
      legacyName: `InkTag_${platform}_1234_OTA`,
    })
  })

  it.each(recoveryFamilies)('%s accepts a cached App name only on its pre-entry grant and live Recovery protocol', async (type, platform, serviceUuid, verify) => {
    const getCharacteristic = vi.fn().mockResolvedValue({})
    const server = { getPrimaryService: vi.fn(async (uuid: string) => {
      if (uuid === serviceUuid) return { getCharacteristic }
      throw new DOMException('not found', 'NotFoundError')
    }) } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'same-browser-device', name: `IKT_${platform}_1234`,
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(), removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()
    const internals = client as unknown as { protocolV2: boolean; device: BluetoothDevice; requestV2: ReturnType<typeof vi.fn> }
    internals.protocolV2 = true
    internals.device = device
    internals.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array(0), transactionId: 1 })
    useDeviceStore.getState().setIdentity({ bleAddress: 'AA:BB:CC:DD:12:34' })
    useDeviceStore.getState().setCapabilities({ ota: type })
    await client.enterOtaRecovery()
    const handoff = client.getExpectedRecoveryIdentity()
    const discover = vi.mocked(server.getPrimaryService)
    discover.mockResolvedValueOnce({ getCharacteristic } as unknown as BluetoothRemoteGATTService)
    await expect(client.reconnectDevice(device, 10000, true)).rejects.toThrow('已返回应用')
    expect(client.getExpectedRecoveryIdentity()).toEqual(handoff)
    expect(getCharacteristic).not.toHaveBeenCalled()
    await client.reconnectDevice(device, 10000, true)
    expect(client.getOtaRecoveryType()).toBe(type)
    expect(client.getVerifiedRecoveryName()).toBe(`IKT_${platform}_1234_OTA`)
    expect(verify).toHaveBeenCalledExactlyOnceWith(server)
    expect(useDeviceStore.getState()).toMatchObject({ connection: 'connected', firmwareVersion: 'Recovery', temperature: null })
    // Role verification is read-only; the single entry is never replayed.
    expect(internals.requestV2).toHaveBeenCalledOnce()
    client.disconnect()
    await expect(client.reconnectDevice({ ...device, id: 'other-device' } as BluetoothDevice)).rejects.toBeInstanceOf(DOMException)
    expect(verify).toHaveBeenCalledTimes(1)
  })

  it.each(recoveryFamilies)('%s rejects a cached App grant with a mismatched explicit Recovery ID', async (type, platform, serviceUuid, verify) => {
    const server = { getPrimaryService: vi.fn(async (uuid: string) => {
      if (uuid === serviceUuid) return { getCharacteristic: vi.fn().mockResolvedValue({}) }
      throw new DOMException('not found', 'NotFoundError')
    }) } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'same-browser-device', name: `IKT_${platform}_9999_OTA`,
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(), removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()
    const internals = client as unknown as { protocolV2: boolean; device: BluetoothDevice; requestV2: ReturnType<typeof vi.fn> }
    internals.protocolV2 = true; internals.device = device
    internals.requestV2 = vi.fn().mockResolvedValue({ payload: new Uint8Array(0), transactionId: 1 })
    useDeviceStore.getState().setIdentity({ bleAddress: 'AA:BB:CC:DD:12:34' })
    useDeviceStore.getState().setCapabilities({ ota: type })
    await client.enterOtaRecovery()
    await expect(client.reconnectDevice(device)).rejects.toThrow('Recovery 身份不匹配')
    expect(verify).not.toHaveBeenCalled()
    expect(client.getOtaRecoveryType()).toBeNull()
  })

  it('accepts only a stable-format fixed Recovery name on a fresh browser grant', async () => {
    const recoveryService = {} as BluetoothRemoteGATTService
    const getPrimaryService = vi.fn(async (uuid: BluetoothServiceUUID) => {
      if (uuid === '62750001-d828-918d-fb46-b6c11c675aec') throw new DOMException('not found', 'NotFoundError')
      if (uuid === '0000fef5-0000-1000-8000-00805f9b34fb') return recoveryService
      throw new DOMException('unexpected service', 'NotFoundError')
    })
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const device = {
      id: 'fresh-da-recovery',
      name: 'InkTag_DA_1234_OTA',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()

    await client.reconnectDevice(device)

    expect(client.getOtaRecoveryType()).toBe('dialog')
    expect(client.getLastConnectedDevice()).toBe(device)
    expect(verifyDialogRecovery).toHaveBeenCalledWith(server)
    expect(client.getRecoverySafeResumeSupported()).toBe(false)
    expect(useDeviceStore.getState().connection).toBe('connected')
    expect(getPrimaryService).toHaveBeenCalledWith('0000fef5-0000-1000-8000-00805f9b34fb')
  })

  it.each(['NetworkError', 'SecurityError', 'AbortError'])('does not infer Recovery from a cached OTA name after App discovery %s', async (name) => {
    const failure = new DOMException('App service discovery failed', name)
    const getPrimaryService = vi.fn().mockRejectedValue(failure)
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'stale-om-name', name: 'IKT_OM_B892_OTA',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const client = new InkTagClient()
    await expect(client.reconnectDevice(device)).rejects.toBe(failure)
    expect(getPrimaryService).toHaveBeenCalledExactlyOnceWith('62750001-d828-918d-fb46-b6c11c675aec')
    expect(verifyOnMicroRecovery).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().logs.some(entry => entry.message.includes('读取 InkTag 服务') && entry.level === 'error')).toBe(true)
  })

  it('preserves the App discovery timeout rather than probing cached Recovery', async () => {
    vi.useFakeTimers()
    const getPrimaryService = vi.fn(() => new Promise<BluetoothRemoteGATTService>(() => undefined))
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'stale-om-timeout', name: 'IKT_OM_B892_OTA',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const pending = new InkTagClient().reconnectDevice(device, 15000)
    const rejected = expect(pending).rejects.toMatchObject({ name: 'BluetoothConnectionTimeoutError', phase: '读取 InkTag 服务' })
    await vi.runAllTimersAsync()
    await rejected
    expect(getPrimaryService).toHaveBeenCalledTimes(1)
    expect(verifyOnMicroRecovery).not.toHaveBeenCalled()
  })

  it.each(['resolve', 'reject'] as const)('ignores App discovery that completes after disconnect (%s)', async (completion) => {
    let resolve!: (service: BluetoothRemoteGATTService) => void
    let reject!: (error: Error) => void
    const getCharacteristic = vi.fn()
    const getPrimaryService = vi.fn(() => new Promise<BluetoothRemoteGATTService>((yes, no) => { resolve = yes; reject = no }))
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const gatt = { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn(() => { gatt.connected = false }) }
    const device = { id: 'cancelled-om', name: 'IKT_OM_B892_OTA', gatt,
      addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const client = new InkTagClient()
    const pending = client.reconnectDevice(device)
    const rejected = expect(pending).rejects.toThrow('蓝牙连接已取消')
    await vi.waitFor(() => expect(getPrimaryService).toHaveBeenCalledOnce())
    client.disconnect()
    if (completion === 'resolve') resolve({ getCharacteristic } as unknown as BluetoothRemoteGATTService)
    else reject(new DOMException('not found', 'NotFoundError'))
    await rejected
    expect(getPrimaryService).toHaveBeenCalledTimes(1)
    expect(getCharacteristic).not.toHaveBeenCalled()
    expect(verifyOnMicroRecovery).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('idle')
  })

  it('does not tear down a newer connection on the same grant when old discovery rejects', async () => {
    let rejectOld!: (error: Error) => void
    const getPrimaryService = vi.fn()
      .mockImplementationOnce(() => new Promise((_resolve, reject) => { rejectOld = reject }))
      .mockRejectedValueOnce(new DOMException('not found', 'NotFoundError'))
      .mockResolvedValue({})
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const gatt = { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn(() => { gatt.connected = false }) }
    const device = { id: 'same-om-grant', name: 'IKT_OM_B892_OTA', gatt,
      addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const client = new InkTagClient()
    const old = client.reconnectDevice(device)
    const rejected = expect(old).rejects.toThrow('蓝牙连接已取消')
    await vi.waitFor(() => expect(getPrimaryService).toHaveBeenCalledOnce())
    client.disconnect()
    gatt.connected = true
    await client.reconnectDevice(device)
    const removed = vi.mocked(device.removeEventListener).mock.calls.length
    const disconnected = gatt.disconnect.mock.calls.length
    rejectOld(new DOMException('not found', 'NotFoundError'))
    await rejected
    expect(gatt.disconnect).toHaveBeenCalledTimes(disconnected)
    expect(device.removeEventListener).toHaveBeenCalledTimes(removed)
    expect(client.getGattServer()).toBe(server)
    expect(useDeviceStore.getState().connection).toBe('connected')
  })

  it('does not close a new grant when its abandoned connect resolves late', async () => {
    let resolveOld!: (server: BluetoothRemoteGATTServer) => void
    const getPrimaryService = vi.fn(async (uuid: BluetoothServiceUUID) => {
      if (uuid === '62750001-d828-918d-fb46-b6c11c675aec') throw new DOMException('not found', 'NotFoundError')
      return {} as BluetoothRemoteGATTService
    })
    const server = { connected: true, getPrimaryService, disconnect: vi.fn() } as unknown as BluetoothRemoteGATTServer
    const connect = vi.fn().mockImplementationOnce(() => new Promise(resolve => { resolveOld = resolve })).mockResolvedValue(server)
    const gatt = { connected: false, connect, disconnect: vi.fn() }
    const device = { id: 'late-same-grant', name: 'IKT_OM_B892_OTA', gatt,
      addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const client = new InkTagClient()
    const old = client.reconnectDevice(device)
    const rejected = expect(old).rejects.toThrow('蓝牙连接已取消')
    await vi.waitFor(() => expect(connect).toHaveBeenCalledOnce())
    client.disconnect()
    gatt.connected = true
    await client.reconnectDevice(device)
    resolveOld(server)
    await rejected
    expect(server.disconnect).not.toHaveBeenCalled()
    expect(gatt.disconnect).not.toHaveBeenCalled()
    expect(useDeviceStore.getState().connection).toBe('connected')
  })

  function grRecovery(name = 'IKT_GR_1234_OTA') {
    const service = {} as BluetoothRemoteGATTService
    const server = { getPrimaryService: vi.fn(async (uuid: BluetoothServiceUUID) => {
      if (uuid === 'a6ed0401-d344-460a-8075-b9e8ec90d71b') return service
      throw new DOMException('not found', 'NotFoundError')
    }) } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'gr-recovery-grant', name,
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(), removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    return { device, server }
  }

  it.each(['chooser', 'remembered'] as const)('uses live TLSR Recovery proof for the same %s grant with a cached App name', async (entry) => {
    const getPrimaryService = vi.fn(async (uuid: BluetoothServiceUUID) => {
      if (uuid === '00010203-0405-0607-0809-0a0b0c0d1912') return {} as BluetoothRemoteGATTService
      throw new DOMException('missing App service', 'NotFoundError')
    })
    const server = { getPrimaryService }
    const device = Object.assign(new EventTarget(), { id: 'same-tlsr-grant', name: 'IKT_TL_1234',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() } }) as unknown as BluetoothDevice
    Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true,
      value: { requestDevice: vi.fn().mockResolvedValue(device) } })
    const client = new InkTagClient()

    const connect = () => entry === 'chooser' ? client.connect() : client.connectKnownDevice(device)
    await connect()

    expect(device.gatt?.connect).toHaveBeenCalledOnce()
    expect(verifyTelinkRecovery).toHaveBeenCalledExactlyOnceWith(server)
    expect(client.getVerifiedRecoveryName()).toBe('IKT_TL_1234_OTA')
    expect(useDeviceStore.getState()).toMatchObject({ connection: 'connected', deviceId: device.id, firmwareVersion: 'Recovery' })

    client.disconnect()
    vi.mocked(verifyTelinkRecovery).mockRejectedValueOnce(new Error('invalid TLSR Recovery profile'))
    await connect()
    expect(client.getOtaRecoveryType()).toBeNull()
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('invalid TLSR Recovery profile')

    getPrimaryService.mockRejectedValueOnce(new DOMException('GATT lookup denied', 'SecurityError'))
    await connect()
    expect(useDeviceStore.getState().connection).toBe('error')
    expect(verifyTelinkRecovery).toHaveBeenCalledTimes(2)
  })

  it('connects a verified TLSR Recovery even with LOW_POWER, without App metadata or mutation', async () => {
    const server = { getPrimaryService: vi.fn(async (uuid: BluetoothServiceUUID) => {
      if (uuid === '00010203-0405-0607-0809-0a0b0c0d1912') return {} as BluetoothRemoteGATTService
      throw new DOMException('not found', 'NotFoundError')
    }) } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'tl-recovery', name: 'IKT_TL_6F40_OTA',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(), removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()
    await client.reconnectDevice(device)
    expect(verifyTelinkRecovery).toHaveBeenCalledWith(server)
    expect(client.getOtaRecoveryType()).toBe('telink')
    expect(useDeviceStore.getState()).toMatchObject({ connection: 'connected',
      firmwareVersion: 'Recovery', voltage: null, temperature: null,
      capabilities: { ota: 'telink', imageStore: false, nfc: 'none' } })
  })
  it.each(['IKT_NRF_6775_OTA', 'InkTag_NRF_37DE_OTA'])('accepts %s only with both live Secure DFU channels', async (name) => {
    const getCharacteristic = vi.fn().mockResolvedValue({})
    const server = { getPrimaryService: vi.fn(async (uuid: string) => {
      if (uuid === '0000fe59-0000-1000-8000-00805f9b34fb') return { getCharacteristic }
      throw new DOMException('not found', 'NotFoundError')
    }) } as unknown as BluetoothRemoteGATTServer
    const device = { id: name, name, gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() }, addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const client = new InkTagClient()
    await client.reconnectDevice(device)
    expect(client.getOtaRecoveryType()).toBe('nordic')
    expect(verifyNordicRecovery).toHaveBeenCalledWith(server)
    expect(client.getRecoverySafeResumeSupported()).toBe(false)
    const secureChannels = ['8ec90001-f315-4f60-9fb8-838830daea50', '8ec90002-f315-4f60-9fb8-838830daea50']
    expect(getCharacteristic.mock.calls.map(([uuid]) => uuid)).toEqual(secureChannels)
    client.disconnect()
    vi.mocked(verifyNordicRecovery).mockResolvedValueOnce({ safeResume: true })
    await client.reconnectDevice(device)
    expect(client.getRecoverySafeResumeSupported()).toBe(true)
    expect(getCharacteristic.mock.calls.map(([uuid]) => uuid)).toEqual([...secureChannels, ...secureChannels])
    expect(useDeviceStore.getState()).toMatchObject({ connection: 'connected', firmwareVersion: 'Recovery', voltage: null })
    client.disconnect()
    getCharacteristic.mockRejectedValueOnce(new Error('missing DFU control'))
    await expect(client.reconnectDevice(device)).rejects.toThrow('missing DFU control')
    expect(client.getOtaRecoveryType()).toBeNull()
  })
  it('requires a verified OM fixed profile instead of relying only on the name/service', async () => {
    const server = { getPrimaryService: vi.fn(async (uuid: string) => {
      if (uuid === '000001bf-5f00-0000-0000-4f4d2d4f5441') return {}
      throw new DOMException('not found', 'NotFoundError')
    }) } as unknown as BluetoothRemoteGATTServer
    const device = { id: 'om-recovery', name: 'IKT_OM_63CF_OTA', gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() }, addEventListener: vi.fn(), removeEventListener: vi.fn() } as unknown as BluetoothDevice
    const client = new InkTagClient()
    await client.reconnectDevice(device)
    expect(verifyOnMicroRecovery).toHaveBeenCalledWith(server)
    expect(client.getOtaRecoveryType()).toBe('onmicro')
    client.disconnect()
    vi.mocked(verifyOnMicroRecovery).mockRejectedValueOnce(new Error('incompatible profile'))
    await expect(client.reconnectDevice(device)).rejects.toThrow('incompatible profile')
    expect(client.getOtaRecoveryType()).toBeNull()
  })

  it.each(['IKT_GR_1234_OTA', 'InkTag_GR_1234_OTA'])('directly connects verified GR Recovery %s without stale App data', async (name) => {
    const { device, server } = grRecovery(name)
    const store = useDeviceStore.getState()
    store.setIdentity({ boardId: 'old-board', firmwareVersion: 'old-version' })
    store.setCapabilities({ nfc: 'hardware', imageStore: true })
    store.setTelemetry({ voltage: 3, temperature: 20 })
    const client = new InkTagClient()
    await client.reconnectDevice(device)
    expect(verifyGoodixRecovery).toHaveBeenCalledWith(server)
    expect(client.getOtaRecoveryType()).toBe('goodix')
    expect(client.getRuntimeImageInfo()).toBeNull()
    expect(useDeviceStore.getState()).toMatchObject({ connection: 'connected',
      deviceName: name, deviceId: device.id, firmwareVersion: 'Recovery',
      boardId: '', protocolVersion: null, voltage: null, temperature: null,
      capabilities: { ota: 'goodix', imageStore: false, nfc: 'none' } })
  })

  it('does not accept a GR _OTA name without a live fixed Recovery role', async () => {
    const { device } = grRecovery()
    vi.mocked(verifyGoodixRecovery).mockRejectedValueOnce(new Error('still an application'))
    const client = new InkTagClient()
    await expect(client.reconnectDevice(device)).rejects.toThrow('still an application')
    expect(client.getOtaRecoveryType()).toBeNull()
    expect(useDeviceStore.getState().connection).not.toBe('connected')
  })

  it('rejects generic GR OTA names without probing a Recovery protocol', async () => {
    const { device } = grRecovery('IKT_OTA')
    await expect(new InkTagClient().reconnectDevice(device)).rejects.toBeInstanceOf(DOMException)
    expect(verifyGoodixRecovery).not.toHaveBeenCalled()
  })

  it('accepts the same verified browser device when its application name is cached during Recovery', async () => {
    const getPrimaryService = vi.fn(async (uuid: BluetoothServiceUUID) => {
      if (uuid === '62750001-d828-918d-fb46-b6c11c675aec') throw new DOMException('not found', 'NotFoundError')
      if (uuid === '0000fef5-0000-1000-8000-00805f9b34fb') return {} as BluetoothRemoteGATTService
      throw new DOMException('unexpected service', 'NotFoundError')
    })
    const server = { getPrimaryService } as unknown as BluetoothRemoteGATTServer
    const device = {
      id: 'same-browser-device',
      name: 'InkTag_DA_1234',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient() as unknown as {
      reconnectDevice: (device: BluetoothDevice) => Promise<void>
      getOtaRecoveryType: () => string | null
      expectedFixedRecovery: { type: 'dialog'; deviceId: string; name: string }
    }
    client.expectedFixedRecovery = {
      type: 'dialog', deviceId: device.id, name: 'InkTag_DA_1234_OTA',
    }

    await client.reconnectDevice(device)

    expect(client.getOtaRecoveryType()).toBe('dialog')
    expect(useDeviceStore.getState().connection).toBe('connected')
  })

  it('rejects the obsolete generic DA Recovery name', async () => {
    const server = {
      getPrimaryService: vi.fn().mockRejectedValue(new DOMException('not found', 'NotFoundError')),
    } as unknown as BluetoothRemoteGATTServer
    const device = {
      id: 'generic-da-recovery',
      name: 'InkTag_DA_RECOVERY',
      gatt: { connected: true, connect: vi.fn().mockResolvedValue(server), disconnect: vi.fn() },
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    } as unknown as BluetoothDevice
    const client = new InkTagClient()

    await expect(client.reconnectDevice(device)).rejects.toBeInstanceOf(DOMException)
    expect(client.getOtaRecoveryType()).toBeNull()
  })

  it('keeps DA battery telemetry without inventing a temperature reading', () => {
    const client = new InkTagClient() as unknown as {
      applyV2Status: (payload: Uint8Array) => void
    }
    const payload = new Uint8Array(16)
    payload[0] = 1
    payload[2] = 0x04
    payload[7] = 0x80
    new DataView(payload.buffer).setUint16(8, 3000, true)

    client.applyV2Status(payload)

    expect(useDeviceStore.getState().voltage).toBe(3)
    expect(useDeviceStore.getState().temperature).toBeNull()
    expect(useDeviceStore.getState().mcuTemperature).toBeNull()
    expect(useDeviceStore.getState().panelTemperature).toBeNull()
  })

  it('dismisses a prior brownout alert once valid current voltage is above 2.8 V without clearing health history', async () => {
    const client = new InkTagClient()
    const payload = new Uint8Array(56)
    payload[0] = 1; payload[1] = 1; payload[2] = 1; payload[20] = 1; payload[23] = 3
    const view = new DataView(payload.buffer)
    view.setUint32(16, 2, true)
    view.setUint16(4, 2600, true)
    ;(client as unknown as { requestV2: unknown }).requestV2 = vi.fn().mockResolvedValue({ payload })
    useDeviceStore.getState().setCapabilities({ health: true })
    useDeviceStore.getState().setIdentity({ deviceId: 'brownout-test' })
    view.setUint32(16, 0, true)
    await client.readHealth()
    view.setUint32(16, 2, true)
    await client.readHealth()
    expect(useDeviceStore.getState().crashAlert?.reason).toBe('低电压复位')
    useDeviceStore.getState().clearLogs()
    view.setUint16(4, 3300, true)
    await client.readHealth()
    expect(useDeviceStore.getState().crashAlert).toBeNull()
    expect(useDeviceStore.getState().health?.totalCrashes).toBe(2)
    expect(useDeviceStore.getState().health?.latestCrash?.reason).toBe(3)
    expect(useDeviceStore.getState().logs).toHaveLength(0)
  })

  it.each([undefined, 0, 39])('logs historical crashes once with saved count %s, while preserving later observed increments', async (baseline) => {
    useDeviceStore.getState().resetDevice()
    const client = new InkTagClient(), payload = new Uint8Array(56)
    payload[0] = 1; payload[1] = 1; payload[2] = 1; payload[20] = 1; payload[23] = 2
    const view = new DataView(payload.buffer)
    view.setUint32(16, 40, true); view.setUint16(4, 3000, true)
    const request = vi.fn().mockResolvedValue({ payload })
    ;(client as unknown as { requestV2: unknown }).requestV2 = request
    useDeviceStore.getState().setCapabilities({ health: true })
    useDeviceStore.getState().setIdentity({ deviceId: 'history-log-only' })
    const preference = baseline === undefined ? undefined : { id: 'history-log-only', lastAcknowledgedTotalCrashes: baseline } as DevicePreference
    vi.mocked(db.devicePreferences.get).mockResolvedValueOnce(preference).mockResolvedValueOnce(preference).mockResolvedValueOnce(preference)
    await client.readHealth()
    expect(useDeviceStore.getState().crashAlert).toBeNull()
    expect(useDeviceStore.getState().logs.filter(log => log.message.includes('异常复位历史记录'))).toHaveLength(1)
    await client.readHealth()
    expect(useDeviceStore.getState().logs.filter(log => log.message.includes('异常复位历史记录'))).toHaveLength(1)
    view.setUint32(16, 41, true)
    await client.readHealth()
    expect(useDeviceStore.getState().crashAlert?.newCrashes).toBe(1)
    expect(useDeviceStore.getState().health?.totalCrashes).toBe(41)
    expect(request.mock.calls.every(([op]) => op === CommandV2.GET_HEALTH)).toBe(true)
  })

  it('shows separately valid temperature sources without changing the effective value', () => {
    const client = new InkTagClient() as unknown as {
      applyV2Status: (payload: Uint8Array) => void
    }
    const payload = new Uint8Array(25)
    payload[0] = 1
    payload[2] = 0x08
    payload[7] = 25
    payload.set([0x49, 0x54, 0x54, 1, 0x02], 16)
    new DataView(payload.buffer).setInt16(23, 408, true)

    client.applyV2Status(payload)
    expect(useDeviceStore.getState().temperature).toBe(25)
    expect(useDeviceStore.getState().mcuTemperature).toBeNull()
    expect(useDeviceStore.getState().panelTemperature).toBe(25.5)

    client.applyV2Status(payload.slice(0, 16))
    expect(useDeviceStore.getState().panelTemperature).toBeNull()
  })

  it.each([CommandV2.SET_DISPLAY_MODE, CommandV2.RUN_TEST, CommandV2.DISPLAY_ACTION, CommandV2.TRANSFER_COMMIT])('reads cached telemetry once after matching display completion %i and ignores duplicates', async (opcode) => {
    const client = new InkTagClient() as unknown as { protocolV2: boolean; requestV2: ReturnType<typeof vi.fn>; waitForV2Completion: (tx: number, op: number) => Promise<Uint8Array>; handleV2Notification: (data: Uint8Array) => void }
    client.protocolV2 = true
    useDeviceStore.getState().setConnection('connected')
    const payload = Uint8Array.from('010108000000ff170000100e0000e001'.match(/../g)!, (b) => parseInt(b, 16))
    client.requestV2 = vi.fn().mockResolvedValue({ payload })
    const done = client.waitForV2Completion(23, opcode)
    const event = new Uint8Array([EventV2.COMPLETE, 0, StatusV2.OK, opcode, 23])
    client.handleV2Notification(event)
    await done
    await Promise.resolve()
    expect(client.requestV2).toHaveBeenCalledExactlyOnceWith(CommandV2.GET_STATUS, new Uint8Array(0), 0, 3000)
    expect(useDeviceStore.getState().temperature).toBe(23)
    expect(useDeviceStore.getState().panelTemperature).toBeNull()
    client.handleV2Notification(event)
    expect(client.requestV2).toHaveBeenCalledTimes(1)
  })

  it('does not request telemetry for an unsuccessful or unrelated completion', async () => {
    const client = new InkTagClient() as unknown as { protocolV2: boolean; requestV2: ReturnType<typeof vi.fn>; waitForV2Completion: (tx: number, op: number) => Promise<Uint8Array>; handleV2Notification: (data: Uint8Array) => void }
    client.protocolV2 = true
    useDeviceStore.getState().setConnection('connected')
    client.requestV2 = vi.fn()
    const done = client.waitForV2Completion(23, CommandV2.SET_DISPLAY_MODE)
    client.handleV2Notification(new Uint8Array([EventV2.COMPLETE, 0, StatusV2.OK, CommandV2.RUN_TEST, 23]))
    expect(client.requestV2).not.toHaveBeenCalled()
    const rejection = expect(done).rejects.toThrow()
    client.handleV2Notification(new Uint8Array([EventV2.COMPLETE, 0, StatusV2.INVALID_REQUEST, CommandV2.SET_DISPLAY_MODE, 23]))
    await rejection
    expect(client.requestV2).not.toHaveBeenCalled()
  })

  it('keeps confirmed display success when the single status read fails', async () => {
    const client = new InkTagClient() as unknown as { protocolV2: boolean; requestV2: ReturnType<typeof vi.fn>; waitForV2Completion: (tx: number, op: number) => Promise<Uint8Array>; handleV2Notification: (data: Uint8Array) => void }
    client.protocolV2 = true
    useDeviceStore.getState().setConnection('connected')
    client.requestV2 = vi.fn().mockRejectedValue(new Error('timeout'))
    const done = client.waitForV2Completion(23, CommandV2.RUN_TEST)
    client.handleV2Notification(new Uint8Array([EventV2.COMPLETE, 0, StatusV2.OK, CommandV2.RUN_TEST, 23]))
    await expect(done).resolves.toEqual(new Uint8Array(0))
    await Promise.resolve()
    expect(client.requestV2).toHaveBeenCalledTimes(1)
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('刷屏已完成，但状态读回失败')
  })

  it.each(['disconnect', 'generation'])('discards a late post-display status after %s', async (mode) => {
    const client = new InkTagClient() as unknown as { protocolV2: boolean; connectionGeneration: number; requestV2: ReturnType<typeof vi.fn>; refreshTelemetryAfterDisplay: (generation: number) => Promise<void> }
    client.protocolV2 = true
    useDeviceStore.getState().setConnection('connected')
    let reply!: (value: { payload: Uint8Array }) => void
    client.requestV2 = vi.fn().mockReturnValue(new Promise((resolve) => { reply = resolve }))
    const pending = client.refreshTelemetryAfterDisplay(client.connectionGeneration)
    if (mode === 'disconnect') useDeviceStore.getState().setConnection('idle')
    else client.connectionGeneration++
    const payload = new Uint8Array(16); payload[0] = 1; payload[2] = 8; payload[7] = 23
    reply({ payload })
    await pending
    expect(useDeviceStore.getState().temperature).toBeNull()
  })

  it('keeps waiting when a late completion belongs to another opcode', async () => {
    const client = new InkTagClient() as unknown as {
      waitForV2Completion: (transactionId: number, opcode: number) => Promise<Uint8Array>
      handleV2Notification: (bytes: Uint8Array) => void
    }
    const transaction = 23
    let settled = false
    const completion = client.waitForV2Completion(transaction, CommandV2.SET_DISPLAY_MODE)
    void completion.then(() => { settled = true })

    client.handleV2Notification(new Uint8Array([
      EventV2.COMPLETE, 0, StatusV2.OK,
      CommandV2.TRANSFER_COMMIT, transaction,
    ]))
    await Promise.resolve()
    expect(settled).toBe(false)
    expect(useDeviceStore.getState().logs.at(-1)?.message).toContain('忽略不匹配')

    client.handleV2Notification(new Uint8Array([
      EventV2.COMPLETE, 0, StatusV2.OK,
      CommandV2.SET_DISPLAY_MODE, transaction, 0xa5,
    ]))
    await expect(completion).resolves.toEqual(new Uint8Array([0xa5]))
  })
})
