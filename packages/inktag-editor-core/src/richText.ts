import type { RichText, Style, TextRun } from './index'

export type TextMetrics = { advance: number; left: number; top: number; right: number; bottom: number }
/** Canvas-engine independent baseline layout. A resize never rescales glyphs. */
export function layoutRichText(content: RichText, style: Style, width: number, height: number,
  measure: (run: TextRun, style: Style) => TextMetrics) {
  type Placed = {run:TextRun;style:Style;metrics:TextMetrics;x:number;y:number}
  const line = () => ({runs:[] as Placed[],cursor:0,left:Infinity,top:Infinity,right:-Infinity,bottom:-Infinity})
  const lines=[line()]
  for(const source of content.runs) {
    const chunks=source.text.split('\n')
    chunks.forEach((text,index)=>{
      if(index) lines.push(line())
      const current=lines[lines.length-1],run={...source,text,gap:index?0:source.gap}
      const resolved={...style,...run.style},metrics=measure(run,resolved)
      current.cursor+=run.gap??0
      const x=current.cursor,y=run.baselineShift??0
      current.left=Math.min(current.left,x+metrics.left);current.top=Math.min(current.top,y+metrics.top)
      current.right=Math.max(current.right,x+metrics.right);current.bottom=Math.max(current.bottom,y+metrics.bottom)
      current.cursor+=metrics.advance;current.runs.push({run,style:resolved,metrics,x,y})
    })
  }
  for(const l of lines) {
    if(!l.runs.length)l.left=l.right=l.top=l.bottom=0
    if(lines.length>1&&l.bottom===l.top){l.top=-style.fontSize;l.bottom=0}
  }
  const padding = content.padding ?? 0, contentWidth = Math.max(...lines.map(l=>l.right-l.left))
  const contentHeight=lines.reduce((sum,l)=>sum+l.bottom-l.top,0)+(lines.length-1)*4
  const align = style.align ?? 'left', vertical = content.verticalAlign ?? 'middle'
  let top = padding + Math.max(0, height - padding * 2 - contentHeight) * (vertical === 'bottom' ? 1 : vertical === 'middle' ? .5 : 0)
  const runs=lines.flatMap(l=>{
    const dx=padding-l.left+Math.max(0,width-padding*2-(l.right-l.left))*(align==='right'?1:align==='center'?.5:0)
    const result=l.runs.map(r=>({...r,x:Math.floor(r.x+dx),y:Math.floor(r.y+top-l.top)}))
    top+=l.bottom-l.top+4;return result
  })
  return { runs,
    width: contentWidth + padding * 2, height: contentHeight + padding * 2,
    overflow: contentWidth + padding * 2 > width || contentHeight + padding * 2 > height }
}
