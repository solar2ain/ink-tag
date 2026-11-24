import { describe, expect, it } from 'vitest'
import type { FindTrackPoint } from './client'
import { buildOsmTrackLayout, fitOsmTrackView, orderOsmTrackPoints, osmTrackMarkerNumber, zoomOsmTrackView } from './OsmTrackMap'

const point = (latitude: number, longitude: number, timestamp: string): FindTrackPoint => ({
  timestamp,
  receivedAt: timestamp,
  latitude,
  longitude,
  horizontalAccuracy: 20,
  confidence: 1,
  status: 0,
})

describe('keyless OpenStreetMap trajectory layout', () => {
  it('fits a multi-point route and requests only bounded valid tiles', () => {
    const layout = buildOsmTrackLayout([
      point(39.9042, 116.4074, '2026-09-12T08:00:00Z'),
      point(39.9142, 116.4274, '2026-09-12T08:15:00Z'),
    ], 900, 500)
    expect(layout.points).toHaveLength(2)
    expect(layout.tiles.length).toBeGreaterThan(0)
    expect(layout.tiles.length).toBeLessThanOrEqual(20)
    expect(layout.tiles.every((tile) => tile.zoom >= 2 && tile.zoom <= 19)).toBe(true)
    expect(layout.tiles.every((tile) => tile.x >= 0 && tile.x < (1 << tile.zoom))).toBe(true)
    expect(layout.tiles.every((tile) => tile.y >= 0 && tile.y < (1 << tile.zoom))).toBe(true)
  })

  it('uses a close zoom for a single report instead of producing invalid math', () => {
    const layout = buildOsmTrackLayout([
      point(31.2304, 121.4737, '2026-09-12T08:00:00Z'),
    ], 320, 280)
    expect(layout.points).toHaveLength(1)
    expect(layout.tiles[0].zoom).toBe(19)
  })

  it('orders and numbers the route oldest-first so the newest report has the largest number', () => {
    const ordered = orderOsmTrackPoints([
      point(39.9142, 116.4274, '2026-09-12T08:15:00Z'),
      point(39.9042, 116.4074, '2026-09-12T08:00:00Z'),
      point(39.9242, 116.4474, '2026-09-12T08:30:00Z'),
    ])
    expect(ordered.map((item) => item.timestamp)).toEqual([
      '2026-09-12T08:00:00Z',
      '2026-09-12T08:15:00Z',
      '2026-09-12T08:30:00Z',
    ])
    expect(buildOsmTrackLayout(ordered.slice().reverse(), 900, 500).orderedTrack).toEqual(ordered)
    expect(ordered.map((_item, index) => osmTrackMarkerNumber(index, ordered.length))).toEqual([1, 2, 3])
  })

  it('zooms around the cursor without moving its geographic anchor', () => {
    const track = [point(39.9042, 116.4074, '2026-09-12T08:00:00Z')]
    const view = fitOsmTrackView(track, 900, 500)
    const anchorX = 215
    const anchorY = 130
    const oldScale = 256 * (1 << view.zoom)
    const worldX = view.centerX + (anchorX - 450) / oldScale
    const worldY = view.centerY + (anchorY - 250) / oldScale
    const zoomed = zoomOsmTrackView(view, view.zoom - 1, anchorX, anchorY, 900, 500)
    const newScale = 256 * (1 << zoomed.zoom)
    expect(zoomed.centerX + (anchorX - 450) / newScale).toBeCloseTo(worldX, 12)
    expect(zoomed.centerY + (anchorY - 250) / newScale).toBeCloseTo(worldY, 12)
  })

  it('keeps overlapping report numbers at their true coordinate without leader lines', () => {
    const layout = buildOsmTrackLayout([
      point(31.2304, 121.4737, '2026-09-12T08:00:00Z'),
      point(31.2304, 121.4737, '2026-09-12T08:15:00Z'),
      point(31.2304, 121.4737, '2026-09-12T08:30:00Z'),
    ], 320, 280)
    expect(layout.markerPoints).toEqual(layout.points)
    expect(new Set(layout.markerPoints.map((marker) => `${marker.x}:${marker.y}`))).toHaveProperty('size', 1)
  })
})
