import { describe, expect, it } from 'vitest'
import {
  bluetoothNameFromDeviceUrl,
  buildInkTagDeviceUrl,
  createQrMatrix,
  FIRMWARE_QR_MAX_MODULES,
  INKTAG_DEVICE_QR_COMPACT_VALUE,
  INKTAG_DEVICE_QR_URL,
  INKTAG_RELEASE_URL,
  INKTAG_WEB_URL,
  isManagedInkTagDeviceUrl,
  packQrMatrix,
  packQrValue,
  qrModule,
} from './qr'

describe('QR generation', () => {
  it('uses canonical HTTPS links for the fixed InkTag destinations', () => {
    expect(INKTAG_RELEASE_URL).toBe('https://github.com/solar2ain/ink-tag')
    expect(INKTAG_WEB_URL).toBe('https://solar2ain.github.io/ink-tag')
    expect(INKTAG_DEVICE_QR_URL).toBe(INKTAG_WEB_URL)
  })

  it('builds one root device link with an exact percent-encoded BLE name', () => {
    expect(buildInkTagDeviceUrl()).toBe('https://solar2ain.github.io/ink-tag/')
    expect(buildInkTagDeviceUrl('InkTag_TL_9903')).toBe(
      'https://solar2ain.github.io/ink-tag/?ble=InkTag_TL_9903',
    )
    expect(buildInkTagDeviceUrl('InkTag_客厅')).toBe(
      'https://solar2ain.github.io/ink-tag/?ble=InkTag_%E5%AE%A2%E5%8E%85',
    )
    expect(bluetoothNameFromDeviceUrl('?ble=InkTag_%E5%AE%A2%E5%8E%85')).toBe('InkTag_客厅')
    expect(isManagedInkTagDeviceUrl(buildInkTagDeviceUrl('InkTag_TL_9903'))).toBe(true)
    expect(isManagedInkTagDeviceUrl('https://example.com/?ble=InkTag_TL_9903')).toBe(false)
  })

  it('creates a complete square QR matrix for arbitrary content', () => {
    const matrix = createQrMatrix('https://example.com/inktag?id=42')
    expect(matrix.size).toBeGreaterThanOrEqual(21)
    expect(matrix.data).toHaveLength(matrix.size * matrix.size)
    expect(qrModule(matrix, 0, 0)).toBe(true)
    expect(() => createQrMatrix('  ')).toThrow('二维码内容不能为空')
  })

  it('does not reuse one decorative pattern for different values', () => {
    expect(createQrMatrix(INKTAG_RELEASE_URL).data).not.toEqual(createQrMatrix(INKTAG_WEB_URL).data)
  })

  it('uses equal module dimensions for the two fixed deviceInfo destinations', () => {
    expect(packQrValue(INKTAG_RELEASE_URL).size).toBe(33)
    expect(packQrValue(INKTAG_WEB_URL).size).toBe(33)
  })

  it('keeps the full device WebUI QR small enough for integer 2x rendering on 152px', () => {
    const matrix = createQrMatrix(INKTAG_DEVICE_QR_URL)
    expect(matrix.size).toBe(29)
    expect((matrix.size + 8) * 2).toBeLessThanOrEqual(152 / 2)
  })

  it('packs the same HTTPS destination into 25 modules for both square panels', () => {
    const matrix = createQrMatrix(INKTAG_DEVICE_QR_COMPACT_VALUE, 21, 'L')
    expect(new URL(INKTAG_DEVICE_QR_COMPACT_VALUE).href).toBe(INKTAG_DEVICE_QR_URL)
    expect(matrix.size).toBe(25)
    expect((matrix.size + 8) * 3).toBeLessThanOrEqual(200 / 2)
  })

  it('packs a firmware QR row-major with the first module in the high bit', () => {
    const matrix = createQrMatrix(INKTAG_WEB_URL)
    const packed = packQrMatrix(matrix)
    expect(matrix.size).toBeLessThanOrEqual(FIRMWARE_QR_MAX_MODULES)
    expect(packed.data).toHaveLength(Math.ceil(matrix.size * matrix.size / 8))
    for (let index = 0; index < matrix.data.length; index += 1) {
      expect((packed.data[index >> 3] & (0x80 >> (index & 7))) !== 0).toBe(matrix.data[index] !== 0)
    }
  })
})
