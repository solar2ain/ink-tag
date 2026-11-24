import { afterEach, beforeEach, expect, it, vi, type Mock } from 'vitest'

const csrf = 'c'.repeat(32)
const token = 't'.repeat(32)
const address = 'D2:A3:11:07:63:CF'
const reply = (value: object) => new Response(JSON.stringify(value), { status: 200 })
let events: EventTarget
let fetcher: Mock<(path: string, init?: any) => Promise<Response>>

beforeEach(() => {
  vi.resetModules()
  vi.useFakeTimers()
  events = new EventTarget()
  vi.stubGlobal('window', {
    location: new URL('https://127.0.0.1:8787/'),
    addEventListener: events.addEventListener.bind(events),
    removeEventListener: events.removeEventListener.bind(events),
  })
  fetcher = vi.fn().mockImplementation(async (path: string) =>
    reply(path.endsWith('capabilities') ? { available: true, csrfToken: csrf }
      : path.endsWith('acquire') ? { available: true, lease: token, ttlMs: 15000 } : { ok: true }))
  vi.stubGlobal('fetch', fetcher)
})
afterEach(() => { vi.useRealTimers(); vi.unstubAllGlobals() })

it.each(['https://example.github.io/', 'https://192.168.1.2:8787/', 'file:///index.html'])
('never probes a native bridge on unsupported origin %s', async (url) => {
  window.location = new URL(url) as any
  const { acquireImageLink } = await import('./imageLink')
  expect(await acquireImageLink(address, 'anything', () => true)).toBeNull()
  expect(fetcher).not.toHaveBeenCalled()
})

it.each(['63CF', '', '00:00:00:00:00:00'])('rejects an unbound peer %s without native lookup', async (peer) => {
  const { acquireImageLink } = await import('./imageLink')
  expect(await acquireImageLink(peer, 'anything', () => true)).toBeNull()
  expect(fetcher).not.toHaveBeenCalled()
})

it('does not probe without a live verified browser App', async () => {
  const { acquireImageLink } = await import('./imageLink')
  expect(await acquireImageLink(address, '', () => false)).toBeNull()
  expect(fetcher).not.toHaveBeenCalled()
})

it.each(['om6626_zkc42b', 'nrf52832_priceetag', 'tlsr8359_hanshow', 'future_board', ''])
('uses the same capability and lease path for %s', async (board) => {
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, board, () => true)
  expect(lease).not.toBeNull()
  const [, init] = fetcher.mock.calls[1]
  expect(JSON.parse(init.body)).toEqual({ address, boardId: board })
  expect(init.credentials).toBe('same-origin')
  expect(init.headers['X-InkTag-Image-Link-CSRF']).toBe(csrf)
  await vi.advanceTimersByTimeAsync(5000)
  expect(fetcher.mock.calls[2][0]).toBe('/api/v1/image-link/renew')
  expect(JSON.parse(fetcher.mock.calls[2][1].body)).toEqual({ lease: token })
  await lease!.close()
  await lease!.close()
  await vi.advanceTimersByTimeAsync(30000)
  expect(fetcher).toHaveBeenCalledTimes(4)
  expect(fetcher.mock.calls[3][0]).toBe('/api/v1/image-link/release')
  expect(fetcher.mock.calls[3][1].keepalive).toBe(true)
  expect(fetcher.mock.calls.every(([url]) => !url.includes(address) && !url.includes(token))).toBe(true)
})

it.each(['missing', 'rejected', 'unsupported', 'bad-token'])('probes unavailable capability only once: %s', async (mode) => {
  fetcher.mockImplementation(async () => {
    if (mode === 'rejected') throw new Error('offline')
    if (mode === 'missing') return new Response('<html>static</html>')
    return reply({ available: mode === 'bad-token', csrfToken: '' })
  })
  const { acquireImageLink } = await import('./imageLink')
  expect(await acquireImageLink(address, '', () => true)).toBeNull()
  expect(await acquireImageLink(address, '', () => true)).toBeNull()
  expect(fetcher).toHaveBeenCalledTimes(1)
})

it('bounds an unresponsive capability lookup and retains one-shot fallback', async () => {
  fetcher.mockReturnValue(new Promise(() => {}))
  const { acquireImageLink } = await import('./imageLink')
  const task = acquireImageLink(address, '', () => true)
  await vi.advanceTimersByTimeAsync(1000)
  expect(await task).toBeNull()
  expect(await acquireImageLink(address, '', () => true)).toBeNull()
  expect(fetcher).toHaveBeenCalledTimes(1)
})

it.each(['refused', 'timeout'])('does not retry native acquisition: %s', async (mode) => {
  fetcher.mockImplementation(async (path: string) => path.endsWith('capabilities')
    ? reply({ available: true, csrfToken: csrf })
    : mode === 'refused' ? reply({ available: false }) : new Promise(() => {}))
  const { acquireImageLink } = await import('./imageLink')
  const task = acquireImageLink(address, '', () => true)
  await vi.advanceTimersByTimeAsync(3100)
  expect(await task).toBeNull()
  expect(await acquireImageLink(address, '', () => true)).toBeNull()
  expect(fetcher).toHaveBeenCalledTimes(2)
})

it('releases on pagehide without disconnecting the browser or replaying release', async () => {
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => true)
  events.dispatchEvent(new Event('pagehide'))
  await lease!.close()
  expect(fetcher.mock.calls.map(([path]) => path)).toEqual([
    '/api/v1/image-link/capabilities', '/api/v1/image-link/acquire', '/api/v1/image-link/release',
  ])
})

