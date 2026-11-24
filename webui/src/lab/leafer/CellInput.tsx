import { useEffect, useRef, useState } from 'react'
import { locked, type EditorCore, type Selection } from '../../../../packages/inktag-editor-core/src/index'

export function commitCellEdit(core: EditorCore, target: Selection, value: string, next?: 'tab' | 'row') {
  const widget = core.doc.widgets.find(item => item.id === target.id)
  if (!widget || locked(widget) || core.multiple || target.row === undefined || target.col === undefined) return
  const cell = widget.cells[target.row]?.[target.col]
  if (!cell) return
  // A focus transfer is not a plain-text replacement of existing rich formatting.
  if (cell.text !== value) core.updateCellAt(target, value)
  const selected = core.selection
  // Blur keeps the cell's inspector available; a late blur must not steal a newer selection.
  if (!next || selected?.id !== target.id || selected.row !== target.row || selected.col !== target.col) return
  const rows = widget.cells.length, cols = widget.cells[0].length
  core.selection = {
    id: target.id,
    row: next === 'row' ? (target.row + 1) % rows : (target.row + (target.col + 1 >= cols ? 1 : 0)) % rows,
    col: next === 'row' ? target.col : (target.col + 1) % cols,
  }
}

export function CellInput({ text, selection, commit, close }: {
  text: string; selection: Selection; commit: (s: Selection, text: string, next?: 'tab' | 'row') => void; close: () => void
}) {
  const [value, setValue] = useState(text), cancelled = useRef(false)
  useEffect(() => { setValue(text) }, [text])
  return <input aria-label="单元格内容" autoFocus value={value} onChange={e => setValue(e.target.value)}
    onBlur={() => { if (!cancelled.current) commit(selection, value) }} onKeyDown={e => {
      if (e.nativeEvent.isComposing) return
      if (e.key === 'Escape') { cancelled.current = true; close() }
      if (e.key === 'Tab') { e.preventDefault(); cancelled.current = true; commit(selection, value, 'tab') }
      if (e.key === 'Enter') { e.preventDefault(); cancelled.current = true; commit(selection, value, 'row') }
    }} />
}
