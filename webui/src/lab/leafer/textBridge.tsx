import { clone, documentPageMode, type EditorCore } from '../../../../packages/inktag-editor-core/src/index'
import { textVariables } from '../../device-gui/textVariables'
import { previewDateTime, previewTimestamp, previewDateLimits, previewDefaultTimestamp } from '../../design/layoutPreviewTime'
import { richTextWarnings } from './richTextRenderer'
import { expandedTextFrame, fittedTextContent, textCellLayout } from './textFit'
import { RichTextEditor } from './RichTextEditor'
export { resolveLabText } from './richTextRenderer'

export function VariableTextEditor({core,act}:{core:EditorCore;act:(fn:()=>unknown)=>void}) {
  const dynamic=documentPageMode(core.doc)==='dynamic'
  const w=core.selected!,s=core.selection!,cell=w.cells[s.row??0][s.col??0]
  const content=cell.richText??{runs:[{text:cell.text}],padding:0,verticalAlign:'middle' as const}
  const template=content.runs.map(r=>r.text).join(''),missing=richTextWarnings(w,content,documentPageMode(core.doc))
  const unknown=[...template.matchAll(/\{\{([^{}]+)\}\}/g)].map(m=>m[1].trim()).filter(id=>!textVariables.some(v=>v.id===id))
  const layout=textCellLayout(w,content,s.row??0,s.col??0,documentPageMode(core.doc))
  const expanded=layout.overflow?expandedTextFrame(core.doc,w,content):undefined
  const sample=(value:Partial<NonNullable<typeof w.sample>>)=>act(()=>core.change(()=>{w.sample={...w.sample,...value}}))
  const {date,time}=previewDateTime(w.sample?.timestamp??previewDefaultTimestamp.v1)
  const setTime=(d:string,t:string)=>{const timestamp=previewTimestamp(d,t);if(timestamp!==undefined)sample({timestamp})}
  return <>
    <RichTextEditor core={core} act={act}/>
    {layout.overflow&&<div className="text-fit-panel" role="alert"><p>文字超出区域</p><div className="button-grid">
      {w.kind==='text'&&<button disabled={!expanded} title={expanded?'保持字号，扩大文字框':'周围空间不足，无法安全扩框'} onClick={()=>{if(expanded)act(()=>core.change(()=>Object.assign(w,expanded)))}}>自动扩框</button>}
      <button onClick={()=>act(()=>{const next=fittedTextContent(w,content,s.row??0,s.col??0,documentPageMode(core.doc));if(!next)return '当前字体的最小字号仍放不下，请减少文字或扩大区域。';core.change(()=>{cell.richText=next});return '已适配字号，文字框位置与大小保持不变。'})}>适配字号</button>
    </div><small>{w.kind==='text'&&!expanded?'周围空间不足，可适配字号或减少文字。':'扩框保留字号，适配字号保留区域。'}</small></div>}
    {missing.length>0&&<p role="alert" className="rich-warning">所选字库缺少：{missing.join('、')}。请选择其他字体或修改文字。</p>}
    {unknown.length>0&&<p role="alert" className="rich-warning">未知变量：{unknown.join('、')}。可从“插入变量”选择，例如日为 {'{{day}}'}。</p>}
    {dynamic&&<details className="inspector-section"><summary>变量数据</summary>
      <div className="text-data-fields"><label>日期<input aria-label="文本预览日期" type="date" {...previewDateLimits} value={date} onChange={e=>setTime(e.target.value,time)}/></label><label>时间<input aria-label="文本预览时间" type="time" step={1} value={time} onChange={e=>setTime(date,e.target.value)}/></label></div>
      {(['bluetoothName','temperature','voltage','deviceName','boardName','batteryPercent'] as const).filter(k=>template.includes(`{{${k==='batteryPercent'?'battery':k}}}`)).map(k=>{
        const numeric=['temperature','voltage','batteryPercent'].includes(k)
        return <label key={k}>{textVariables.find(v=>v.id===(k==='batteryPercent'?'battery':k))?.label}{k==='voltage'?'（mV）':''}<input aria-label={`文本样本 ${k}`} type={numeric?'number':'text'} maxLength={64} value={w.sample?.[k]??''} onChange={e=>sample({[k]:e.target.value===''?undefined:numeric?Number(e.target.value):e.target.value})}/></label>
      })}
      {template.includes('{{countdown')&&<><label>事件名称<input aria-label="倒计时事件名称" value={w.countdownTarget?.name??''} maxLength={64} placeholder="默认使用下一个节气" onChange={e=>act(()=>core.change(()=>{w.countdownTarget={targetTimestamp:0,...w.countdownTarget,name:e.target.value}}))}/></label>
        <label>目标日期<input aria-label="倒计时目标日期" type="date" {...previewDateLimits} value={w.countdownTarget?.targetTimestamp?previewDateTime(w.countdownTarget.targetTimestamp).date:''} onChange={e=>act(()=>core.change(()=>{w.countdownTarget={name:'',...w.countdownTarget,targetTimestamp:previewTimestamp(e.target.value,'00:00:00')??0}}))}/></label><p className="field-hint">未设置完整目标或日期已过期，自动显示下一个节气。</p></>}
      <p className="field-hint">仅影响本控件，不修改其他控件的数据。</p>
    </details>}
    <details className="inspector-section"><summary>排版与间距</summary>
      <div className="field-pair"><label>内边距<input aria-label="文字内边距" type="number" min={0} max={128} value={content.padding??0} onChange={e=>{const padding=Number(e.target.value);if(padding>=0&&padding<=128)act(()=>core.change(()=>{cell.richText={...clone(content),padding}}))}}/></label>
        <label>垂直对齐<select aria-label="文字垂直对齐" value={content.verticalAlign??'middle'} onChange={e=>{const verticalAlign=e.target.value as 'top'|'middle'|'bottom';act(()=>core.change(()=>{cell.richText={...clone(content),verticalAlign}}))}}><option value="top">顶部</option><option value="middle">居中</option><option value="bottom">底部</option></select></label></div>
      <p className="field-hint">字号不会随文字框缩放。默认保留预置的字间留白，支持手动换行。</p>
    </details>
  </>
}