it('releases when a live binding changes', async () => {
  let current = true
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => current)
  current = false
  await vi.advanceTimersByTimeAsync(5000)
  await lease!.close()
  expect(fetcher.mock.calls.at(-1)![0]).toBe('/api/v1/image-link/release')
  expect(fetcher).toHaveBeenCalledTimes(3)
})

it('releases a lease arriving after its browser binding was invalidated', async () => {
  let current = true
  const original = fetcher.getMockImplementation()!
  fetcher.mockImplementation(async (path: string, init: unknown) => {
    const value = await original(path, init)
    if (path.endsWith('acquire')) current = false
    return value
  })
  const { acquireImageLink } = await import('./imageLink')
  expect(await acquireImageLink(address, '', () => current)).toBeNull()
  expect(fetcher.mock.calls.at(-1)![0]).toBe('/api/v1/image-link/release')
})

it('renew failure stops maintenance and attempts release once', async () => {
  const original = fetcher.getMockImplementation()!
  fetcher.mockImplementation((path: string, init: unknown) => path.endsWith('renew')
    ? Promise.reject(new Error('local service stopped')) : original(path, init))
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => true)
  await vi.advanceTimersByTimeAsync(30000)
  await lease!.close()
  expect(fetcher.mock.calls.map(([path]) => path.split('/').at(-1))).toEqual([
    'capabilities', 'acquire', 'renew', 'release',
  ])
  expect(await acquireImageLink(address, '', () => true)).toBeNull()
})

it('release failure is bounded, does not throw into transport and disables further native requests', async () => {
  const original = fetcher.getMockImplementation()!
  fetcher.mockImplementation((path: string, init: unknown) => path.endsWith('release')
    ? new Promise(() => {}) : original(path, init))
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => true)
  const task = lease!.close()
  await vi.advanceTimersByTimeAsync(1000)
  await expect(task).resolves.toBeUndefined()
  expect(await acquireImageLink(address, '', () => true)).toBeNull()
  expect(fetcher).toHaveBeenCalledTimes(3)
})

it('reports actual parameters on acquire/inspect without tokens or another native acquire', async () => {
  const original = fetcher.getMockImplementation()!
  fetcher.mockImplementation(async (path: string, init: unknown) => {
    if (path.endsWith('acquire')) return reply({ available: true, lease: token, ttlMs: 15000,
      parameters: { intervalMs: 60, latency: 0, tx2M: true, address } })
    if (path.endsWith('inspect')) return reply({ ok: true, parameters: { intervalMs: 15, latency: 0 } })
    return original(path, init)
  })
  const events = vi.fn()
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => true, events)
  expect(events).toHaveBeenLastCalledWith({ phase: 'acquired', parameters: { intervalMs: 60, latency: 0, tx2M: true } })
  await lease!.sample!()
  expect(events).toHaveBeenLastCalledWith({ phase: 'inspected', parameters: { intervalMs: 15, latency: 0 } })
  expect(fetcher.mock.calls.filter(([path]) => path.endsWith('acquire'))).toHaveLength(1)
  expect(JSON.stringify(events.mock.calls)).not.toContain(address)
  expect(JSON.stringify(events.mock.calls)).not.toContain(token)
  await lease!.close()
  await lease!.sample!()
  expect(fetcher).toHaveBeenCalledTimes(4)
})

it('reports sticky refusal safely while retaining ordinary transfer fallback', async () => {
  fetcher.mockImplementation(async path => reply(path.endsWith('capabilities')
    ? { available: true, csrfToken: csrf }
    : { available: false, reason: 'preference_refused', parameters: { intervalMs: 60, latency: 0 }, secret: token }))
  const events = vi.fn()
  const { acquireImageLink } = await import('./imageLink')
  expect(await acquireImageLink(address, '', () => true, events)).toBeNull()
  expect(events).toHaveBeenLastCalledWith({ phase: 'unavailable', reason: 'preference_refused',
    parameters: { intervalMs: 60, latency: 0 } })
  expect(await acquireImageLink(address, '', () => true, events)).toBeNull()
  expect(events).toHaveBeenLastCalledWith({ phase: 'unavailable', reason: 'preference_refused' })
  expect(fetcher).toHaveBeenCalledTimes(2)
  expect(JSON.stringify(events.mock.calls)).not.toContain(token)
})

it('does not let a diagnostic observer throw into acquire or release', async () => {
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => true, () => { throw Error('observer') })
  expect(lease).not.toBeNull()
  await expect(lease!.close()).resolves.toBeUndefined()
})

it('does not report a late renewal as active after its lease was released', async () => {
  let finish: (value: Response) => void
  const original = fetcher.getMockImplementation()!
  fetcher.mockImplementation((path: string, init: unknown) => path.endsWith('renew')
    ? new Promise<Response>(resolve => { finish = resolve }) : original(path, init))
  const events = vi.fn()
  const { acquireImageLink } = await import('./imageLink')
  const lease = await acquireImageLink(address, '', () => true, events)
  await vi.advanceTimersByTimeAsync(5000)
  await lease!.close()
  finish!(reply({ ok: false, reason: 'lease_missing' }))
  await vi.advanceTimersByTimeAsync(30000)
  expect(events.mock.calls.map(([event]) => event.phase)).toEqual(['acquired', 'released'])
  expect(fetcher.mock.calls.map(([url]) => url.split('/').at(-1))).toEqual(['capabilities', 'acquire', 'renew', 'release'])
})
