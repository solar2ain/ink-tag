import { INKTAG_SERVICE_UUID } from './framing'

export type DiscoveredDevice = {
  deviceId: string
  name?: string
  localName?: string
  RSSI?: number
  advertisServiceUUIDs?: string[]
}

function normalizedName(device: DiscoveredDevice): string {
  return (device.localName || device.name || '').trim()
}

export function isInkTagDevice(device: DiscoveredDevice): boolean {
  if (normalizedName(device).toLowerCase().startsWith('inktag_')) return true
  return (device.advertisServiceUUIDs || []).some((uuid) => uuid.toLowerCase() === INKTAG_SERVICE_UUID)
}

export function rankDiscoveredDevices<T extends DiscoveredDevice>(devices: readonly T[]): T[] {
  return [...devices].sort((left, right) => {
    const family = Number(isInkTagDevice(right)) - Number(isInkTagDevice(left))
    if (family) return family
    const signal = (right.RSSI ?? -127) - (left.RSSI ?? -127)
    if (signal) return signal
    return normalizedName(left).localeCompare(normalizedName(right), 'zh-CN')
  })
}
