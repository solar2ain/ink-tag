// @vitest-environment jsdom
import { act, useState } from 'react'
import { createRoot, type Root } from 'react-dom/client'
import { afterEach, beforeEach, expect, it, vi } from 'vitest'
import { EditorCore, type Selection } from '../../../../packages/inktag-editor-core/src/index'
import { CellInput, commitCellEdit } from './CellInput'

let host: HTMLDivElement, root: Root, core: EditorCore, target: Selection
beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true)
  core = new EditorCore({ version: 1, width: 400, height: 300, widgets: [] })
  core.add('todo'); target = { id: core.selected!.id, row: 0, col: 0 }; core.selection = { ...target }
  host = document.createElement('div'); document.body.append(host); root = createRoot(host)
})
afterEach(async () => { await act(async () => root.unmount()); host.remove(); vi.unstubAllGlobals() })

it('commits a typed cell without unmounting its inspector during focus transfer', async () => {
  function Harness() {
    const [, render] = useState(0)
    const update = (fn: () => void) => { fn(); render(n => n + 1) }
    return <><CellInput text={core.selected!.cells[0][0].text} selection={target}
      commit={(s, text, next) => update(() => commitCellEdit(core, s, text, next))}
      close={() => update(() => core.select(target.id))} />
      {core.selection?.row === 0 && <button onClick={() => update(() => core.toggleChecked())}>标为完成</button>}</>
  }
  await act(async () => root.render(<Harness />))
  const input = host.querySelector('input')!, button = host.querySelector('button')!
  await act(async () => {
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')!.set!.call(input, '准备书包')
    input.dispatchEvent(new Event('input', { bubbles: true }))
  })
  await act(async () => button.focus())
  expect(host.querySelector('button')).toBe(button)
  await act(async () => button.click())
  expect(core.selected!.cells[0][0]).toMatchObject({ text: '准备书包', checked: true })
  expect(core.selection).toEqual(target)
  core.undo(); expect(core.selected!.cells[0][0].checked).not.toBe(true)
})

it('does not erase rich formatting or create history for an unchanged blur', () => {
  const cell = core.selected!.cells[0][0]
  cell.richText = { runs: [{ text: cell.text, style: { fontSize: 20, color: '#cf262d' } }] }
  const before = JSON.stringify(core.doc)
  commitCellEdit(core, target, cell.text)
  expect(JSON.stringify(core.doc)).toBe(before)
  core.updateStyle({ fontSize: 19 })
  expect(core.selected!.cells[0][0].style?.fontSize).toBe(19)
  expect(core.selected!.style.fontSize).not.toBe(19)
})

it('follows sidebar text changes without overwriting them on a later blur', async () => {
  const commit = vi.fn(), close = vi.fn()
  const render = (text: string) => act(async () => root.render(<CellInput text={text} selection={target} commit={commit} close={close} />))
  await render('原文字'); await render('右侧编辑后的文字')
  const input = host.querySelector('input')!
  expect(input.value).toBe('右侧编辑后的文字')
  await act(async () => input.blur())
  expect(commit).toHaveBeenLastCalledWith(target, '右侧编辑后的文字')
})

it.each(['tab', 'row'] as const)('preserves %s cell navigation and ignores stale navigation', next => {
  commitCellEdit(core, target, '新内容', next)
  expect(core.selection).toEqual({ ...target, row: 1 }) // Todo has one column.
  const selected = { ...core.selection! }
  commitCellEdit(core, target, '旧格子的新内容', next)
  expect(core.selection).toEqual(selected)
  core.deselect(); commitCellEdit(core, target, '提交失焦')
  expect(core.selection).toBeUndefined()
})

it('Escape cancels the plain text edit instead of committing it on unmount', async () => {
  const commit = vi.fn(), close = vi.fn()
  await act(async () => root.render(<CellInput text="原文字" selection={target} commit={commit} close={close} />))
  const input = host.querySelector('input')!
  await act(async () => input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })))
  await act(async () => input.blur())
  expect(close).toHaveBeenCalledOnce(); expect(commit).not.toHaveBeenCalled()
})
