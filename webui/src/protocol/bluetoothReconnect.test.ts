import { afterEach, describe, expect, it, vi } from 'vitest'
import { isBluetoothDeviceOutOfRange, waitForBluetoothRetry, waitForDeviceAdvertisement } from './bluetoothReconnect'

function advertisingDevice(watch = vi.fn().mockResolvedValue(undefined)) {
  return Object.assign(new EventTarget(), { id: 'remembered', name: 'IKT_TL_TEST', watchAdvertisements: watch }) as unknown as BluetoothDevice
}

afterEach(() => { vi.useRealTimers() })

describe('bounded Bluetooth rediscovery', () => {
  it('recognizes only the browser range-cache error', () => {
    expect(isBluetoothDeviceOutOfRange(new DOMException('Bluetooth Device is no longer in range.', 'NetworkError'))).toBe(true)
    expect(isBluetoothDeviceOutOfRange(new DOMException('Bluetooth permission has been blocked.', 'NetworkError'))).toBe(false)
    expect(isBluetoothDeviceOutOfRange(new Error('Bluetooth Device is no longer in range.'))).toBe(false)
  })

  it('falls back immediately when advertisements cannot be watched', async () => {
    await expect(waitForDeviceAdvertisement({} as BluetoothDevice, 15000, new AbortController().signal)).resolves.toBe(false)
  })

  it('waits for a later advertisement and then releases the scan and listeners', async () => {
    vi.useFakeTimers()
    const watch = vi.fn().mockResolvedValue(undefined)
    const device = advertisingDevice(watch)
    const remove = vi.spyOn(device, 'removeEventListener')
    const pending = waitForDeviceAdvertisement(device, 15000, new AbortController().signal)
    await vi.advanceTimersByTimeAsync(8000)
    expect(watch.mock.calls[0][0].signal.aborted).toBe(false)
    device.dispatchEvent(new Event('advertisementreceived'))
    await expect(pending).resolves.toBe(true)
    expect(watch.mock.calls[0][0].signal.aborted).toBe(true)
    expect(remove).toHaveBeenCalledWith('advertisementreceived', expect.any(Function))
    expect(vi.getTimerCount()).toBe(0)
  })

  it('times out even when starting the browser scan never resolves', async () => {
    vi.useFakeTimers()
    const watch = vi.fn((_options: { signal: AbortSignal }) => new Promise<void>(() => undefined))
    const pending = waitForDeviceAdvertisement(advertisingDevice(watch), 15000, new AbortController().signal)
    await vi.advanceTimersByTimeAsync(15000)
    await expect(pending).resolves.toBe(false)
    expect(watch.mock.calls[0][0].signal.aborted).toBe(true)
    expect(vi.getTimerCount()).toBe(0)
  })

  it('cancels without waiting for the scan timeout', async () => {
    vi.useFakeTimers()
    const controller = new AbortController()
    const watch = vi.fn().mockResolvedValue(undefined)
    const pending = waitForDeviceAdvertisement(advertisingDevice(watch), 15000, controller.signal)
    const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' })
    controller.abort()
    await rejected
    expect(watch.mock.calls[0][0].signal.aborted).toBe(true)
    expect(vi.getTimerCount()).toBe(0)
  })

  it.each(['reject', 'throw'])('releases listeners and scanning when the API fails by %s', async (mode) => {
    vi.useFakeTimers()
    const error = new DOMException('Scanning is not supported', 'NotSupportedError')
    const watch = mode === 'throw' ? vi.fn(() => { throw error }) : vi.fn().mockRejectedValue(error)
    const device = advertisingDevice(watch)
    const remove = vi.spyOn(device, 'removeEventListener')
    await expect(waitForDeviceAdvertisement(device, 15000, new AbortController().signal)).rejects.toBe(error)
    expect(watch.mock.calls[0][0].signal.aborted).toBe(true)
    expect(remove).toHaveBeenCalled()
    expect(vi.getTimerCount()).toBe(0)
  })

  it('does not start a scan or a retry after cancellation', async () => {
    const controller = new AbortController()
    controller.abort()
    const watch = vi.fn().mockResolvedValue(undefined)
    await expect(waitForDeviceAdvertisement(advertisingDevice(watch), 15000, controller.signal)).rejects.toMatchObject({ name: 'AbortError' })
    await expect(waitForBluetoothRetry(1000, controller.signal)).rejects.toMatchObject({ name: 'AbortError' })
    expect(watch).not.toHaveBeenCalled()
  })

  it('cancels the backoff timer immediately', async () => {
    vi.useFakeTimers()
    const controller = new AbortController()
    const pending = waitForBluetoothRetry(6000, controller.signal)
    const rejected = expect(pending).rejects.toMatchObject({ name: 'AbortError' })
    controller.abort()
    await rejected
    expect(vi.getTimerCount()).toBe(0)
  })
})
