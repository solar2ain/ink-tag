import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent, type WheelEvent } from 'react'
import type { FindTrackPoint } from './client'

const TILE_SIZE = 256
const MIN_ZOOM = 2
const MAX_ZOOM = 19
const PADDING = 42

interface WorldPoint {
  x: number
  y: number
}

interface Tile {
  key: string
  left: number
  top: number
  x: number
  y: number
  zoom: number
}

export interface OsmTrackView {
  centerX: number
  centerY: number
  zoom: number
}

interface TrackLayout {
  tiles: Tile[]
  points: WorldPoint[]
  markerPoints: WorldPoint[]
  orderedTrack: FindTrackPoint[]
  view: OsmTrackView
}

interface DragState {
  pointerId: number
  clientX: number
  clientY: number
  view: OsmTrackView
}

function normalizedWorld(latitude: number, longitude: number): WorldPoint {
  const boundedLatitude = Math.max(-85.05112878, Math.min(85.05112878, latitude))
  const radians = boundedLatitude * Math.PI / 180
  return {
    x: (longitude + 180) / 360,
    y: (1 - Math.log(Math.tan(radians) + (1 / Math.cos(radians))) / Math.PI) / 2,
  }
}

export function orderOsmTrackPoints(track: FindTrackPoint[]) {
  return [...track].sort((left, right) => {
    const byReportTime = Date.parse(left.timestamp) - Date.parse(right.timestamp)
    if (Number.isFinite(byReportTime) && byReportTime !== 0) return byReportTime
    const byReceivedTime = Date.parse(left.receivedAt) - Date.parse(right.receivedAt)
    if (Number.isFinite(byReceivedTime) && byReceivedTime !== 0) return byReceivedTime
    return left.timestamp.localeCompare(right.timestamp)
  })
}

export function osmTrackMarkerNumber(index: number, _total: number) {
  return index + 1
}

export function fitOsmTrackView(track: FindTrackPoint[], width: number, height: number): OsmTrackView {
  const normalized = orderOsmTrackPoints(track).map((point) => normalizedWorld(point.latitude, point.longitude))
  if (!normalized.length || width <= 0 || height <= 0) return { centerX: 0.5, centerY: 0.5, zoom: MIN_ZOOM }
  if (normalized.length === 1) return { centerX: normalized[0].x, centerY: normalized[0].y, zoom: MAX_ZOOM }
  const minX = Math.min(...normalized.map((point) => point.x))
  const maxX = Math.max(...normalized.map((point) => point.x))
  const minY = Math.min(...normalized.map((point) => point.y))
  const maxY = Math.max(...normalized.map((point) => point.y))
  const availableWidth = Math.max(1, width - PADDING * 2)
  const availableHeight = Math.max(1, height - PADDING * 2)
  const xSpan = Math.max(maxX - minX, 1 / (1 << MAX_ZOOM))
  const ySpan = Math.max(maxY - minY, 1 / (1 << MAX_ZOOM))
  const fittedZoom = Math.floor(Math.log2(Math.min(
    availableWidth / (xSpan * TILE_SIZE),
    availableHeight / (ySpan * TILE_SIZE),
  )))
  return {
    centerX: (minX + maxX) / 2,
    centerY: (minY + maxY) / 2,
    zoom: Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Number.isFinite(fittedZoom) ? fittedZoom : MAX_ZOOM)),
  }
}

function boundedView(view: OsmTrackView): OsmTrackView {
  return {
    centerX: view.centerX,
    centerY: Math.max(0, Math.min(1, view.centerY)),
    zoom: Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Math.round(view.zoom))),
  }
}

export function zoomOsmTrackView(
  view: OsmTrackView,
  nextZoom: number,
  anchorX: number,
  anchorY: number,
  width: number,
  height: number,
): OsmTrackView {
  const boundedZoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Math.round(nextZoom)))
  if (boundedZoom === view.zoom) return view
  const oldScale = TILE_SIZE * (1 << view.zoom)
  const newScale = TILE_SIZE * (1 << boundedZoom)
  const anchorWorldX = view.centerX + (anchorX - width / 2) / oldScale
  const anchorWorldY = view.centerY + (anchorY - height / 2) / oldScale
  return boundedView({
    centerX: anchorWorldX - (anchorX - width / 2) / newScale,
    centerY: anchorWorldY - (anchorY - height / 2) / newScale,
    zoom: boundedZoom,
  })
}

