// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest'
import assets from 'virtual:inktag-optional-assets'

afterEach(() => {
  vi.restoreAllMocks(); vi.useRealTimers(); vi.resetModules()
  delete (globalThis as { __inktagOptionalAssets?: unknown }).__inktagOptionalAssets
  document.head.innerHTML = ''
})

it('loads a content-addressed sibling once and reuses the result', async () => {
  const { loadOptionalAsset } = await import('./optionalAssets')
  const first = loadOptionalAsset('colorPhoto'), second = loadOptionalAsset('colorPhoto')
  expect(second).toBe(first)
  const script = document.head.querySelector('script')!
  expect(script.src).toContain('/assets/optional-colorPhoto-')
  Object.assign(globalThis, { __inktagOptionalAssets: { [assets.colorPhoto.id]: 'data:image/png;base64,AAAA' } })
  script.dispatchEvent(new Event('load'))
  await expect(first).resolves.toBe('data:image/png;base64,AAAA')
  await expect(loadOptionalAsset('colorPhoto')).resolves.toBe('data:image/png;base64,AAAA')
  expect(document.head.querySelector('script')).toBeNull()
})

it('missing resources reject locally and a later request can retry', async () => {
  const { loadOptionalAsset } = await import('./optionalAssets')
  const first = loadOptionalAsset('calligraphy')
  const rejected = expect(first).rejects.toThrow('unavailable')
  document.head.querySelector('script')!.dispatchEvent(new Event('error'))
  await rejected
  const retry = loadOptionalAsset('calligraphy')
  const rejectedRetry = expect(retry).rejects.toThrow('missing')
  document.head.querySelector('script')!.dispatchEvent(new Event('load'))
  await rejectedRetry
})

it('a resource that never answers times out without leaving a script or rejected cache', async () => {
  vi.useFakeTimers()
  const { loadOptionalAsset } = await import('./optionalAssets')
  const first = loadOptionalAsset('calligraphy')
  const rejected = expect(first).rejects.toThrow('timed out')
  await vi.advanceTimersByTimeAsync(8000)
  await rejected
  expect(document.head.querySelector('script')).toBeNull()
})
