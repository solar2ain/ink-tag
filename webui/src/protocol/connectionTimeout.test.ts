import { afterEach, describe, expect, it, vi } from 'vitest'
import { BluetoothConnectionTimeoutError, withConnectionTimeout } from './connectionTimeout'

describe('Bluetooth connection timeout', () => {
  afterEach(() => vi.useRealTimers())

  it('returns a completed operation without invoking cleanup', async () => {
    const cleanup = vi.fn()
    await expect(withConnectionTimeout(Promise.resolve('ready'), 100, '读取服务', cleanup)).resolves.toBe('ready')
    expect(cleanup).not.toHaveBeenCalled()
  })

  it('rejects a stalled operation and invokes cleanup once', async () => {
    vi.useFakeTimers()
    const cleanup = vi.fn()
    const result = withConnectionTimeout(new Promise<never>(() => undefined), 100, '连接设备', cleanup)
    const rejection = expect(result).rejects.toEqual(new BluetoothConnectionTimeoutError('连接设备'))
    await vi.advanceTimersByTimeAsync(100)
    await rejection
    expect(cleanup).toHaveBeenCalledTimes(1)
  })
})
