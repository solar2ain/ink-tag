import QRCode from 'qrcode'

export const INKTAG_RELEASE_URL = 'https://github.com/solar2ain/ink-tag'
export const INKTAG_WEB_URL = 'https://solar2ain.github.io/ink-tag'
export const INKTAG_DEVICE_LINK_ROOT = `${INKTAG_WEB_URL}/`
/** Current product-facing QR destination. Keep one stable address across layouts and the editor. */
export const INKTAG_DEVICE_QR_URL = INKTAG_WEB_URL
/** Case-insensitive scheme/host permit QR alphanumeric packing without losing HTTPS. */
export const INKTAG_DEVICE_QR_COMPACT_VALUE = 'HTTPS://SOLAR2AIN.GITHUB.IO/ink-tag'

/**
 * Build the one public device link used by the device-information QR and NFC.
 * The query stays before the HashRouter fragment so a static GitHub Pages load
 * can read it before React routing starts.
 */
export function buildInkTagDeviceUrl(bluetoothName?: string | null) {
  if (!bluetoothName) return INKTAG_DEVICE_LINK_ROOT
  return `${INKTAG_DEVICE_LINK_ROOT}?ble=${encodeURIComponent(bluetoothName)}`
}

/** Read the exact advertised BLE name carried by an InkTag device link. */
export function bluetoothNameFromDeviceUrl(search: string) {
  try {
    const name = new URLSearchParams(search).get('ble')
    return name || ''
  } catch {
    return ''
  }
}

/** True only for the managed root link, optionally carrying one BLE hint. */
export function isManagedInkTagDeviceUrl(value: string) {
  try {
    const url = new URL(value)
    const base = new URL(INKTAG_WEB_URL)
    const normalizedPath = (path: string) => path.replace(/\/+$/, '')
    if (url.origin !== base.origin || normalizedPath(url.pathname) !== normalizedPath(base.pathname) || url.hash) return false
    return [...url.searchParams.keys()].every((key) => key === 'ble')
  } catch {
    return false
  }
}

export interface QrMatrix {
  size: number
  data: Uint8Array
}

export interface DrawnQrCode {
  left: number
  top: number
  size: number
  modulePixels: number
}

export const FIRMWARE_QR_MAX_MODULES = 57
export const FIRMWARE_QR_MIN_DISPLAY_MODULES = 33

export function createQrMatrix(value: string, minimumModules = 21, errorCorrectionLevel: 'L' | 'M' = 'M'): QrMatrix {
  const content = value.trim()
  if (!content) throw new RangeError('二维码内容不能为空')
  if (minimumModules < 21 || minimumModules > FIRMWARE_QR_MAX_MODULES || (minimumModules - 21) % 4 !== 0) {
    throw new RangeError('二维码最小模块数无效')
  }
  let modules = QRCode.create(content, { errorCorrectionLevel }).modules
  if (modules.size < minimumModules) {
    modules = QRCode.create(content, {
      errorCorrectionLevel,
      version: (minimumModules - 17) / 4,
    }).modules
  }
  return { size: modules.size, data: Uint8Array.from(modules.data) }
}

export function qrModule(matrix: QrMatrix, row: number, column: number) {
  return matrix.data[row * matrix.size + column] !== 0
}

export function packQrMatrix(matrix: QrMatrix) {
  if (matrix.size < 21 || matrix.size > FIRMWARE_QR_MAX_MODULES || (matrix.size - 21) % 4 !== 0) {
    throw new RangeError(`固件二维码边长必须为 21..${FIRMWARE_QR_MAX_MODULES} 且符合 QR 版本步进`)
  }
  const packed = new Uint8Array(Math.ceil(matrix.data.length / 8))
  matrix.data.forEach((value, index) => {
    if (value) packed[index >> 3] |= 0x80 >> (index & 7)
  })
  return { size: matrix.size, data: packed }
}

export function packQrValue(value: string) {
  return packQrMatrix(createQrMatrix(value, FIRMWARE_QR_MIN_DISPLAY_MODULES))
}

export function drawQrCode(
  context: CanvasRenderingContext2D,
  value: string,
  left: number,
  top: number,
  boxSize: number,
  dark = '#111714',
  light = '#ffffff',
  minimumModules = 21,
): DrawnQrCode {
  const matrix = createQrMatrix(value, minimumModules)
  const quietModules = 4
  const totalModules = matrix.size + quietModules * 2
  const modulePixels = Math.max(1, Math.floor(boxSize / totalModules))
  const size = totalModules * modulePixels
  const renderedLeft = Math.round(left + (boxSize - size) / 2)
  const renderedTop = Math.round(top + (boxSize - size) / 2)

  context.save()
  context.imageSmoothingEnabled = false
  context.fillStyle = light
  context.fillRect(renderedLeft, renderedTop, size, size)
  context.fillStyle = dark
  for (let row = 0; row < matrix.size; row += 1) {
    let start = -1
    for (let column = 0; column <= matrix.size; column += 1) {
      const filled = column < matrix.size && qrModule(matrix, row, column)
      if (filled && start < 0) start = column
      if (!filled && start >= 0) {
        context.fillRect(
          renderedLeft + (quietModules + start) * modulePixels,
          renderedTop + (quietModules + row) * modulePixels,
          (column - start) * modulePixels,
          modulePixels,
        )
        start = -1
      }
    }
  }
  context.restore()
  return { left: renderedLeft, top: renderedTop, size, modulePixels }
}
