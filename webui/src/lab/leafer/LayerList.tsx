import { Group, Lock, Unlock, SquareDashedMousePointer, Trash2 } from 'lucide-react'
import { deviceMark, locked, type EditorCore } from '../../../../packages/inktag-editor-core/src/index'

export function LayerList({ core, act }: { core: EditorCore; act: (fn: () => unknown) => void }) {
  const selected = new Set(core.selectedWidgets.map(w => w.id))
  return <>
    <div className="layer-heading"><h2>图层 <small>{core.doc.widgets.length}</small></h2><div>
      <button aria-label="多选图层" title="桌面也可按住 Shift 点击，或在画板空白处拖动框选" aria-pressed={core.multiSelect} onClick={() => act(() => { core.multiSelect = !core.multiSelect; if (core.selection) core.selectMany(core.selectedWidgets.map(w => w.id)) })}><SquareDashedMousePointer /><span>多选</span></button>
      <button aria-label="选择所有可编辑图层" onClick={() => act(() => core.selectAll())}>全选</button>
    </div></div>
    <div className="layer-list">{[...core.doc.widgets].reverse().map(item => <div key={item.id} className={`layer-row${selected.has(item.id) ? ' active' : ''}`}>
      <button className={`layer-select${selected.has(item.id) ? ' active' : ''}`} aria-pressed={selected.has(item.id)} onClick={event => act(() => core.select(item.id, core.multiSelect || event.shiftKey || event.ctrlKey || event.metaKey))}><span>{item.name}</span>{item.groupId && <Group aria-label="已组合" />}</button>
      <label className="layer-lock" title={deviceMark(item) ? '设备固定标识，不可解锁' : locked(item) ? '解锁图层' : '锁定图层'}><input type="checkbox" aria-label={`锁定图层 ${item.name}`} checked={locked(item)} disabled={deviceMark(item)} onChange={event => act(() => core.setLocked(event.target.checked, [item.id]))} />{locked(item) ? <Lock /> : <Unlock />}</label>
      <button className="layer-delete" aria-label={`删除图层 ${item.name}`} title={item.groupId ? '删除此组合' : '删除图层'} disabled={locked(item) || Boolean(item.groupId && core.doc.widgets.some(w => w.groupId === item.groupId && locked(w)))} onClick={() => act(() => { core.selectMany([item.id]); core.remove() })}><Trash2 /></button>
    </div>)}</div>
    {core.multiSelect && <p className="multi-select-hint" role="status">多选中 · 已选 {selected.size} 个图层，点击可选中或取消。</p>}
  </>
}
