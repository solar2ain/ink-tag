import { CanvasImageControls } from './CanvasImageControls'
import { kindOf, locked, deviceMark, resizable, type EditorCore, type Document } from '../../../../packages/inktag-editor-core/src/index'
import { SelectionTools, SelectionActions } from './SelectionTools'
import { NativeInspector } from './NativeInspector'
import { VariableTextEditor } from './textBridge'
import { AppearancePanel } from './AppearancePanel'
import { SlotCarouselInspector } from './SlotCarouselInspector'
import { colorModes, inkColor, inksFor } from './palette'
import { WeatherPanel } from './WeatherPanel'
import { documentPageMode } from '../../../../packages/inktag-editor-core/src/index'
import { weatherLabels, type WeatherData } from '../../../../packages/inktag-editor-core/src/weather'

type Props = { core: EditorCore; act: (fn: () => unknown) => void; notice: (s: string) => void; imageRequest?: {id:string;nonce:number}; weather?:WeatherData; onWeather?:(data?:WeatherData)=>void }
export function Inspector({ core, act, notice, imageRequest, weather, onWeather }: Props) {
  const w = core.selected, s = core.selection
  const cell = w && s?.row !== undefined && s.col !== undefined ? w.cells[s.row]?.[s.col] : undefined
  const textEditing = Boolean(w && !w.hostWeather && !locked(w) && !core.multiple && !w.native && (kindOf(w)==='text'||(cell&&['table','todo','list'].includes(kindOf(w)))))
  const screen=<details className={w?'inspector-section':'screen-palette'} open={!w}><summary>画板色彩</summary><label>目标屏幕色彩<select aria-label="目标屏幕色彩" value={core.doc.colorMode ?? 2}
      onChange={e => act(() => core.change(doc => { doc.colorMode = Number(e.target.value) as Document['colorMode'] }))}>
      {colorModes.map(mode => <option value={mode.value} key={mode.value}>{mode.label}</option>)}
    </select></label><div className="palette-strip" aria-label="屏幕可用颜色">{inksFor(core.doc.colorMode).map(ink => <i key={ink.name} title={ink.name} style={{ background: ink.color }} />)}</div>
      <p className="field-hint">切换色板保留原始配色，可撤销。图片按所选抖动算法重新处理。</p></details>
  const geometry=w&&<details className="inspector-section geometry-panel" open={!textEditing}><summary>位置与图层</summary><div className="metrics">{(['x','y','width','height'] as const).map((key, i) => <label className="number-field" key={`${w.id}-${key}`}>{['X','Y','宽度','高度'][i]}
        <input aria-label={['X','Y','宽度','高度'][i]} key={w[key]} type="number" disabled={locked(w) || (i > 1 && !resizable(w))} defaultValue={w[key]} onBlur={e => { if (e.target.value !== '') act(() => core.setFrame({ [key]: Number(e.target.value) })) }} onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur() }} />
      </label>)}</div>
      <div className="tools"><button onClick={() => act(() => core.reorder(1))}>上移一层</button><button onClick={() => act(() => core.reorder(-1))}>下移一层</button></div></details>
  return <aside id="canvas-mobile-properties" className="property-panel properties"><h2>{core.multiple ? `已选 ${core.selectedWidgets.length} 个图层` : cell && w?.kind!=='text' ? `内容 · ${s!.row! + 1} 行 ${s!.col! + 1} 列` : w?.name || '画板属性'}</h2>
    <SelectionTools core={core} act={act} />
    {documentPageMode(core.doc)==='host-dynamic'&&onWeather&&<WeatherPanel core={core} act={act} data={weather} onData={onWeather}/>}
    {!textEditing&&screen}
    {core.multiple ? <>
      <p className="field-hint">{core.units.length === 1 ? '当前组合整体移动；取消组合后可编辑内部控件。' : '拖动已选控件可整体移动，或使用下方按钮精确微调。'}每次操作均可撤销。</p>
      <div className="selection-nudge" role="group" aria-label="微调所选位置">{([[-1,0,'左移'],[0,-1,'上移'],[0,1,'下移'],[1,0,'右移']] as const).map(([x,y,label]) => <button key={label} disabled={core.selectionLocked} onClick={() => act(() => core.nudge(x,y))}>{label} 1px</button>)}</div>
    </> : w ? <>
      {!textEditing&&geometry}
      {locked(w) ? <p className="note">{deviceMark(w) ? 'InkTag 标识由设备管理，不可移动、修改或删除。' : '图层已锁定，不能移动、调整大小或编辑内容。解锁后继续编辑。'}</p> : w.native ? <NativeInspector core={core} act={act} /> : w.imageSlots ? <SlotCarouselInspector key={w.id} core={core} act={act} /> : kindOf(w) === 'stroke' ? <div className="inspector-section">
        <h3>笔迹</h3><label>颜色<select aria-label="笔迹颜色" value={inkColor(w.style.color, core.doc.colorMode)} onChange={event => act(() => core.updateStyle({ color: event.target.value }))}>{inksFor(core.doc.colorMode).map(ink => <option key={ink.color} value={ink.color}>{ink.name}</option>)}</select></label>
        <p>笔宽 {w.stroke?.width ?? 1} px · 整笔可移动、复制、撤销；橡皮笔迹为白色，可调整图层顺序。</p>
      </div> : w.hostWeather ? <><p>{weatherLabels[w.hostWeather.field]} · 内容随天气数据更新。</p><AppearancePanel core={core} act={act}/></> : kindOf(w) !== 'image' ? <>
        {textEditing ? <><VariableTextEditor key={`${w.id}-${s?.row??0}-${s?.col??0}`} core={core} act={act} />{geometry}{screen}</> : <AppearancePanel core={core} act={act} />}
        {kindOf(w) === 'qr' && <label>二维码内容<textarea aria-label="二维码内容" value={w.cells[0][0].text} onChange={e => act(() => core.updateCellAt({ id: w.id, row: 0, col: 0 }, e.target.value))} /></label>}
        {cell && w.kind!=='text' && <div className="tools"><button onClick={() => act(() => core.select(w.id))}>选整个控件</button></div>}
        {['table','todo','list'].includes(kindOf(w)) && <div className="structure"><h3>{kindOf(w) === 'table' ? `表格 · ${w.cells.length} 行 ${w.cells[0].length} 列` : `列表 · ${w.cells.length} 项`}</h3><div className="button-grid">
          <button onClick={() => act(() => core.structure('row'))}>＋ {cell ? '下方插入' : '末尾增加'}</button><button disabled={w.cells.length <= 1} onClick={() => act(() => core.structure('row', true))}>− {cell ? '删除本行' : '删除末行'}</button>
          {kindOf(w) === 'table' && <><button onClick={() => act(() => core.structure('col'))}>＋ {cell ? '右侧插列' : '增加列'}</button><button disabled={w.cells[0].length <= 1} onClick={() => act(() => core.structure('col', true))}>− {cell ? '删除本列' : '删除末列'}</button></>}
        </div>{kindOf(w) === 'todo' && cell && <button onClick={() => act(() => core.toggleChecked())}>{cell.checked ? '标为未完成' : '标为完成'}</button>}</div>}
      </> : <CanvasImageControls key={w.id} core={core} act={act} notice={notice} request={imageRequest} />}
    </> : <><p>添加控件，或点击画板开始编辑。</p><div className="help"><h3>视图操作</h3><p>灰色区域拖动：直接平移<br/>平移工具：整个画板可拖动<br/>单击空白：取消选中<br/>滚轮：围绕鼠标缩放<br/>空格＋拖动：在控件上平移<br/>鼠标中键：平移<br/>Shift＋滚轮：横向平移<br/>H / V：平移 / 选择工具</p><h3>编辑操作</h3><p>Ctrl / ⌘ + Z：撤销<br/>Ctrl / ⌘ + Shift + Z：重做<br/>Ctrl / ⌘ + C、V：复制粘贴<br/>方向键：1 px · Shift：10 px<br/>Ctrl / ⌘ + S：保存作品</p></div></>}
    {!w&&<p className="note">{documentPageMode(core.doc)==='host-dynamic'?'使用浏览器字体编辑；CLI 使用主机本地字体渲染，请检查生成图片。':'文字可使用设备点阵或浏览器字体。局部样式覆盖控件默认值；发送前请确认目标设备分辨率一致。'}</p>}
    <SelectionActions core={core} act={act}/>
  </aside>
}
