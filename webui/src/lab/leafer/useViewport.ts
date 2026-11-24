import { useEffect, useRef, useState, type PointerEvent as ReactPointerEvent } from 'react'
import { zoomAt, type Point } from '../../../../packages/inktag-editor-core/src/index'

export function useViewport(width: number, height: number, blank: { hit: (point: Point, handleRadius: number) => boolean; click: () => void }) {
  const stage = useRef<HTMLDivElement>(null)
  const [size, setSize] = useState({ width: 600, height: 500 })
  const [view, setView] = useState({ x: 30, y: 30, zoom: 1 })
  const [hand, setHand] = useState(false)
  const [space, setSpace] = useState(false)
  const [dragging, setDragging] = useState(false)
  const [overBlank, setOverBlank] = useState(false)
  const pan = useRef<{ id: number; x: number; y: number; vx: number; vy: number; blankClick: boolean; moved: boolean } | undefined>(undefined)
  const current = useRef(view); current.current = view
  useEffect(() => {
    const observer = new ResizeObserver(([entry]) => setSize({ width: entry.contentRect.width, height: entry.contentRect.height }))
    observer.observe(stage.current!)
    return () => observer.disconnect()
  }, [])
  const fit = (zoom?: number) => {
    const bounds = stage.current!.getBoundingClientRect()
    zoom ??= Math.max(.25, Math.min(3, (bounds.width - 64) / width, (bounds.height - 64) / height))
    setView({ zoom, x: (bounds.width - width * zoom) / 2, y: (bounds.height - height * zoom) / 2 })
  }
  useEffect(() => { fit(1) }, [width, height])
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.code !== 'Space' || (e.target as HTMLElement).closest('input,textarea,select,[contenteditable]')) return
      e.preventDefault(); setSpace(e.type === 'keydown')
    }
    const blur = () => { setSpace(false); setDragging(false); pan.current = undefined }
    window.addEventListener('keydown', onKey); window.addEventListener('keyup', onKey); window.addEventListener('blur', blur)
    const wheel = (e: WheelEvent) => {
      if((e.target as HTMLElement).closest('[data-text-popover]'))return
      e.preventDefault()
      const bounds = stage.current!.getBoundingClientRect()
      if (e.shiftKey) setView(v => ({ ...v, x: v.x - e.deltaY, y: v.y - e.deltaX }))
      else setView(v => zoomAt(v, { x: e.clientX - bounds.left, y: e.clientY - bounds.top }, v.zoom * Math.exp(-e.deltaY * .002)))
    }
    const el = stage.current!; el.addEventListener('wheel', wheel, { passive: false })
    return () => { el.removeEventListener('wheel', wheel); window.removeEventListener('keydown', onKey); window.removeEventListener('keyup', onKey); window.removeEventListener('blur', blur) }
  }, [])
  const isBlankAt = (e: ReactPointerEvent) => {
    const bounds = e.currentTarget.getBoundingClientRect(), v = current.current
    const overInput = (e.target as HTMLElement).closest('input,textarea,[contenteditable],[data-text-popover],[data-stage-control]')
    const point = { x: (e.clientX - bounds.left - v.x) / v.zoom,
      y: (e.clientY - bounds.top - v.y) / v.zoom }
    // Selection mode pans only the surrounding workspace, never blank page pixels.
    const outside = point.x < 0 || point.y < 0 || point.x >= width || point.y >= height
    return !overInput && outside && blank.hit(point, 6 / v.zoom)
  }
  const down = (e: ReactPointerEvent) => {
    if((e.target as HTMLElement).closest('[data-text-popover],[data-stage-control]'))return
    if (e.button !== 0 && e.button !== 1) return
    const v = current.current
    const explicitPan = hand || space || e.button === 1
    if (!explicitPan && !isBlankAt(e)) return
    // Normal blank clicks must blur/commit any active cell before deselection on UP.
    if (explicitPan) e.preventDefault()
    e.stopPropagation(); e.currentTarget.setPointerCapture(e.pointerId)
    pan.current = { id: e.pointerId, x: e.clientX, y: e.clientY, vx: v.x, vy: v.y, blankClick: !explicitPan, moved: false }
  }
  const move = (e: ReactPointerEvent) => {
    if((e.target as HTMLElement).closest('[data-text-popover]'))return
    const p = pan.current
    if (!p) { if (!e.buttons) setOverBlank(isBlankAt(e)); return }
    if (p.id !== e.pointerId) return
    e.preventDefault(); e.stopPropagation()
    if (!p.moved && Math.hypot(e.clientX - p.x, e.clientY - p.y) <= 3) return
    p.moved = true; setDragging(true)
    setView(v => ({ ...v, x: p.vx + e.clientX - p.x, y: p.vy + e.clientY - p.y }))
  }
  const up = (e: ReactPointerEvent) => {
    const p = pan.current
    if (p?.id !== e.pointerId) return
    e.stopPropagation(); pan.current = undefined; setDragging(false)
    setOverBlank(isBlankAt(e))
    if (e.type === 'pointerup' && p.blankClick && !p.moved) blank.click()
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId)
  }
  return { stage, size, view, hand, setHand, fit, dragging, overBlank, panning: hand || space,
    zoom: (factor: number) => setView(v => zoomAt(v, { x: size.width / 2, y: size.height / 2 }, v.zoom * factor)),
    handlers: { onPointerDownCapture: down, onPointerMoveCapture: move, onPointerUpCapture: up, onPointerCancelCapture: up,
      onPointerEnter: (e: ReactPointerEvent) => setOverBlank(isBlankAt(e)), onPointerLeave: () => setOverBlank(false),
      onLostPointerCapture: () => { pan.current = undefined; setDragging(false) } } }
}
