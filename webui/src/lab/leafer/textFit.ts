import { cellFrame, kindOf, documentPageMode, type Document, type Frame, type RichText, type Widget, type PageMode } from '../../../../packages/inktag-editor-core/src/index'
import { nativeFontSize, richTextLayout } from './richTextRenderer'
import { creationPixelFont, pixelFontSize } from '../../data/creationFonts'

export function textCellLayout(w:Widget,content:RichText,row=0,col=0,mode:PageMode='dynamic') {
  const style={...w.style,...w.cells[row][col].style},frame=cellFrame(w,row,col)
  const inset=kindOf(w)==='text'?0:['todo','list'].includes(kindOf(w))?style.fontSize+12:4
  return richTextLayout(w,content,style,Math.max(1,frame.width-inset),frame.height,mode)
}
/** Explicit, undoable expansion. Never move a neighbor or cover its bounds. */
export function expandedTextFrame(doc:Document,w:Widget,content:RichText,exact=false):Frame|undefined {
  if(kindOf(w)!=='text'||w.native)return
  const layout=textCellLayout(w,content,0,0,documentPageMode(doc)),width=Math.max(exact?1:w.width,Math.ceil(layout.width)),height=Math.max(exact?1:w.height,Math.ceil(layout.height))
  const availableHeight=doc.height-(doc.footerInset??18)
  if(width>doc.width||height>availableHeight)return
  const align={...w.style,...w.cells[0][0].style}.align
  const x=Math.max(0,Math.min(doc.width-width,Math.round(w.x-(width-w.width)*(align==='right'?1:align==='center'?.5:0))))
  const y=Math.max(0,Math.min(availableHeight-height,Math.round(w.y-(height-w.height)*(content.verticalAlign==='top'?0:content.verticalAlign==='bottom'?1:.5))))
  const frame={x,y,width,height}
  const overlap=(a:Frame,b:Frame)=>a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height
  if(doc.widgets.some(other=>other.id!==w.id&&overlap(frame,other)))return
  return frame
}
/** Scale mixed typography together, retaining font families, colors and relative emphasis. */
export function fittedTextContent(w:Widget,content:RichText,row=0,col=0,mode:PageMode='dynamic'):RichText|undefined {
  if(!textCellLayout(w,content,row,col,mode).overflow)return content
  const base={...w.style,...w.cells[row][col].style}
  const resolved=content.runs.map(run=>{
    const size=run.style?.fontSize??base.fontSize,id=run.fontId||(run.style?.fontFamily?undefined:base.fontId)
    const scale=run.scale??(id?Math.round(size/nativeFontSize(id)*256):256)
    const pixel = mode==='static' && !id ? creationPixelFont(run.style?.fontFamily ?? base.fontFamily) : undefined
    return {run,size,id,scale,pixel}
  })
  const minimum=Math.max(...resolved.map(({size,id,scale})=>Math.max(6/size,id?64/scale:0)))
  if(minimum>=1)return
  const at=(factor:number):RichText=>({...content,runs:resolved.map(({run,size,id,scale,pixel})=>{
    const targetSize=Math.max(6,Math.floor(size*factor))
    return {...run,scale:id?Math.max(64,Math.floor(scale*targetSize/size)):undefined,
      gap:run.gap===undefined?undefined:run.gap*factor,baselineShift:run.baselineShift===undefined?undefined:run.baselineShift*factor,
      style:{...run.style,fontSize:pixel?pixelFontSize(pixel,targetSize):targetSize}}
  })})
  let low=minimum,high=1,best=at(low)
  if(textCellLayout(w,best,row,col,mode).overflow)return
  for(let i=0;i<9;i++) {
    const mid=(low+high)/2,candidate=at(mid)
    if(textCellLayout(w,candidate,row,col,mode).overflow)high=mid
    else {low=mid;best=candidate}
  }
  return best
}
