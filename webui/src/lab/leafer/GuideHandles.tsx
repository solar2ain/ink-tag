import { useRef } from 'react'
import type { EditorCore, Guide, Point } from '../../../../packages/inktag-editor-core/src/index'

export function GuideHandles({ core, view, size, redraw }: { core: EditorCore; view: Point & { zoom: number }; size: { width: number; height: number }; redraw: () => void }) {
  const drag = useRef<{ id: string; pointer: number; start: number; position: number; axis: Guide['axis']; zoom: number } | undefined>(undefined)
  if (!core.showGuides) return null
  return <>{core.doc.guides?.map(guide => {
    const position = core.guidePreview?.id === guide.id ? core.guidePreview.position : guide.position
    const pixel = (guide.axis === 'x' ? view.x : view.y) + position * view.zoom
    if (pixel < 0 || pixel > (guide.axis === 'x' ? size.width : size.height)) return null
    return <button key={guide.id} className={`guide-handle guide-${guide.axis}`} data-stage-control
      aria-label={`${guide.axis === 'x' ? '垂直' : '水平'}参考线 ${position} 像素`} title="拖动参考线，或用方向键微调"
      style={guide.axis === 'x' ? { left: pixel - 14, top: Math.max(2, Math.min(size.height - 28, view.y - 28)) } : { left: Math.max(2, Math.min(size.width - 28, view.x - 28)), top: pixel - 14 }}
      onPointerDown={event => { if (event.button !== 0) return; event.preventDefault(); event.stopPropagation(); core.cancel(); drag.current = { id: guide.id, pointer: event.pointerId, start: guide.axis === 'x' ? event.clientX : event.clientY, position: guide.position, axis: guide.axis, zoom: view.zoom }; event.currentTarget.setPointerCapture(event.pointerId) }}
      onPointerMove={event => { const active = drag.current; if (!active || active.pointer !== event.pointerId) return; event.stopPropagation()
        const value = active.position + ((active.axis === 'x' ? event.clientX : event.clientY) - active.start) / active.zoom
        core.guidePreview = { id: active.id, position: Math.max(0, Math.min(active.axis === 'x' ? core.doc.width : core.doc.height - (core.doc.footerInset ?? 18), Math.round(value))) }; redraw() }}
      onPointerUp={event => { if (drag.current?.pointer !== event.pointerId) return; event.stopPropagation(); const preview = core.guidePreview; drag.current = undefined; core.guidePreview = undefined; if (preview) core.setGuide(preview.id, preview.position); if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId); redraw() }}
      onPointerCancel={() => { drag.current = undefined; core.guidePreview = undefined; redraw() }}
      onLostPointerCapture={() => { if (drag.current) { drag.current = undefined; core.guidePreview = undefined; redraw() } }}
      onKeyDown={event => { event.stopPropagation(); if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); core.removeGuide(guide.id); redraw() }
        else if (event.key.startsWith('Arrow')) { event.preventDefault(); const direction = event.key === 'ArrowLeft' || event.key === 'ArrowUp' ? -1 : 1; core.setGuide(guide.id, position + direction * (event.shiftKey ? 10 : 1)); redraw() } }}
    ><span>{guide.axis === 'x' ? '▾' : '▸'}</span></button>
  })}</>
}
