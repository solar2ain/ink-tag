type AdvertisingDevice = BluetoothDevice & {
  watchAdvertisements?: (options: { signal: AbortSignal }) => Promise<void>
}

export function isBluetoothDeviceOutOfRange(error: unknown) {
  return error instanceof DOMException && error.name === 'NetworkError'
    && /Bluetooth Device is no longer in range/i.test(error.message)
}

export function waitForBluetoothRetry(milliseconds: number, signal: AbortSignal) {
  return new Promise<void>((resolve, reject) => {
    if (signal.aborted) { reject(new DOMException('蓝牙连接已取消', 'AbortError')); return }
    const finish = () => { signal.removeEventListener('abort', cancel); resolve() }
    const timer = globalThis.setTimeout(finish, milliseconds)
    const cancel = () => {
      globalThis.clearTimeout(timer)
      signal.removeEventListener('abort', cancel)
      reject(new DOMException('蓝牙连接已取消', 'AbortError'))
    }
    signal.addEventListener('abort', cancel, { once: true })
  })
}

// getDevices() restores permission, not the adapter's current discovery cache.
// Listen only for the selected device, and release scanning on every exit path.
export function waitForDeviceAdvertisement(device: BluetoothDevice, milliseconds: number, signal: AbortSignal) {
  const advertisingDevice = device as AdvertisingDevice
  if (!advertisingDevice.watchAdvertisements) return Promise.resolve(false)
  return new Promise<boolean>((resolve, reject) => {
    if (signal.aborted) { reject(new DOMException('蓝牙连接已取消', 'AbortError')); return }
    const scan = new AbortController()
    let finished = false
    const finish = (seen: boolean, error?: unknown) => {
      if (finished) return
      finished = true
      globalThis.clearTimeout(timer)
      device.removeEventListener('advertisementreceived', received)
      signal.removeEventListener('abort', cancel)
      scan.abort()
      if (error) reject(error)
      else resolve(seen)
    }
    const received = () => finish(true)
    const cancel = () => finish(false, new DOMException('蓝牙连接已取消', 'AbortError'))
    const timer = globalThis.setTimeout(() => finish(false), milliseconds)
    device.addEventListener('advertisementreceived', received)
    signal.addEventListener('abort', cancel, { once: true })
    try {
      Promise.resolve(advertisingDevice.watchAdvertisements!({ signal: scan.signal }))
        .catch((error) => finish(false, error))
    } catch (error) { finish(false, error) }
  })
}
