import type { SerialPort, WebSerial } from './pn532'

const storageKey = 'inktag-pn532-reader'
let lastPort: SerialPort | undefined

function usbSignature(port: SerialPort) {
  const info = port.getInfo?.()
  if (info?.usbVendorId != null && info.usbProductId != null) return `${info.usbVendorId}:${info.usbProductId}`
}

export function rememberReaderPort(port: SerialPort) {
  lastPort = port
  const signature = usbSignature(port)
  try {
    if (signature) window.localStorage.setItem(storageKey, signature)
    else window.localStorage.removeItem(storageKey)
  } catch { /* The current page can still keep the authorized port. */ }
}

export function forgetReaderPort() {
  lastPort = undefined
  try { window.localStorage.removeItem(storageKey) } catch { /* Storage is optional. */ }
}

export async function restoreReaderPort(serial: WebSerial) {
  let signature: string | null = null
  try { signature = window.localStorage.getItem(storageKey) } catch { /* Storage is optional. */ }
  if (!lastPort && !signature) return
  if (!serial.getPorts) return lastPort?.connected === false ? undefined : lastPort
  const ports: SerialPort[] = await serial.getPorts().catch(() => [])
  if (lastPort) return lastPort.connected !== false && ports.includes(lastPort) ? lastPort : undefined
  // The Web Serial API exposes the model IDs, but no unique USB serial number.
  if (signature) {
    const matching = ports.filter(port => port.connected !== false && usbSignature(port) === signature)
    if (matching.length === 1) return matching[0]
  }
}