export function buildOsmTrackLayout(track: FindTrackPoint[], width: number, height: number, requestedView?: OsmTrackView): TrackLayout {
  const orderedTrack = orderOsmTrackPoints(track)
  const normalized = orderedTrack.map((point) => normalizedWorld(point.latitude, point.longitude))
  const view = boundedView(requestedView || fitOsmTrackView(orderedTrack, width, height))
  if (!normalized.length || width <= 0 || height <= 0) return { tiles: [], points: [], markerPoints: [], orderedTrack, view }
  const scale = TILE_SIZE * (1 << view.zoom)
  const originX = view.centerX * scale - width / 2
  const originY = view.centerY * scale - height / 2
  const firstTileX = Math.floor(originX / TILE_SIZE)
  const lastTileX = Math.floor((originX + width) / TILE_SIZE)
  const firstTileY = Math.max(0, Math.floor(originY / TILE_SIZE))
  const lastTileY = Math.min((1 << view.zoom) - 1, Math.floor((originY + height) / TILE_SIZE))
  const tiles: Tile[] = []
  for (let y = firstTileY; y <= lastTileY; y += 1) {
    for (let x = firstTileX; x <= lastTileX; x += 1) {
      const wrappedX = ((x % (1 << view.zoom)) + (1 << view.zoom)) % (1 << view.zoom)
      tiles.push({
        key: [view.zoom, x, y].join('-'),
        left: x * TILE_SIZE - originX,
        top: y * TILE_SIZE - originY,
        x: wrappedX,
        y,
        zoom: view.zoom,
      })
    }
  }
  const screenPoints = normalized.map((point) => ({ x: point.x * scale - originX, y: point.y * scale - originY }))
  return {
    tiles,
    orderedTrack,
    points: screenPoints,
    // Keep every numbered report at its true map coordinate. Moving overlapping
    // labels apart needed leader lines, which made stationary reports look like
    // movement. Later reports naturally stack above earlier ones instead.
    markerPoints: screenPoints,
    view,
  }
}

