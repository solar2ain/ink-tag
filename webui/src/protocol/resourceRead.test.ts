import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { CommandV2 } from './generatedCommandsV2'
import { resourceReadBudget, retryResourceRead } from './resourceRead'

describe('bounded resource snapshot retry', () => {
  const busy = new Error('busy')
  const isBusy = (error: unknown) => error === busy
  beforeEach(() => vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout', 'performance'] }))
  afterEach(() => vi.useRealTimers())

  it('retries only explicit BUSY with the ordinary per-request timeout', async () => {
    const request = vi.fn().mockRejectedValueOnce(busy).mockResolvedValue('ready')
    const result = retryResourceRead(CommandV2.GET_RESOURCE_INFO, request, isBusy, new AbortController().signal)
    await vi.advanceTimersByTimeAsync(1999)
    expect(request).toHaveBeenCalledTimes(1)
    await vi.advanceTimersByTimeAsync(1)
    expect(await result).toBe('ready')
    expect(request.mock.calls.map(([timeout]) => timeout)).toEqual([10000, 10000])
  })

  it('does not retry missing responses or authentication/transport errors', async () => {
    for (const error of [new Error('timeout'), new Error('denied'), new Error('disconnected')]) {
      const request = vi.fn().mockRejectedValue(error)
      await expect(retryResourceRead(CommandV2.GET_RESOURCE_INFO, request, isBusy, new AbortController().signal)).rejects.toBe(error)
      expect(request).toHaveBeenCalledTimes(1)
    }
  })

  it('accepts one delayed post-COMMIT reply while a native page refresh completes', async () => {
    const request = vi.fn((timeout: number) => new Promise<string>((resolve, reject) => {
      const expiry = setTimeout(() => reject(new Error('timeout')), timeout)
      setTimeout(() => { clearTimeout(expiry); resolve('committed image') }, 20000)
    }))
    const pending = retryResourceRead(CommandV2.GET_RESOURCE_INFO, request, isBusy,
      new AbortController().signal, resourceReadBudget(true))
    const result = expect(pending).resolves.toBe('committed image')
    await vi.advanceTimersByTimeAsync(20000)
    await result
    expect(request).toHaveBeenCalledTimes(1)
  })

  it('forbids mutations and image payload reads before sending', async () => {
    const request = vi.fn()
    for (const opcode of [CommandV2.DELETE_RESOURCE, CommandV2.READ_RESOURCE, CommandV2.SET_IMAGE_METADATA]) {
      await expect(retryResourceRead(opcode, request, isBusy, new AbortController().signal)).rejects.toThrow('不允许')
    }
    expect(request).not.toHaveBeenCalled()
  })

  it('cancels a backoff immediately and never sends another request', async () => {
    const controller = new AbortController()
    const request = vi.fn().mockRejectedValue(busy)
    const pending = retryResourceRead(CommandV2.LIST_RESOURCES, request, isBusy, controller.signal)
    const rejected = expect(pending).rejects.toThrow('取消')
    await vi.advanceTimersByTimeAsync(0)
    controller.abort()
    await rejected
    await vi.advanceTimersByTimeAsync(60000)
    expect(request).toHaveBeenCalledTimes(1)
  })

  it('shares the deadline and retry ceiling across pages without resetting them', async () => {
    const budget = resourceReadBudget()
    const first = vi.fn().mockRejectedValueOnce(busy).mockResolvedValue('info')
    const pending = retryResourceRead(CommandV2.GET_RESOURCE_INFO, first, isBusy, new AbortController().signal, budget)
    await vi.advanceTimersByTimeAsync(2000)
    await pending
    expect(budget.retries).toBe(1)
    budget.retries = 30
    const page = vi.fn().mockRejectedValue(busy)
    await expect(retryResourceRead(CommandV2.LIST_RESOURCES, page, isBusy, new AbortController().signal, budget)).rejects.toThrow('未就绪')
    expect(page).toHaveBeenCalledTimes(1)
    await vi.advanceTimersByTimeAsync(57000)
    const last = vi.fn().mockResolvedValue('last')
    expect(await retryResourceRead(CommandV2.LIST_RESOURCES, last, isBusy, new AbortController().signal, budget)).toBe('last')
    expect(last).toHaveBeenCalledWith(1000)
    await vi.advanceTimersByTimeAsync(1000)
    await expect(retryResourceRead(CommandV2.LIST_RESOURCES, last, isBusy, new AbortController().signal, budget)).rejects.toThrow('上限')
    expect(last).toHaveBeenCalledTimes(1)
  })

  it('bounds continuous BUSY below 60 seconds and never publishes late replies', async () => {
    const request = vi.fn().mockRejectedValue(busy)
    const pending = retryResourceRead(CommandV2.LIST_RESOURCES, request, isBusy, new AbortController().signal)
    const rejected = expect(pending).rejects.toThrow('未就绪')
    await vi.advanceTimersByTimeAsync(60000)
    await rejected
    expect(request.mock.calls.length).toBeLessThanOrEqual(31)
    const late = vi.fn(async () => { await vi.advanceTimersByTimeAsync(60001); return 'late' })
    await expect(retryResourceRead(CommandV2.GET_RESOURCE_INFO, late, isBusy, new AbortController().signal)).rejects.toThrow('上限')
  })

  it('binds the entire metadata/list pass to its original connection', async () => {
    const first = new AbortController()
    const next = new AbortController()
    const budget = resourceReadBudget()
    const request = vi.fn().mockResolvedValue('info')
    await retryResourceRead(CommandV2.GET_RESOURCE_INFO, request, isBusy, first.signal, budget)
    first.abort()
    await expect(retryResourceRead(CommandV2.LIST_RESOURCES, request, isBusy, next.signal, budget)).rejects.toThrow('取消')
    expect(request).toHaveBeenCalledTimes(1)
  })
})
