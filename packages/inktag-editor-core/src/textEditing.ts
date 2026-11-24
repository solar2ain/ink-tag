import { clone, type TextRun } from './index'

export type TextRange = { start:number; end:number }
export const templateText = (runs:TextRun[]) => runs.map(r=>r.text).join('')
function slice(runs:TextRun[],start:number,end:number) {
  let offset=0
  return runs.flatMap(run=>{
    const from=Math.max(0,start-offset),to=Math.min(run.text.length,end-offset);offset+=run.text.length
    return to>from?[{...clone(run),text:run.text.slice(from,to),gap:from?0:run.gap}]:[]
  })
}
export function mergeTextRuns(runs:TextRun[]) {
  const same=(r:TextRun)=>JSON.stringify([r.fontId,r.scale,r.raster,r.baselineShift||0,Object.entries(r.style||{}).filter(([,v])=>v!==undefined).sort(([a],[b])=>a.localeCompare(b))])
  return runs.reduce<TextRun[]>((result,run)=>{
    const last=result[result.length-1]
    if(last&&!(run.gap??0)&&same(last)===same(run))last.text+=run.text
    else result.push(clone(run))
    return result
  },[])
}
export function tokenRange(text:string,range:TextRange):TextRange {
  let start=Math.max(0,Math.min(text.length,Math.min(range.start,range.end))),end=Math.max(0,Math.min(text.length,Math.max(range.start,range.end)))
  if(start===end)return {start,end}
  for(const token of text.matchAll(/\{\{[^{}\n]+\}\}/g)) {
    const left=token.index!,right=left+token[0].length
    if(start<right&&end>left){start=Math.min(start,left);end=Math.max(end,right)}
  }
  return {start,end}
}
/** Style a text range, not a visible list of implementation fragments. */
export function formatText(runs:TextRun[],range:TextRange,change:(run:TextRun)=>TextRun) {
  const text=templateText(runs),selected=tokenRange(text,range.start===range.end?{start:0,end:text.length}:range)
  if(!text)return [change(runs[0]||{text:''})]
  return mergeTextRuns([...slice(runs,0,selected.start),...slice(runs,selected.start,selected.end).map(change),...slice(runs,selected.end,text.length)])
}
/** Preserve unaffected run styles and imported spacing while plain text is edited. */
export function editText(runs:TextRun[],next:string):TextRun[] {
  const previous=templateText(runs)
  if(previous===next)return runs
  let start=0,end=previous.length,nextEnd=next.length
  while(start<end&&start<nextEnd&&previous[start]===next[start])start++
  while(end>start&&nextEnd>start&&previous[end-1]===next[nextEnd-1]){end--;nextEnd--}
  // Never divide a UTF-16 surrogate pair when inheriting a style.
  if(start>0&&/[\uDC00-\uDFFF]/.test(previous[start]||next[start]||''))start--
  const anchor=end>start?start:Math.max(0,start-1)
  const inherited=slice(runs,anchor,anchor+1)[0]||runs[0]||{text:''}
  const inserted=next.slice(start,nextEnd)
  return mergeTextRuns([...slice(runs,0,start),...(inserted?[{...clone(inherited),text:inserted,gap:end>start||!start?inherited.gap:0}]:[]),...slice(runs,end,previous.length)])
}
