import type { Frame, Widget, ResizeCorner } from './index'

export type Guide = { id: string; axis: 'x' | 'y'; position: number }
export type SnapLine = Pick<Guide, 'axis' | 'position'>
export type Alignment = 'left' | 'center' | 'right' | 'top' | 'middle' | 'bottom'

export function boundsOf(frames: Frame[]): Frame | undefined {
  if (!frames.length) return
  const x = Math.min(...frames.map(f => f.x)), y = Math.min(...frames.map(f => f.y))
  return { x, y, width: Math.max(...frames.map(f => f.x + f.width)) - x,
    height: Math.max(...frames.map(f => f.y + f.height)) - y }
}

/** Flat group IDs preserve paint order, fonts and geometry without nested engine objects. */
export function selectionUnits(widgets: Widget[]) {
  const groups = new Map<string, Widget[]>()
  for (const widget of widgets) {
    const key = widget.groupId ? `group:${widget.groupId}` : `widget:${widget.id}`
    const members = groups.get(key) ?? []; members.push(widget); groups.set(key, members)
  }
  return [...groups.values()].map(members => ({ ...boundsOf(members)!, members }))
}

export function alignmentOffset(frame: Frame, target: Frame, alignment: Alignment) {
  const x = alignment === 'left' ? target.x : alignment === 'center' ? target.x + (target.width - frame.width) / 2
    : alignment === 'right' ? target.x + target.width - frame.width : frame.x
  const y = alignment === 'top' ? target.y : alignment === 'middle' ? target.y + (target.height - frame.height) / 2
    : alignment === 'bottom' ? target.y + target.height - frame.height : frame.y
  return { x: Math.round(x - frame.x), y: Math.round(y - frame.y) }
}

/** Threshold is passed in document pixels by each input adapter, so zoom stays neutral. */
export function snapFrame(frame: Frame, targets: Frame[], guides: SnapLine[], distance: number, resize = false, corner: ResizeCorner = 'se') {
  const lines: SnapLine[] = []
  const correction = (axis: 'x' | 'y') => {
    const size = axis === 'x' ? 'width' : 'height'
    const leading = axis === 'x' ? corner.endsWith('w') : corner.startsWith('n')
    const anchors = resize ? [frame[axis] + (leading ? 0 : frame[size])] : [frame[axis], frame[axis] + frame[size] / 2, frame[axis] + frame[size]]
    const positions = [...guides.filter(g => g.axis === axis).map(g => g.position),
      ...targets.flatMap(t => [t[axis], t[axis] + t[size] / 2, t[axis] + t[size]])]
    let best = distance + .001, delta = 0, position: number | undefined
    for (const value of positions) for (const anchor of anchors) {
      const offset = Math.round(value - anchor)
      if (Math.abs(value - anchor) < best && Math.abs(offset) <= distance) {
        best = Math.abs(value - anchor); delta = offset; position = value
      }
    }
    if (position !== undefined) lines.push({ axis, position })
    return delta
  }
  return { x: correction('x'), y: correction('y'), lines }
}
