import { Children, useEffect, useRef, useState, type CSSProperties, type ReactNode } from 'react'

type Side = 'left' | 'right'
export type PanelWidths = Record<Side, number>
const defaults: PanelWidths = { left: 224, right: 272 }
const minimum: PanelWidths = { left: 144, right: 216 }
const maximum: PanelWidths = { left: 360, right: 420 }
const storageKey = 'inktag.editor.panel-widths.v1'
const canvasAndHandles = 264 + 12
const clamp = (value: number, min: number, max: number) => Math.max(min, Math.min(max, value))

/** Keep the canvas usable when the window shrinks, without discarding preferred widths. */
export function fitPanelWidths(total: number, preferred: PanelWidths): PanelWidths {
  const left = clamp(preferred.left, minimum.left, maximum.left)
  const right = clamp(preferred.right, minimum.right, maximum.right)
  const spare = Math.max(0, total - canvasAndHandles - minimum.left - minimum.right)
  const extra = left + right - minimum.left - minimum.right
  const ratio = extra > 0 ? Math.min(1, spare / extra) : 1
  return { left: Math.round(minimum.left + (left - minimum.left) * ratio),
    right: Math.round(minimum.right + (right - minimum.right) * ratio) }
}

function loadWidths(): PanelWidths {
  try {
    const saved = JSON.parse(localStorage.getItem(storageKey) || 'null')
    if (saved && Number.isFinite(saved.left) && Number.isFinite(saved.right)) return saved
  } catch { /* Storage is optional. */ }
  return defaults
}

export function ResizableEditorPanels({ className, children, inert }: { className: string; children: ReactNode; inert?: boolean }) {
  const root = useRef<HTMLDivElement>(null)
  const [preferred, setPreferred] = useState(loadWidths)
  const [total, setTotal] = useState(1000)
  const [dragging, setDragging] = useState<Side | null>(null)
  const drag = useRef<{ x: number; widths: PanelWidths; side: Side } | null>(null)
  const widths = fitPanelWidths(total, preferred)
  const panes = Children.toArray(children)
  useEffect(() => {
    const element = root.current!
    const resize = () => setTotal(element.getBoundingClientRect().width)
    resize()
    const observer = new ResizeObserver(resize); observer.observe(element)
    return () => observer.disconnect()
  }, [])
  useEffect(() => {
    if (!dragging) try { localStorage.setItem(storageKey, JSON.stringify(preferred)) } catch { /* Optional preference. */ }
  }, [preferred, dragging])
  const maxFor = (side: Side, current = widths) => Math.max(minimum[side], Math.min(maximum[side], total - canvasAndHandles - current[side === 'left' ? 'right' : 'left']))
  const resize = (side: Side, value: number, current = widths) => setPreferred({ ...current, [side]: clamp(value, minimum[side], maxFor(side, current)) })
  const separator = (side: Side) => <div role="separator" aria-label={`${side === 'left' ? '左' : '右'}侧栏宽度`}
    aria-orientation="vertical" aria-valuemin={minimum[side]} aria-valuemax={Math.round(maxFor(side))} aria-valuenow={widths[side]}
    tabIndex={0} className="editor-panel-resizer" title="拖动调整宽度 · 双击还原 · 方向键微调"
    onPointerDown={(event) => {
      if (event.button !== 0) return
      event.preventDefault(); event.currentTarget.focus(); event.currentTarget.setPointerCapture(event.pointerId)
      drag.current = { side, x: event.clientX, widths }; setDragging(side)
    }}
    onPointerMove={(event) => {
      const start = drag.current
      if (start?.side === side) resize(side, start.widths[side] + (event.clientX - start.x) * (side === 'left' ? 1 : -1), start.widths)
    }}
    onPointerUp={(event) => {
      if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId)
      drag.current = null; setDragging(null)
    }}
    onLostPointerCapture={() => { drag.current = null; setDragging(null) }}
    onPointerCancel={() => { drag.current = null; setDragging(null) }}
    onDoubleClick={() => setPreferred((value) => ({ ...value, [side]: defaults[side] }))}
    onKeyDown={(event) => {
      const step = event.shiftKey ? 32 : 8
      const sign = side === 'left' ? 1 : -1
      const next = event.key === 'ArrowLeft' ? widths[side] - step * sign : event.key === 'ArrowRight' ? widths[side] + step * sign
        : event.key === 'Home' ? minimum[side] : event.key === 'End' ? maxFor(side) : undefined
      if (next !== undefined) { event.preventDefault(); event.stopPropagation(); resize(side, next) }
    }} />
  return <div ref={root} className={`${className} resizable-editor-panels${dragging ? ' resizing-panels' : ''}`} inert={inert}
    style={{ '--editor-left': `${widths.left}px`, '--editor-right': `${widths.right}px` } as CSSProperties}>
    {panes[0]}{separator('left')}{panes[1]}{separator('right')}{panes[2]}
  </div>
}
