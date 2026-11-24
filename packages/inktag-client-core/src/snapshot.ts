import { CapabilityV2, CommandV2 } from './generated/commands-v2'
import {
  decodeCapabilitiesV2, decodeDeviceInfoV2, decodeHealthV2, decodeHelloV2,
  decodeScreenProfileV2, decodeStatusV2, decodeTimeV2,
  type CapabilitiesV2, type DeviceHealthV2, type DeviceInfoV2, type DeviceTimeV2,
  type HelloV2, type ScreenProfileV2, type StatusSnapshotV2,
} from './decoders'

export type V2Requester = (opcode: number, payload?: Uint8Array) => Promise<Uint8Array>

export type ReadOnlySnapshotV2 = {
  hello: HelloV2
  capabilities: CapabilitiesV2
  device: DeviceInfoV2
  status: StatusSnapshotV2
  time: DeviceTimeV2
  screen: ScreenProfileV2
  health: DeviceHealthV2 | null
}

function requireValue<T>(value: T | null, label: string): T {
  if (value === null) throw new Error(`device returned invalid ${label}`)
  return value
}

export async function readOnlySnapshotV2(request: V2Requester): Promise<ReadOnlySnapshotV2> {
  const hello = requireValue(decodeHelloV2(await request(CommandV2.HELLO)), 'HELLO response')
  const capabilities = requireValue(
    decodeCapabilitiesV2(await request(CommandV2.GET_CAPABILITIES)), 'capabilities response',
  )
  const deviceBytes = await request(CommandV2.GET_DEVICE_INFO)
  const statusBytes = await request(CommandV2.GET_STATUS)
  const timeBytes = await request(CommandV2.GET_TIME)
  const screenBytes = await request(CommandV2.GET_SCREEN_PROFILE)
  const health = (capabilities.mask & CapabilityV2.HEALTH) !== 0
    ? requireValue(decodeHealthV2(await request(CommandV2.GET_HEALTH, Uint8Array.of(0))), 'health response')
    : null
  return {
    hello, capabilities,
    device: requireValue(decodeDeviceInfoV2(deviceBytes), 'device information response'),
    status: requireValue(decodeStatusV2(statusBytes), 'status response'),
    time: requireValue(decodeTimeV2(timeBytes), 'time response'),
    screen: requireValue(decodeScreenProfileV2(screenBytes), 'screen profile response'),
    health,
  }
}
