import { AlignLeft, AlignCenter, AlignRight, Check, RotateCcw, Type, Paintbrush } from 'lucide-react'
import { kindOf, documentPageMode, type EditorCore, type Style } from '../../../../packages/inktag-editor-core/src/index'
import { inkColor, inksFor } from './palette'
import { deviceGuiFontOptions } from '../../device-gui/model'
import { nativeFontSize } from './richTextRenderer'
import { CreationFontOptions, CreationFontSize, creationFontValue, selectedFontSize } from './CreationFontFields'

type Props = { core: EditorCore; act: (fn: () => unknown) => void }
export function AppearancePanel({ core, act }: Props) {
  const w = core.selected!, s = core.selection
  const cell = s?.row !== undefined && s.col !== undefined ? w.cells[s.row]?.[s.col] : undefined
  const header = kindOf(w) === 'table' && s?.row === 0
  const drawnDigits=w.hostWeather?.field==='day-number'
  const style = { ...w.style, ...cell?.style }
  const mode = documentPageMode(core.doc)
  const change = (value: Partial<Style>) => act(() => core.updateStyle(value))
  const inks = inksFor(core.doc.colorMode)
  const colorRow = (key: 'color' | 'background' | 'borderColor', label: string, fallback: string) => {
    const value = key === 'color' && header ? cell?.style?.color || '#ffffff' : style[key] || fallback
    const mapped = inkColor(value, core.doc.colorMode)
    return <div className="ink-field" key={key}>
      <div className="ink-field-label"><span>{label}</span><code>{mapped.toUpperCase()}</code></div>
      <div className="ink-swatches" role="group" aria-label={label}>{key==='background'&&<button className="ink-swatch" aria-label="背景填充 · 透明" aria-pressed={value==='transparent'} onClick={()=>change({background:'transparent'})}><i className="transparent-swatch">{value==='transparent'&&<Check size={13}/>}</i><span>透明</span></button>}{inks.map(ink => <button key={ink.color}
        className="ink-swatch" title={`${label} · ${ink.name}`} aria-label={`${label} · ${ink.name}`}
        aria-pressed={mapped === ink.color} onClick={() => change({ [key]: ink.color })}>
        <i style={{ background: ink.color }}>{mapped === ink.color && <Check size={13} color={['白','黄'].includes(ink.name) ? '#191919' : '#ffffff'} />}</i>
        <span>{ink.name}</span>
      </button>)}</div>
    </div>
  }
  return <>
    <section className="inspector-section">
      <div className="section-heading"><h3><Type size={14} />文字</h3><span className="scope-badge">{cell ? '当前单元格' : '整个控件'}</span></div>
      {!drawnDigits&&<label className="font-family-field">字体<select aria-label="字体" value={creationFontValue(style.fontId || style.fontFamily || 'sans-serif', mode)} onChange={e => {
        const value=e.target.value, device=deviceGuiFontOptions.find(font=>font.id===value)
        change(device ? { fontId:device.id, fontFamily:undefined, fontSize:nativeFontSize(device.id) }
          : { fontId:undefined, fontFamily:value, fontSize:selectedFontSize(value, style.fontSize) })
      }}>
        <CreationFontOptions mode={mode}/>
      </select></label>}
      {drawnDigits&&<p className="field-hint">设备时钟数字</p>}
      <div className="type-metrics">{!drawnDigits&&<label>字重<select aria-label="字重" value={style.bold ? 'bold' : 'normal'} onChange={e => change({ bold: e.target.value === 'bold' })}><option value="normal">常规</option><option value="bold">加粗</option></select></label>}
        <label>字号<div className="font-size-input"><CreationFontSize key={`${w.id}-${s?.row}-${s?.col}`} label="字号" family={style.fontId ? undefined : style.fontFamily} mode={mode} value={style.fontSize} onChange={size=>change({fontSize:size})}/><span>px</span></div></label>
      </div>
      <div className="alignment-control"><span>对齐</span><div className="segmented-control" role="group" aria-label="文字对齐">{([
        ['left','左对齐',AlignLeft], ['center','居中',AlignCenter], ['right','右对齐',AlignRight],
      ] as const).map(([align, label, Icon]) => <button key={align} title={label} aria-label={label} aria-pressed={(style.align || 'center') === align} onClick={() => change({ align })}><Icon size={16} /></button>)}</div></div>
      {cell && <button className="inherit-style" onClick={() => act(() => core.resetCellStyle())}><RotateCcw size={13} />恢复控件默认样式</button>}
    </section>
    <section className="inspector-section"><div className="section-heading"><h3><Paintbrush size={14} />颜色</h3><span className="scope-badge">屏幕色板</span></div>
      {colorRow('color', '文字颜色', '#191919')}
      {colorRow('background', '背景填充', header ? '#b32025' : kindOf(w)==='text' ? 'transparent' : '#ffffff')}
      {(w.kind === 'table' || (!w.kind && w.cells.length > 1)) && colorRow('borderColor', '表格线条', '#191919')}
      {!cell && kindOf(w) === 'table' && <p className="field-hint">表头文字默认白色；选中单元格可单独设置。</p>}
    </section>
  </>
}
