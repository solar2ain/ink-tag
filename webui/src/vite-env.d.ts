/// <reference types="vite/client" />

declare module '*?ota-adapter' {
  const install: (target: Window) => void
  export default install
}

declare module 'virtual:inktag-optional-assets' {
  const assets: Record<'calligraphy' | 'colorPhoto' | 'pixelCompact' | 'pixelMini' | 'pixelSong', { id: string; path: string }>
  export default assets
}

declare module 'virtual:inktag-default-creation-fonts' {
  const fonts: Record<'pixelBody' | 'pixelFusion', string>
  export default fonts
}

interface BluetoothRemoteGATTCharacteristic extends EventTarget {
  readonly properties?: { readonly writeWithoutResponse: boolean }
  value?: DataView
  readValue(): Promise<DataView>
  startNotifications(): Promise<BluetoothRemoteGATTCharacteristic>
  writeValueWithResponse(value: BufferSource): Promise<void>
  writeValueWithoutResponse(value: BufferSource): Promise<void>
}

interface BluetoothRemoteGATTService {
  getCharacteristic(characteristic: BluetoothServiceUUID): Promise<BluetoothRemoteGATTCharacteristic>
}

interface BluetoothRemoteGATTServer {
  connected: boolean
  connect(): Promise<BluetoothRemoteGATTServer>
  disconnect(): void
  getPrimaryService(service: BluetoothServiceUUID): Promise<BluetoothRemoteGATTService>
}

interface BluetoothDevice extends EventTarget {
  id: string
  name?: string
  gatt?: BluetoothRemoteGATTServer
}

type BluetoothServiceUUID = string | number

interface RequestDeviceOptions {
  filters?: Array<{ name?: string; namePrefix?: string; services?: BluetoothServiceUUID[] }>
  optionalServices?: BluetoothServiceUUID[]
  acceptAllDevices?: boolean
}

interface Bluetooth {
  requestDevice(options: RequestDeviceOptions): Promise<BluetoothDevice>
}

interface Navigator {
  bluetooth?: Bluetooth
}
