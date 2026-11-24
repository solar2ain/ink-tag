import type { FindIdentity } from './client'

export interface ConnectedFindDeviceIdentity {
  activationId?: string
  bleAddress?: string
  browserDeviceId?: string
}

export interface FindIdentityGroup {
  id: string
  deviceId: string
  identities: FindIdentity[]
  current: boolean
}

function normalizedName(value: string) {
  return value.trim().toLocaleLowerCase()
}

export function compactFindBluetoothName(value: string) {
  return value.trim().replace(/^(?:IKT_|InkTag_)/i, '')
}

export function compactFindFingerprint(value: string) {
  return value.length > 12 ? `${value.slice(0, 8)}…${value.slice(-4)}` : value
}

export function findIdentityDisplayName(identity: Pick<FindIdentity, 'findNote' | 'deviceName' | 'bluetoothName' | 'keyFingerprint'>) {
  return identity.findNote.trim()
    || identity.deviceName.trim()
    || compactFindBluetoothName(identity.bluetoothName)
    || `Find ${identity.keyFingerprint.slice(0, 4).toUpperCase()}`
}

export function findIdentityDeviceName(identity: Pick<FindIdentity, 'findNote' | 'deviceName' | 'bluetoothName' | 'keyFingerprint'>) {
  return identity.deviceName.trim()
    || compactFindBluetoothName(identity.bluetoothName)
    || identity.findNote.trim()
    || `Find ${identity.keyFingerprint.slice(0, 4).toUpperCase()}`
}

export function findDeviceDisplayName(identities: FindIdentity[]) {
  const note = identities.find((identity) => identity.findNote.trim())?.findNote.trim()
  if (note) return note
  const deviceName = identities.find((identity) => identity.deviceName.trim())?.deviceName.trim()
  if (deviceName) return deviceName
  const bluetoothName = identities.find((identity) => identity.bluetoothName.trim())?.bluetoothName || ''
  if (bluetoothName) return compactFindBluetoothName(bluetoothName)
  return '未命名设备'
}

export function distinctFindIdentityNote(identity: FindIdentity, deviceDisplayName: string) {
  const note = identity.findNote.trim()
  return note && normalizedName(note) !== normalizedName(deviceDisplayName) ? note : ''
}

export function findIdentityUsage(identities: FindIdentity[], fingerprint: string): 'current' | 'history' {
  const newest = [...identities].sort(newestIdentityFirst)[0]
  return newest?.keyFingerprint === fingerprint ? 'current' : 'history'
}

export function stableFindDeviceId(device: ConnectedFindDeviceIdentity) {
  const activationId = device.activationId?.trim().toUpperCase()
  if (activationId && /^(?!0{16})[0-9A-F]{16}$/.test(activationId)) return `activation:${activationId}`
  const bleAddress = device.bleAddress?.trim().toUpperCase()
  if (bleAddress) return `ble:${bleAddress}`
  const browserDeviceId = device.browserDeviceId?.trim()
  return browserDeviceId ? `browser:${browserDeviceId}` : ''
}

function identityTimestamp(identity: FindIdentity) {
  const timestamp = Date.parse(identity.createdAt)
  return Number.isFinite(timestamp) ? timestamp : 0
}
function newestIdentityFirst(left: FindIdentity, right: FindIdentity) {
  return identityTimestamp(right) - identityTimestamp(left) || right.keyFingerprint.localeCompare(left.keyFingerprint)
}
const sameDevice = (left: string, right: string) => left.toLowerCase() === right.toLowerCase()

export function identitiesForDevice(identities: FindIdentity[], deviceId: string, advertisementKey = '') {
  if (!deviceId && !advertisementKey) return []
  return identities
    .filter((identity) => (deviceId && sameDevice(identity.deviceId, deviceId)) || (advertisementKey && identity.advertisementKey === advertisementKey))
    .sort(newestIdentityFirst)
}

export function selectedFindIdentity(identities: FindIdentity[]) {
  return identities.find((identity) => identity.selected)
}

export function findIdentityBelongsToDevice(identity: FindIdentity | undefined, deviceId: string, advertisementKey = '') {
  if (!identity) return false
  return Boolean(
    (deviceId && sameDevice(identity.deviceId, deviceId)) ||
    (advertisementKey && identity.advertisementKey === advertisementKey),
  )
}

export function groupFindIdentities(identities: FindIdentity[], currentDeviceId = '', currentAdvertisementKey = '') {
  const groups = new Map<string, FindIdentityGroup>()
  for (const identity of identities) {
    const current = Boolean(
      (currentDeviceId && sameDevice(identity.deviceId, currentDeviceId)) ||
      (currentAdvertisementKey && identity.advertisementKey === currentAdvertisementKey),
    )
    const id = current
      ? currentDeviceId.toLowerCase() || `key:${identity.keyFingerprint}`
      : identity.deviceId.toLowerCase() || `key:${identity.keyFingerprint}`
    const group = groups.get(id) || { id, deviceId: identity.deviceId, identities: [], current }
    group.current ||= current
    group.identities.push(identity)
    groups.set(id, group)
  }
  return [...groups.values()]
    .map((group) => ({
      ...group,
      identities: [...group.identities].sort(newestIdentityFirst),
    }))
    .sort((left, right) => Number(right.current) - Number(left.current) || identityTimestamp(right.identities[0]) - identityTimestamp(left.identities[0]))
}
