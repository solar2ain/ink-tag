import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import { InkTagClient, ProtocolV2Error } from './client'
import { CapabilityV2, CommandV2, SettingsSectionV2, StatusV2 } from './generatedCommandsV2'
import { useDeviceStore } from '../store/device'
import { encodeScreenConfig, makeScreenConfig } from './screen'

describe('independent V2 connection details, simulated transport', () => {
  beforeEach(() => {
    Object.defineProperty(globalThis, 'window', { configurable: true, value: globalThis })
    useDeviceStore.getState().resetDevice()
  })
  afterEach(() => vi.restoreAllMocks())

  it.each(['gr5331_zkel', 'om6626_zkcxxb', 'tlsr8359_hanshow', 'nrf52832_priceetag'])(
    'isolates malformed Find on %s and still reads independent screen, pins and telemetry', async boardId => {
      useDeviceStore.setState({ boardId })
      const client = new InkTagClient()
      const internal = client as unknown as {
        initializeV2Details: (mask: number) => Promise<void>
        requestV2: ReturnType<typeof vi.fn>
      }
      const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0, width: 400, height: 300,
        visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
      internal.requestV2 = vi.fn(async opcode => {
        if (opcode === CommandV2.GET_FIND_CAPABILITIES) return { payload: new Uint8Array(16) }
        if (opcode === CommandV2.GET_SCREEN_PROFILE) return { payload: encodeScreenConfig(screen) }
        if (opcode === CommandV2.GET_TIME) return { payload: new Uint8Array(7) }
        throw new Error(`unexpected opcode ${opcode}`)
      })
      const pins = vi.spyOn(client, 'getRuntimePinConfig').mockResolvedValue(null as never)
      vi.spyOn(client, 'getAuxIoConfiguration').mockRejectedValue(new Error('unavailable auxiliary IO'))
      const telemetry = vi.spyOn(client, 'requestTelemetry').mockResolvedValue(undefined)
      await internal.initializeV2Details(CapabilityV2.EXPERIMENTAL_FIND | CapabilityV2.PIN_CONFIG)
      expect(useDeviceStore.getState().findCapabilities).toBeNull()
      expect(useDeviceStore.getState().screen).toEqual(screen)
      expect(pins).toHaveBeenCalledOnce()
      expect(telemetry).toHaveBeenCalledOnce()
      expect(internal.requestV2.mock.calls.map(([opcode]) => opcode)).not.toContain(CommandV2.GET_FIND_CONFIG)
      expect(useDeviceStore.getState().logs.some(log => log.level === 'warning' && log.message.includes('Find'))).toBe(true)
    },
  )

  it.each(['screen', 'time', 'telemetry', 'activation', 'resources'])('keeps later independent reads after a %s failure', async failure => {
    const client = new InkTagClient()
    const internal = client as unknown as { initializeV2Details: (mask: number) => Promise<void>; requestV2: ReturnType<typeof vi.fn> }
    const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0, width: 400, height: 300,
      visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
    internal.requestV2 = vi.fn(async opcode => {
      if (opcode === CommandV2.GET_SCREEN_PROFILE) {
        if (failure === 'screen') throw new ProtocolV2Error(StatusV2.INTERNAL_ERROR)
        return { payload: encodeScreenConfig(screen) }
      }
      if (opcode === CommandV2.GET_TIME) return { payload: new Uint8Array(failure === 'time' ? 0 : 7) }
      throw new Error(`unexpected opcode ${opcode}`)
    })
    const telemetry = vi.spyOn(client, 'requestTelemetry').mockImplementation(async () => {
      if (failure === 'telemetry') throw new Error('bad status')
    })
    const activation = vi.spyOn(client, 'readActivationStatus').mockImplementation(async () => {
      if (failure === 'activation') throw new Error('bad activation')
      return { phase: 'inactive', edition: 'base' }
    })
    const slots = vi.spyOn(client, 'listStoredImages').mockImplementation(async () => {
      if (failure === 'resources') throw new Error('bad resources')
    })
    await internal.initializeV2Details(CapabilityV2.ACTIVATION | CapabilityV2.RESOURCES)
    expect(useDeviceStore.getState().screen).toEqual(failure === 'screen' ? null : screen)
    expect(telemetry).toHaveBeenCalledOnce(); expect(activation).toHaveBeenCalledOnce(); expect(slots).toHaveBeenCalledOnce()
    expect(useDeviceStore.getState().logs.some(log => log.level === 'warning')).toBe(true)
    expect(internal.requestV2.mock.calls.map(([op]) => op)).not.toContain(CommandV2.SET_SCREEN_PROFILE)
  })

  it.each([[0, 'basic'], [1, 'pro'], [null, null]] as const)(
    'uses the wire scheme %s to identify the firmware build when the activation bit is absent', async (scheme, edition) => {
      useDeviceStore.setState({ boardId: 'gr5331_zkel' })
      const client = new InkTagClient()
      const internal = client as unknown as { initializeV2Details: (mask: number) => Promise<void>; requestV2: ReturnType<typeof vi.fn> }
      const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0, width: 400, height: 300,
        visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
      internal.requestV2 = vi.fn(async opcode => {
        if (opcode === CommandV2.GET_SCREEN_PROFILE) return { payload: encodeScreenConfig(screen) }
        if (opcode === CommandV2.GET_TIME) return { payload: new Uint8Array(7) }
        if (opcode === CommandV2.GET_ACTIVATION_STATUS) {
          if (scheme === null) throw new ProtocolV2Error(StatusV2.UNSUPPORTED)
          const payload = new Uint8Array(24)
          payload.set([2, 1, scheme])
          return { payload }
        }
        throw new Error(`unexpected opcode ${opcode}`)
      })
      vi.spyOn(client, 'requestTelemetry').mockResolvedValue(undefined)
      await internal.initializeV2Details(0)
      expect(useDeviceStore.getState().firmwareBuildEdition).toBe(edition)
      expect(useDeviceStore.getState().activation.edition).toBe('base')
    },
  )

  it('stops the detail chain after a connection generation changes instead of clearing a new device', async () => {
    const client = new InkTagClient()
    const internal = client as unknown as { connectionGeneration: number; initializeV2Details: (mask: number) => Promise<void>; requestV2: ReturnType<typeof vi.fn> }
    const screen = makeScreenConfig({ driver: 0x13, color: 2, transform: 0, width: 400, height: 300,
      visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300 })
    useDeviceStore.getState().setScreen(screen)
    internal.requestV2 = vi.fn(async () => { internal.connectionGeneration++; return { payload: new Uint8Array() } })
    await expect(internal.initializeV2Details(0)).rejects.toThrow('连接已改变')
    expect(internal.requestV2).toHaveBeenCalledOnce()
    expect(useDeviceStore.getState().screen).toEqual(screen)
  })

  it.each([true, false])('rejects non-ASCII new identity values before any write (V2=%s)', async (v2) => {
    const client = new InkTagClient()
    const internal = client as unknown as {
      protocolV2: boolean; requestV2: ReturnType<typeof vi.fn>; write: ReturnType<typeof vi.fn>
      waitForSettingsReply: ReturnType<typeof vi.fn>
    }
    internal.protocolV2 = v2
    internal.requestV2 = vi.fn()
    internal.write = vi.fn()
    internal.waitForSettingsReply = vi.fn()
    const store = useDeviceStore.getState()
    store.setSettingsCapabilities({ ...store.settingsCapabilities,
      bluetoothName: true, bluetoothNameMaxBytes: 19, deviceLabel: true, deviceLabelMaxBytes: 24 })
    await expect(client.setDeviceLabel('客厅')).rejects.toThrow('只支持')
    await expect(client.setBluetoothName('IKT_客厅')).rejects.toThrow('只支持')
    expect(internal.requestV2).not.toHaveBeenCalled()
    expect(internal.write).not.toHaveBeenCalled()
    expect(internal.waitForSettingsReply).not.toHaveBeenCalled()
  })

  it('preserves unchanged device-reported UTF-8 fields in the combined V2 identity frame', async () => {
    const client = new InkTagClient()
    const internal = client as unknown as { protocolV2: boolean; requestV2: ReturnType<typeof vi.fn> }
    internal.protocolV2 = true
    internal.requestV2 = vi.fn().mockResolvedValue({})
    const store = useDeviceStore.getState()
    store.setSettingsCapabilities({ ...store.settingsCapabilities,
      bluetoothName: true, bluetoothNameMaxBytes: 19, deviceLabel: true, deviceLabelMaxBytes: 24 })
    store.setIdentity({ deviceName: 'InkTag_客厅', localAlias: '旧名称' })
    await client.setDeviceLabel('Desk')
    const first = internal.requestV2.mock.calls[0]
    expect(first[0]).toBe(CommandV2.SET_SETTINGS)
    const oldName = new TextEncoder().encode('InkTag_客厅')
    expect(Array.from(first[1] as Uint8Array)).toEqual([
      SettingsSectionV2.IDENTITY, 1, oldName.length, 4, ...oldName, 68, 101, 115, 107,
    ])
    store.setIdentity({ localAlias: '旧名称' })
    await client.setBluetoothName('IKT_Desk')
    const oldLabel = new TextEncoder().encode('旧名称')
    expect(Array.from(internal.requestV2.mock.calls[1][1] as Uint8Array)).toEqual([
      SettingsSectionV2.IDENTITY, 1, 8, oldLabel.length, ...new TextEncoder().encode('IKT_Desk'), ...oldLabel,
    ])
  })

  it.each([false, true])('keeps the advertised GAP name when label is saved or cleared (writable=%s)', async writable => {
    const client = new InkTagClient()
    const internal = client as unknown as { requestV2: ReturnType<typeof vi.fn> }
    internal.requestV2 = vi.fn().mockResolvedValue({})
    const store = useDeviceStore.getState()
    store.setSettingsCapabilities({ ...store.settingsCapabilities,
      bluetoothName: writable, bluetoothNameMaxBytes: writable ? 19 : 0,
      deviceLabel: true, deviceLabelMaxBytes: 24 })
    store.setIdentity({ deviceName: 'IKT_NRF_37DE', localAlias: 'Old' })
    for (const [index, label] of ['NRF11_TEST', ''].entries()) {
      await client.setDeviceLabel(label)
      const name = new TextEncoder().encode(writable ? 'IKT_NRF_37DE' : '')
      expect(internal.requestV2.mock.calls[index]).toEqual([CommandV2.SET_SETTINGS,
        new Uint8Array([SettingsSectionV2.IDENTITY, 1, name.length, label.length,
          ...name, ...new TextEncoder().encode(label)])])
      expect(useDeviceStore.getState().deviceName).toBe('IKT_NRF_37DE')
      expect(useDeviceStore.getState().localAlias).toBe(label)
    }
    expect(internal.requestV2).toHaveBeenCalledTimes(2)
  })

  it.each(['previous-page', 'unknown-action', 'request-failed'] as const)(
    'reads screen and telemetry when input is %s', async (mode) => {
      const device = new InkTagClient()
      const internal = device as unknown as {
        initializeV2Details: (mask: number) => Promise<void>
        requestV2: ReturnType<typeof vi.fn>
      }
      const screen = makeScreenConfig({
        driver: 0x13, color: 2, transform: 0, width: 400, height: 300,
        visibleX: 0, visibleY: 0, visibleWidth: 400, visibleHeight: 300,
      })
      internal.requestV2 = vi.fn().mockImplementation(async (opcode: number) => {
        if (opcode === CommandV2.GET_INPUT_CONFIG) {
          if (mode === 'request-failed') throw new Error('input timeout')
          return { payload: new Uint8Array([1, 7, 0xff, 6, 1, 0, mode === 'unknown-action' ? 11 : 10, 0, 9, 0]) }
        }
        if (opcode === CommandV2.GET_SCREEN_PROFILE) return { payload: encodeScreenConfig(screen) }
        if (opcode === CommandV2.GET_TIME) return { payload: new Uint8Array(7) }
        throw new Error(`unexpected opcode ${opcode}`)
      })
      const telemetry = vi.spyOn(device, 'requestTelemetry').mockResolvedValue(undefined)
      await internal.initializeV2Details(CapabilityV2.INPUT)
      expect(useDeviceStore.getState().screen).toEqual(screen)
      expect(telemetry).toHaveBeenCalledTimes(1)
      expect(internal.requestV2.mock.calls.map(([opcode]) => opcode)).toEqual([
        CommandV2.GET_INPUT_CONFIG, CommandV2.GET_SCREEN_PROFILE, CommandV2.GET_TIME,
      ])
      if (mode === 'previous-page') {
        expect(useDeviceStore.getState().settingsCapabilities.buttonActionMask).toBe(0x6ff)
        expect(useDeviceStore.getState().reportedButtonActions?.double.action).toBe('previousPage')
      } else {
        expect(useDeviceStore.getState().settingsCapabilities.buttonActionMask).toBe(0)
        expect(useDeviceStore.getState().logs.some((log) => log.message.includes('按键扩展信息不可用'))).toBe(true)
      }
    },
  )
})
