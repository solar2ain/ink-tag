// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import type { SerialPort } from './pn532'
import { forgetReaderPort, rememberReaderPort, restoreReaderPort } from './readerPort'

function port(usbProductId = 0x7523, connected = true): SerialPort {
  return {
    connected,
    getInfo: () => ({ usbVendorId: 0x1a86, usbProductId }),
    readable: null,
    writable: null,
    open: vi.fn(),
    setSignals: vi.fn(),
    close: vi.fn(),
  }
}

beforeEach(() => {
  const items = new Map<string, string>()
  vi.stubGlobal('localStorage', {
    getItem: (key: string) => items.get(key) ?? null,
    setItem: (key: string, value: string) => items.set(key, value),
    removeItem: (key: string) => items.delete(key),
  })
})

afterEach(() => {
  forgetReaderPort()
  vi.unstubAllGlobals()
})

it('reuses the exact verified reader during the session when identical adapters are authorized', async () => {
  const reader = port(), other = port()
  const serial = { requestPort: vi.fn(), getPorts: vi.fn().mockResolvedValue([other, reader]) }
  await expect(restoreReaderPort(serial)).resolves.toBeUndefined()
  expect(serial.getPorts).not.toHaveBeenCalled()
  rememberReaderPort(reader)
  expect(window.localStorage.getItem('inktag-pn532-reader')).toBe('6790:29987')
  await expect(restoreReaderPort(serial)).resolves.toBe(reader)
  serial.getPorts.mockResolvedValue([other])
  await expect(restoreReaderPort(serial)).resolves.toBeUndefined()
})

it('restores a unique authorized match after reload and declines an ambiguous or unavailable match', async () => {
  window.localStorage.setItem('inktag-pn532-reader', '6790:29987')
  const reader = port()
  const serial = { requestPort: vi.fn(), getPorts: vi.fn().mockResolvedValue([port(0x5523), reader, port(0x7523, false)]) }
  await expect(restoreReaderPort(serial)).resolves.toBe(reader)
  serial.getPorts.mockResolvedValue([reader, port()])
  await expect(restoreReaderPort(serial)).resolves.toBeUndefined()
  serial.getPorts.mockResolvedValue([])
  await expect(restoreReaderPort(serial)).resolves.toBeUndefined()
})
