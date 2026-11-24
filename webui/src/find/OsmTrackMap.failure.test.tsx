// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import { expect, it, vi } from 'vitest'
import { OsmTrackMap } from './OsmTrackMap'

it('keeps the position marker and explains failed tiles without a retry loop', async () => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  vi.stubGlobal('ResizeObserver', class { observe() {} disconnect() {} })
  vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(640)
  vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(360)
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null)
  const host = document.createElement('div'); document.body.append(host)
  const root = createRoot(host)
  try {
    await act(async () => root.render(<OsmTrackMap points={[{ latitude: 31, longitude: 120,
      timestamp: '2026-11-09T00:00:00Z', receivedAt: '2026-11-09T00:00:01Z', horizontalAccuracy: 15, confidence: 1, status: 0 }]} />))
    const tiles = Array.from(host.querySelectorAll('img'))
    expect(tiles.length).toBeGreaterThan(0)
    expect(tiles.every(tile => tile.getAttribute('referrerpolicy') === 'strict-origin-when-cross-origin')).toBe(true)
    await act(async () => tiles.forEach(tile => tile.dispatchEvent(new Event('error'))))
    expect(host.querySelector('[role=status]')?.textContent).toContain('底图加载失败')
    expect(host.querySelectorAll('.find-track-point-marker')).toHaveLength(1)
    expect(host.querySelectorAll('img')).toHaveLength(tiles.length)
    await act(async () => tiles[0].dispatchEvent(new Event('load')))
    expect(host.querySelector('[role=status]')).toBeNull()
  } finally {
    await act(async () => root.unmount()); host.remove(); vi.restoreAllMocks(); vi.unstubAllGlobals()
  }
})
