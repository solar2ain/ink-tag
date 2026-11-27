import { afterEach, beforeEach, expect, it, vi } from 'vitest'
vi.mock('../data/db', () => ({ db: { devicePreferences: { get: vi.fn().mockResolvedValue(undefined) } } }))
import source from '../ota-drivers/telink_ota.js?raw'
import { inktagClient, ProtocolV2Error } from './client'
import { enterFixedRecovery, exitTelinkRecovery, inspectTelinkRecovery, transferOta } from './ota'
import { useDeviceStore } from '../store/device'
import { CommandV2, PROTOCOL_V2_VERSION, StatusV2 } from './generatedCommandsV2'
import { telinkNativeTransportFixture } from './testFixtures/telinkNative'

beforeEach(() => { vi.useFakeTimers(); useDeviceStore.getState().resetDevice() })
afterEach(() => { inktagClient.disconnect(); vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers(); useDeviceStore.getState().resetDevice() })

function stack(recovery = false) {
  const scope: any = {}; new Function('window', source)(scope)
  const ota = scope.TelinkOta, raw = inktagClient as any
  const { firmware, declaredSize, compatibility: compat, entry } = telinkNativeTransportFixture()
  const status = new Uint8Array([0,0,0,0,0,0,0,0,0x49,0x54,1,14,0,0])
  let role = recovery ? 'recovery' : 'app'
  const controls: number[][] = []
  const data: any = Object.assign(new EventTarget(), { properties: { write: true, notify: true },
    readValue: vi.fn(async () => new DataView(status.slice().buffer)),
    startNotifications: vi.fn(async () => data), stopNotifications: vi.fn(async () => data),
    writeValueWithResponse: vi.fn(async (bytes: Uint8Array) => {
      controls.push(Array.from(bytes.slice(0,2)))
      if (bytes[0] === 1 && bytes[1] === 255) status[0] = 1
      else if (bytes[0] === 2 && bytes[1] === 255) { status[0] = 2; role = 'app' }
      else if (bytes[0] === 3 && bytes[1] === 255) { status[0] = 8; role = 'app' }
      else new DataView(status.buffer).setUint16(1,(bytes[0] | bytes[1]<<8)+(bytes.length-4)/16,true)
      const event = new Event('characteristicvaluechanged')
      Object.defineProperty(event,'target',{value:{value:new DataView(status.slice().buffer)}})
      data.dispatchEvent(event)
    }) })
  ;(data as any).writeValueWithoutResponse = data.writeValueWithResponse
  const recoveryService = { getCharacteristic: vi.fn(async (uuid: string) => {
    if (uuid.endsWith('2b13')) return { properties: { read: true }, readValue: async () => new DataView(compat.buffer) }
    if (uuid.endsWith('2b14')) return { properties: { read: true }, readValue: async () => new DataView(entry.buffer) }
    return data
  }) }
  const product = Object.assign(new EventTarget(), {
    writeValueWithResponse: vi.fn(async (bytes: Uint8Array) => {
      if (bytes[0] === CommandV2.SYSTEM_ACTION) { role = 'recovery'; status[0] = 0 }
      queueMicrotask(() => { const response = new Uint8Array([bytes[0],bytes[1],StatusV2.OK]); const e = new Event('characteristicvaluechanged'); Object.defineProperty(e,'target',{value:{value:new DataView(response.buffer)}}); product.dispatchEvent(e) })
    }), startNotifications: vi.fn(async () => product) })
  const version = { readValue: async () => new DataView(new Uint8Array([PROTOCOL_V2_VERSION,0,2,0]).buffer) }
  const appService = { getCharacteristic: async (uuid: string) => uuid.includes('0002') ? product : version }
  const device: any = Object.assign(new EventTarget(), { id: 'exact-TL-A3FF', name: recovery ? 'IKT_TL_A3FF_OTA' : 'IKT_TL_A3FF' })
  const server: any = { device, connected: true,
    connect: vi.fn(async () => { server.connected = true; return server }),
    disconnect: vi.fn(() => { server.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')) }),
    getPrimaryService: vi.fn(async (uuid: string) => {
      if (uuid.startsWith('62750001')) { if(role!=='app')throw new DOMException('no App','NotFoundError');return appService }
      return recoveryService
    }) }
  device.gatt = server
  vi.stubGlobal('window',{TelinkOta:ota,setTimeout,clearTimeout})
  vi.stubGlobal('document',{createElement:()=>({remove(){}}),head:{appendChild(){}}})
  const identity = () => {
    raw.runtimeImageInfo = { version:1,appBytes:declaredSize,checksum:0 }
    useDeviceStore.getState().setIdentity({boardId:'tlsr8359_hanshow',bleAddress:'C7:1D:C9:64:A3:FF'})
    useDeviceStore.getState().setCapabilities({ota:'telink'})
  }
  vi.spyOn(raw,'initializeV2Core').mockImplementation(async()=>identity())
  vi.spyOn(raw,'initializeV2Details').mockResolvedValue(undefined)
  vi.spyOn(raw,'getDisplaySettings').mockResolvedValue(undefined)
  vi.spyOn(raw,'queryV2ApplicationIdentity').mockResolvedValue({size:declaredSize,checksum:0})
  const sync=vi.spyOn(inktagClient,'syncTime').mockResolvedValue(undefined)
  Object.assign(raw,{device,server,protocolV2:!recovery,characteristic:recovery?null:product,
    otaRecoveryType:recovery?'telink':null,verifiedRecoveryName:recovery?'IKT_TL_A3FF_OTA':'',expectedFixedRecovery:null,
    disconnectRequested:false,passwordBootstrap:Promise.resolve(),v2Capabilities:0,passwordDetailsGeneration:-1,writeQueue:Promise.resolve()})
  if(!recovery)product.addEventListener('characteristicvaluechanged',raw.onNotification)
  device.addEventListener('gattserverdisconnected',raw.onDisconnected)
  useDeviceStore.setState({connection:'connected',deviceId:device.id,deviceName:device.name,protocolVersion:PROTOCOL_V2_VERSION})
  identity()
  return { ota, raw, device, server, controls, data, product, sync,
    parsed:{type:'telink' as const,payload:{firmware,declaredSize,crc32:0},summary:'synthetic host transport fixture, not signed artifact'} }
}

it.each([false,true])('shared TL route completes App attestation and exactly one sync; live Recovery=%s', async recovery=>{
  const f=stack(recovery), progress=vi.fn()
  if(recovery)await f.ota.queryRecovery(f.server)
  const acquire=vi.spyOn(f.ota,'acquireRecoverySession'), transfer=vi.spyOn(f.ota,'transferRecoverySession')
  const run=transferOta(f.parsed,progress), result=expect(run).resolves.toBeUndefined()
  await vi.runAllTimersAsync(); await result
  expect(acquire).toHaveBeenCalledOnce();expect(transfer).toHaveBeenCalledOnce()
  expect(f.controls.filter(x=>x[0]===1&&x[1]===255)).toHaveLength(1)
  expect(f.controls.filter(x=>x[0]===2&&x[1]===255)).toHaveLength(1)
  expect(f.product.writeValueWithResponse.mock.calls.filter(([p])=>p[0]===CommandV2.SYSTEM_ACTION)).toHaveLength(recovery?0:1)
  expect(f.sync).toHaveBeenCalledOnce()
  expect(progress.mock.lastCall?.[0].message).toContain('应用已验身、重连并校时')
})

it('accepts a new TLSR update after leaving Recovery and verifying App on the same GATT object', async () => {
  const f = stack()
  const enter = enterFixedRecovery(f.device.id)
  await vi.runAllTimersAsync()
  await enter
  expect(inktagClient.getOtaRecoveryType()).toBe('telink')

  const exit = exitTelinkRecovery({ syncTime: false })
  await vi.runAllTimersAsync()
  await exit
  expect(inktagClient.getGattServer()).toBe(f.server)
  expect(inktagClient.getOtaRecoveryType()).toBeNull()

  const upgrade = transferOta(f.parsed, vi.fn())
  const completed = expect(upgrade).resolves.toBeUndefined()
  await vi.runAllTimersAsync()
  await completed
  expect(f.controls.filter(([kind, tag]) => tag === 255 && kind === 3)).toHaveLength(1)
  expect(f.product.writeValueWithResponse.mock.calls.filter(([packet]) => packet[0] === CommandV2.SYSTEM_ACTION)).toHaveLength(2)
})

it('reads status repeatedly over the verified live TLSR Recovery without reconnecting', async () => {
  const f = stack(true)
  await f.ota.queryRecovery(f.server)
  f.server.getPrimaryService.mockClear()
  await expect(inspectTelinkRecovery(vi.fn())).resolves.toContain('Recovery 状态 0')
  await expect(inspectTelinkRecovery(vi.fn())).resolves.toContain('Recovery 状态 0')
  expect(f.server.connected).toBe(true)
  expect(f.server.disconnect).not.toHaveBeenCalled()
  expect(f.server.connect).not.toHaveBeenCalled()
  expect(f.server.getPrimaryService).not.toHaveBeenCalled()
  expect(f.controls).toHaveLength(0)
  expect(inktagClient.getOtaRecoveryType()).toBe('telink')
})

it('reselects the exact Recovery after the bounded discovery attempts drop and continues without replaying entry', async () => {
  const f = stack()
  const ordinaryDiscovery = f.server.getPrimaryService.getMockImplementation()
  const loseLink = async () => {
    f.server.connected = false
    throw new DOMException('GATT Server is disconnected. Cannot retrieve services.', 'NetworkError')
  }
  f.server.getPrimaryService.mockImplementation(loseLink)
  const pending = expect(transferOta(f.parsed, vi.fn())).rejects.toMatchObject({ code: 'RECOVERY_SELECT_REQUIRED' })
  await vi.runAllTimersAsync()
  await pending
  expect(f.controls).toHaveLength(0)
  expect(f.server.connect).toHaveBeenCalledTimes(9)

  const handoff = inktagClient.getExpectedRecoveryIdentity()!
  const requestDevice = vi.fn().mockResolvedValueOnce({ ...f.device, id: 'different-device' }).mockResolvedValueOnce(f.device)
  vi.stubGlobal('navigator', { bluetooth: { requestDevice } })
  await expect(inktagClient.selectRecoveryForOta(handoff)).rejects.toThrow('所选设备与原设备不一致')
  expect(f.server.connect).toHaveBeenCalledTimes(9)
  expect(f.controls).toHaveLength(0)
  f.server.getPrimaryService.mockImplementation(ordinaryDiscovery)

  await inktagClient.selectRecoveryForOta(handoff)
  expect(requestDevice).toHaveBeenLastCalledWith({
    filters: [{ name: 'IKT_TL_A3FF_OTA' }, { name: 'InkTag_TL_A3FF_OTA' }],
    optionalServices: expect.arrayContaining(['00010203-0405-0607-0809-0a0b0c0d1912']),
  })
  expect(inktagClient.getOtaRecoveryType()).toBe('telink')
  const upgrade = transferOta(f.parsed, vi.fn())
  await vi.runAllTimersAsync()
  await upgrade
  expect(f.product.writeValueWithResponse.mock.calls.filter(([p]) => p[0] === CommandV2.SYSTEM_ACTION)).toHaveLength(1)
  expect(f.controls.filter(([cmd, tag]) => cmd === 1 && tag === 255)).toHaveLength(1)
})

it('refuses a peer without terminal notifications before firmware controls', async () => {
  const f=stack(true)
  f.data.properties.notify=false
  const run=transferOta(f.parsed,vi.fn()), rejected=expect(run).rejects.toThrow(/终态通知/)
  await vi.runAllTimersAsync();await rejected
  expect(f.controls).toHaveLength(0);expect(f.sync).not.toHaveBeenCalled()
})

it('does not accept a readable COMPLETE in place of the current terminal notification', async () => {
  const f=stack(true),dispatch=f.data.dispatchEvent.bind(f.data)
  vi.spyOn(f.data,'dispatchEvent').mockImplementation((...args: unknown[]) => {
    const event=args[0] as Event & { target: { value: DataView } }
    return event.target.value.getUint8(0)===2 ? true : dispatch(event)
  })
  const run=transferOta(f.parsed,vi.fn()), rejected=expect(run).rejects.toMatchObject({code:'TLSR_FINALIZATION_UNKNOWN'})
  await vi.runAllTimersAsync();await rejected
  expect(f.controls.filter(x=>x[0]===2&&x[1]===255)).toHaveLength(1)
  expect(f.sync).not.toHaveBeenCalled()
})

it('unknown App entry remains sealed on the same server, without acquiring or replaying entry',async()=>{
  const f=stack()
  const enter=vi.spyOn(inktagClient,'enterOtaRecovery').mockImplementation(async () => {
    vi.spyOn(inktagClient, 'getExpectedRecoveryIdentity').mockReturnValue({
      deviceId: inktagClient.getDevice()!.id, recoveryName: 'IKT_TL_A3FF_OTA',
    })
    throw new Error('unknown native entry')
  })
  const acquire=vi.spyOn(f.ota,'acquireRecoverySession')
  await expect(transferOta(f.parsed,vi.fn())).rejects.toThrow('unknown native entry')
  await expect(transferOta(f.parsed,vi.fn())).rejects.toThrow('进入 Recovery 结果尚未确认')
  expect(enter).toHaveBeenCalledOnce(); expect(acquire).not.toHaveBeenCalled();expect(f.sync).not.toHaveBeenCalled()
})

it('an explicit entry refusal keeps ordinary AUTH and does not create a transfer session',async()=>{
  const f=stack(),enter=vi.spyOn(inktagClient,'enterOtaRecovery').mockRejectedValue(new ProtocolV2Error(StatusV2.LOCKED))
  const acquire=vi.spyOn(f.ota,'acquireRecoverySession')
  await expect(transferOta(f.parsed,vi.fn())).rejects.toMatchObject({status:StatusV2.LOCKED})
  expect(enter).toHaveBeenCalledOnce();expect(acquire).not.toHaveBeenCalled();expect(f.controls).toHaveLength(0)
})
