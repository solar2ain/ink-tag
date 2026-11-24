export const INKTAG_DEVICE_PREFIX = 'IKT_'
export const INKTAG_LEGACY_DEVICE_PREFIX = 'InkTag_'
export const INKTAG_DEVICE_PREFIXES = [INKTAG_DEVICE_PREFIX, INKTAG_LEGACY_DEVICE_PREFIX] as const

export type BluetoothDiscoveryOptions = {
  onlyInkTag?: boolean
  namePrefix?: string
  preferredName?: string
}

export function buildBluetoothRequestOptions(
  optionalServices: BluetoothServiceUUID[],
  discovery: BluetoothDiscoveryOptions = {},
): RequestDeviceOptions {
  const preferredName = discovery.preferredName?.trim()
  if (preferredName) {
    return {
      filters: [
        { name: preferredName },
        ...(discovery.onlyInkTag ? INKTAG_DEVICE_PREFIXES.map((namePrefix) => ({ namePrefix })) : []),
      ],
      optionalServices,
    }
  }
  const namePrefix = discovery.namePrefix?.trim()
  if (namePrefix) {
    const knownPrefix = INKTAG_DEVICE_PREFIXES.some((prefix) => namePrefix.startsWith(prefix))
    const requestedPrefixes = discovery.onlyInkTag && !knownPrefix
      ? INKTAG_DEVICE_PREFIXES.map((prefix) => `${prefix}${namePrefix}`)
      : [namePrefix]
    return {
      filters: requestedPrefixes.map((requestedPrefix) => ({ namePrefix: requestedPrefix })),
      optionalServices,
    }
  }
  if (discovery.onlyInkTag) {
    return { filters: INKTAG_DEVICE_PREFIXES.map((namePrefix) => ({ namePrefix })), optionalServices }
  }
  return { acceptAllDevices: true, optionalServices }
}

export function isKnownInkTagName(name: string) {
  return INKTAG_DEVICE_PREFIXES.some((prefix) => name.startsWith(prefix))
}

export function compareInkTagDeviceNames(a: string | null | undefined, b: string | null | undefined) {
  return Number(isKnownInkTagName(b || '')) - Number(isKnownInkTagName(a || ''))
}
