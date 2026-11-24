import { Group, Ungroup, Lock, Unlock, Magnet, Ruler, Plus, X, MoveHorizontal, MoveVertical } from 'lucide-react'
import { deviceMark, type Alignment, type EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { useState } from 'react'

const alignments: [Alignment, string][] = [['left','左对齐'],['center','水平居中'],['right','右对齐'],['top','顶对齐'],['middle','垂直居中'],['bottom','底对齐']]
function AlignIcon({ alignment }: { alignment: Alignment }) {
  const horizontal = ['left','center','right'].includes(alignment), center = alignment === 'center' || alignment === 'middle'
  const edge = center ? 12 : alignment === 'left' || alignment === 'top' ? 4 : 20
  return <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" aria-hidden="true" style={{ transform: horizontal ? undefined : 'rotate(90deg)' }}>
    <path d={`M${edge} 2v20`} /><rect x={center ? 7 : edge === 4 ? 6 : 9} y="5" width="9" height="5" rx="1" /><rect x={center ? 9 : edge === 4 ? 6 : 13} y="14" width="5" height="5" rx="1" />
  </svg>
}
export function SelectionTools({ core, act }: { core: EditorCore; act: (fn: () => unknown) => void }) {
  const [reference, setReference] = useState<'selection' | 'canvas'>('selection')
  const widgets = core.selectedWidgets, count = widgets.length, blocked = !count || core.selectionLocked
  const to = core.units.length > 1 ? reference : 'canvas'
  return <section className="selection-tools" aria-label="对齐与图层工具">
    {count > 0 && <>
      {count > 1 && <div className="selection-action-grid">
        <button title="组合 · Ctrl / ⌘ + G" disabled={core.units.length < 2 || blocked} onClick={() => act(() => core.group())}><Group />组合</button>
        <button title="取消组合 · Ctrl / ⌘ + Shift + G" disabled={blocked || !widgets.some(w => w.groupId)} onClick={() => act(() => core.ungroup())}><Ungroup />取消组合</button>
      </div>}
      <details className="inspector-section alignment-panel" open={core.multiple}><summary>对齐与间距</summary>
        <label className="alignment-reference">对齐到<select aria-label="对齐参照" value={to} disabled={core.units.length < 2} onChange={event => setReference(event.target.value as typeof reference)}><option value="selection">所选范围</option><option value="canvas">画板内容区</option></select></label>
        <div className="alignment-grid">{alignments.map(([alignment,label]) => <button key={alignment} aria-label={label} title={label} disabled={blocked} onClick={() => act(() => core.align(alignment,to))}><AlignIcon alignment={alignment} /><span>{label}</span></button>)}</div>
        <div className="selection-pair"><button disabled={blocked || core.units.length < 3} onClick={() => act(() => core.distribute('x'))}><MoveHorizontal />水平等间距</button><button disabled={blocked || core.units.length < 3} onClick={() => act(() => core.distribute('y'))}><MoveVertical />垂直等间距</button></div>
        <p className="field-hint">{core.selectionLocked ? '所选包含锁定图层，请先解锁。' : core.units.length === 1 ? '单个控件或组合相对于内容区对齐，避开底部设备标识。' : '组合整体参与对齐，内部位置、字号保持不变。'}</p>
      </details>
    </>}
    <details className="inspector-section guide-panel"><summary>吸附与参考线</summary>
      <label className="lab-check"><input type="checkbox" aria-label="拖动吸附" checked={core.snapEnabled} onChange={event => act(() => { core.snapEnabled = event.target.checked })} /><Magnet />拖动吸附</label>
      <label className="lab-check"><input type="checkbox" aria-label="显示参考线" checked={core.showGuides} onChange={event => act(() => { core.showGuides = event.target.checked })} /><Ruler />显示参考线</label>
      <div className="selection-pair"><button onClick={() => act(() => core.addGuide('x'))}><Plus />垂直参考线</button><button onClick={() => act(() => core.addGuide('y'))}><Plus />水平参考线</button></div>
      <div className="guide-list">{core.doc.guides?.map((guide,index) => <div className="guide-setting" key={guide.id}><label>{guide.axis === 'x' ? '垂直' : '水平'} {index + 1}<input type="number" aria-label={`参考线 ${index + 1} 位置`} key={guide.position} defaultValue={guide.position} min={0} max={guide.axis === 'x' ? core.doc.width : core.doc.height - (core.doc.footerInset ?? 18)} onBlur={event => { if (event.target.value !== '') act(() => core.setGuide(guide.id, Number(event.target.value))) }} onKeyDown={event => { if (event.key === 'Enter') event.currentTarget.blur() }} /></label><button className="icon-button" aria-label={`删除参考线 ${index + 1}`} onClick={() => act(() => core.removeGuide(guide.id))}><X /></button></div>)}</div>
      <p className="field-hint">拖动画板边缘的蓝色标记可移动参考线。吸附到画板、其他控件和参考线；按住 Alt 临时关闭吸附。辅助线不会导出或发送到设备。</p>
    </details>
  </section>
}

export function SelectionActions({core,act}:{core:EditorCore;act:(fn:()=>unknown)=>void}) {
  if(!core.selectedWidgets.length)return null
  return <div className="selection-bottom-actions">
    <button disabled={core.selectionLocked} onClick={()=>act(()=>{core.copy();core.paste()})}>{core.multiple?'复制所选':'复制控件'}</button>
    <div className="tools danger-zone">
      <button title="锁定 / 解锁 · Ctrl / ⌘ + Shift + L" disabled={core.selectedWidgets.some(deviceMark)} onClick={()=>act(()=>core.setLocked(!core.selectionLocked))}>{core.selectionLocked?<Unlock/>:<Lock/>}{core.selectionLocked?'解锁所选':'锁定所选'}</button>
      <button className="danger" disabled={core.selectionLocked} onClick={()=>act(()=>core.remove())}>{core.multiple?'删除所选':'删除控件'}</button>
    </div>
  </div>
}
