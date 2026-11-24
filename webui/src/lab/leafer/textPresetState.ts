import { clone, type EditorCore, type RichText, type Style, type TextRun } from '../../../../packages/inktag-editor-core/src/index'
import { mergeTextRuns, templateText } from '../../../../packages/inktag-editor-core/src/textEditing'
import { expandedTextFrame, fittedTextContent } from './textFit'
import presets from './textPresets.json'

// Authoring provenance only; not a device payload. Compare semantic styles, not object key order.
function signature(content:RichText,base:Style) {
  const runs=mergeTextRuns(content.runs.map(run=>{
    const style={...base,...run.style}
    return {...run,gap:run.gap??0,baselineShift:run.baselineShift??0,
      style:{fontSize:style.fontSize,color:style.color,bold:style.bold,align:style.align??'left',fontId:style.fontId,fontFamily:style.fontFamily}}
  }))
  return JSON.stringify(runs.map(r=>[r.text,r.gap,r.baselineShift,r.fontId??null,r.scale??null,r.raster??null,
    r.style!.fontSize,r.style!.color,r.style!.bold,r.style!.align,r.style!.fontId??null,r.style!.fontFamily??null]))
}
export function selectedTextPreset(content:RichText,base:Style) {
  const current=signature(content,base)
  if(content.preset)return presets.some(p=>p.id===content.preset!.id)&&content.preset.signature===current?content.preset.id:''
  return presets.find(p=>signature({runs:p.runs as TextRun[]},base)===current)?.id??''
}
export function rememberTextPreset(content:RichText,base:Style,id:string):RichText {
  return {...content,preset:{id,signature:signature(content,base)}}
}
export function applyTextPreset(core:EditorCore,id:string) {
  const w=core.selected,s=core.selection,preset=presets.find(p=>p.id===id)
  if(!w||!s||!preset)return
  const row=s.row??0,col=s.col??0,cell=w.cells[row][col],base={...w.style,...cell.style}
  let content:RichText={...cell.richText,runs:clone(preset.runs) as TextRun[]}
  // Size against resolved values and actual fonts, never the {{variable}} source length.
  const frame=expandedTextFrame(core.doc,w,content,true)
  if(!frame)content=fittedTextContent(w,content,row,col)??content
  content=rememberTextPreset(content,base,id)
  core.change(()=>{cell.richText=content;cell.text=templateText(content.runs);if(frame)Object.assign(w,frame)})
}