export function OsmTrackMap({ points }: { points: FindTrackPoint[] }) {
  const containerRef = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const dragRef = useRef<DragState | null>(null)
  const [size, setSize] = useState({ width: 0, height: 0 })
  const [view, setView] = useState<OsmTrackView | null>(null)
  const [dragging, setDragging] = useState(false)
  const [failedTiles, setFailedTiles] = useState<Set<string>>(() => new Set())
  const pointSignature = useMemo(() => points.map((point) => [point.timestamp, point.latitude, point.longitude].join(':')).join('|'), [points])

  useEffect(() => {
    const container = containerRef.current
    if (!container) return
    const update = () => setSize({ width: container.clientWidth, height: container.clientHeight })
    update()
    const observer = new ResizeObserver(update)
    observer.observe(container)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    setView(null)
  }, [pointSignature])

  const fittedView = useMemo(() => fitOsmTrackView(points, size.width, size.height), [pointSignature, points, size.height, size.width])
  const activeView = view || fittedView
  const layout = useMemo(
    () => buildOsmTrackLayout(points, size.width, size.height, activeView),
    [activeView, pointSignature, points, size.height, size.width],
  )
  const tileSignature = layout.tiles.map((tile) => tile.key).join('|')
  useEffect(() => { setFailedTiles(new Set()) }, [tileSignature])
  const tilesUnavailable = layout.tiles.length > 0 && layout.tiles.every((tile) => failedTiles.has(tile.key))

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || !size.width || !size.height) return
    const ratio = window.devicePixelRatio || 1
    canvas.width = Math.round(size.width * ratio)
    canvas.height = Math.round(size.height * ratio)
    const context = canvas.getContext('2d')
    if (!context) return
    context.setTransform(ratio, 0, 0, ratio, 0, 0)
    context.clearRect(0, 0, size.width, size.height)
    if (!layout.points.length) return
    context.lineCap = 'round'
    context.lineJoin = 'round'
    context.strokeStyle = 'rgba(255,255,255,.94)'
    context.lineWidth = 9
    context.beginPath()
    layout.points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y))
    context.stroke()
    context.strokeStyle = '#087f5b'
    context.lineWidth = 5
    context.beginPath()
    layout.points.forEach((point, index) => index ? context.lineTo(point.x, point.y) : context.moveTo(point.x, point.y))
    context.stroke()
  }, [layout, size.height, size.width])

  const zoomAt = (nextZoom: number, anchorX = size.width / 2, anchorY = size.height / 2) => {
    setView(zoomOsmTrackView(activeView, nextZoom, anchorX, anchorY, size.width, size.height))
  }

  const handleWheel = (event: WheelEvent<HTMLDivElement>) => {
    event.preventDefault()
    const bounds = event.currentTarget.getBoundingClientRect()
    zoomAt(activeView.zoom + (event.deltaY < 0 ? 1 : -1), event.clientX - bounds.left, event.clientY - bounds.top)
  }

  const handlePointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0) return
    event.currentTarget.setPointerCapture(event.pointerId)
    dragRef.current = { pointerId: event.pointerId, clientX: event.clientX, clientY: event.clientY, view: activeView }
    setDragging(true)
  }

  const handlePointerMove = (event: PointerEvent<HTMLDivElement>) => {
    const drag = dragRef.current
    if (!drag || drag.pointerId !== event.pointerId) return
    const scale = TILE_SIZE * (1 << drag.view.zoom)
    setView(boundedView({
      centerX: drag.view.centerX - (event.clientX - drag.clientX) / scale,
      centerY: drag.view.centerY - (event.clientY - drag.clientY) / scale,
      zoom: drag.view.zoom,
    }))
  }

  const endDrag = (event: PointerEvent<HTMLDivElement>) => {
    if (dragRef.current?.pointerId !== event.pointerId) return
    dragRef.current = null
    setDragging(false)
    if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId)
  }

  const handleKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.key === '+' || event.key === '=') {
      event.preventDefault()
      zoomAt(activeView.zoom + 1)
      return
    }
    if (event.key === '-') {
      event.preventDefault()
      zoomAt(activeView.zoom - 1)
      return
    }
    if (event.key === '0') {
      event.preventDefault()
      setView(null)
      return
    }
    const directions: Record<string, [number, number]> = {
      ArrowLeft: [-80, 0],
      ArrowRight: [80, 0],
      ArrowUp: [0, -80],
      ArrowDown: [0, 80],
    }
    const direction = directions[event.key]
    if (!direction) return
    event.preventDefault()
    const scale = TILE_SIZE * (1 << activeView.zoom)
    setView(boundedView({
      centerX: activeView.centerX + direction[0] / scale,
      centerY: activeView.centerY + direction[1] / scale,
      zoom: activeView.zoom,
    }))
  }

  return <div
    className={'find-osm-track-map' + (dragging ? ' is-dragging' : '')}
    ref={containerRef}
    role="application"
    tabIndex={0}
    aria-label="OSMap 交互轨迹地图；可拖动、滚轮缩放，轨迹点按时间编号"
    onWheel={handleWheel}
    onPointerDown={handlePointerDown}
    onPointerMove={handlePointerMove}
    onPointerUp={endDrag}
    onPointerCancel={endDrag}
    onKeyDown={handleKeyDown}
  >
    {layout.tiles.map((tile) => <img
      key={tile.key}
      src={'https://tile.openstreetmap.org/' + tile.zoom + '/' + tile.x + '/' + tile.y + '.png'}
      alt=""
      draggable={false}
      referrerPolicy="strict-origin-when-cross-origin"
      onError={() => setFailedTiles((previous) => new Set(previous).add(tile.key))}
      onLoad={() => setFailedTiles((previous) => {
        if (!previous.has(tile.key)) return previous
        const next = new Set(previous); next.delete(tile.key); return next
      })}
      style={{ left: tile.left, top: tile.top }}
    />)}
    <canvas ref={canvasRef} aria-hidden="true" />
    {tilesUnavailable && <p className="find-osm-map-error" role="status">底图加载失败，请检查网络或在地图网站打开。位置点仍可查看。</p>}
    <div className="find-track-point-layer" aria-label={layout.points.length + ' 个轨迹点'}>
      {layout.markerPoints.map((point, index) => {
        const trackPoint = layout.orderedTrack[index]
        const markerNumber = osmTrackMarkerNumber(index, layout.points.length)
        const first = index === 0
        const latest = index === layout.points.length - 1
        return <span
          className={'find-track-point-marker' + (first ? ' is-first' : '') + (latest ? ' is-latest' : '')}
          key={[trackPoint.timestamp, trackPoint.latitude, trackPoint.longitude].join('-')}
          style={{ left: point.x, top: point.y }}
          title={'轨迹点 ' + markerNumber + ' · ' + new Date(trackPoint.timestamp).toLocaleString() + ' · 精度约 ' + trackPoint.horizontalAccuracy + ' m'}
        >{markerNumber}</span>
      })}
    </div>
    <div className="find-track-map-controls" aria-label="地图缩放控制" onPointerDown={(event) => event.stopPropagation()}>
      <button type="button" onClick={() => zoomAt(activeView.zoom + 1)} disabled={activeView.zoom >= MAX_ZOOM} aria-label="放大轨迹地图" title="放大">＋</button>
      <button type="button" onClick={() => zoomAt(activeView.zoom - 1)} disabled={activeView.zoom <= MIN_ZOOM} aria-label="缩小轨迹地图" title="缩小">−</button>
      <button type="button" onClick={() => setView(null)} aria-label="显示完整轨迹" title="显示完整轨迹">◎</button>
    </div>
    <span className="find-track-map-hint">拖动地图 · 滚轮缩放</span>
    <a className="find-osm-attribution" href="https://www.openstreetmap.org/copyright" target="_blank" rel="noreferrer" onPointerDown={(event) => event.stopPropagation()}>© OpenStreetMap contributors</a>
  </div>
}
