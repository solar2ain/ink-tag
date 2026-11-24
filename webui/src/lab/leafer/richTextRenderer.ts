import { layoutRichText, type TextMetrics } from '../../../../packages/inktag-editor-core/src/richText'
import type { RichText, Style, TextRun, Widget, PageMode } from '../../../../packages/inktag-editor-core/src/index'
import { DevicePixelGfx, type DevicePixelFrame } from '../../device-gui/devicePixelGfx'
import { missingDeviceGlyphs } from '../../device-gui/fontRenderer'
import { resolveTextTemplate } from '../../device-gui/textVariables'
import { defaultDeviceGuiStyle, type DeviceGuiWidget } from '../../device-gui/model'
import { previewDefaultTimestamp } from '../../design/layoutPreviewTime'
import { creationPixelFont, rasterCreationText } from '../../data/creationFonts'

export function textWidget(w: Widget): DeviceGuiWidget {
  return { id: w.id, kind: 'raster-text', variant: 'normal', frame: w, style: defaultDeviceGuiStyle,
    data: { text: w.cells[0][0].text, sample: { timestamp: previewDefaultTimestamp.v1, ...w.sample }, countdownTarget: w.countdownTarget } }
}
export function resolveLabText(w: Widget, text: string) { return text.includes('{{') ? resolveTextTemplate(text,textWidget(w)) : text }
type Glyphs = TextMetrics & { width: number; height: number; mask: Uint8Array }
export const nativeFontSize = (id: string) => id==='wqy9-lunar'?12:id==='wqy12-lunar'?16:id.includes('18')?24:20
const cache = new Map<string, Glyphs>()
export function clearTextRasterCache() { cache.clear() }
let hostCanvas: (() => HTMLCanvasElement) | undefined
export function setTextRasterCanvasFactory(factory: () => HTMLCanvasElement) { hostCanvas=factory;cache.clear() }
export function measureTextRun(run: TextRun, style: Style, useCreationFonts = false): Glyphs {
  const fontId = run.fontId || (run.style?.fontFamily ? undefined : style.fontId)
  const scale = run.scale ?? (fontId?Math.round(style.fontSize/nativeFontSize(fontId)*256):256)
  const pixelFamily = useCreationFonts && !fontId && creationPixelFont(style.fontFamily)
  const key = JSON.stringify([run.text,fontId,scale,run.raster,style.fontFamily,style.fontSize,style.bold,Boolean(pixelFamily)])
  const cached = cache.get(key); if (cached) return cached
  const pixelRaster = pixelFamily && rasterCreationText(run.text, style)
  if (pixelRaster) {
    if(cache.size>=512) cache.delete(cache.keys().next().value!)
    cache.set(key,pixelRaster)
    return pixelRaster
  }
  const pad = 32, baseline = 256
  let advance: number, frame: DevicePixelFrame
  if (fontId || (!hostCanvas && typeof document === 'undefined')) {
    const gfx = new DevicePixelGfx(1,1)
    gfx.setFont(fontId || 'wqy9-lunar'); gfx.setFontScale(scale)
    advance = Math.min(8192,gfx.getUTF8Width(run.text))
    const target = new DevicePixelGfx(Math.max(1,advance + pad * 2),512)
    target.setFont(fontId || 'wqy9-lunar'); target.setFontScale(scale)
    target.fontRasterMode = run.raster ?? 'nearest'; target.setFontMode(1)
    target.setCursor(pad,baseline); target.print(run.text)
    if (style.bold) { target.setCursor(pad+1,baseline); target.print(run.text) }
    frame = target.frame
  } else {
    const canvas = hostCanvas ? hostCanvas() : document.createElement('canvas'), ctx = canvas.getContext('2d')!
    const font = `${style.bold ? 'bold' : 'normal'} ${style.fontSize}px ${!pixelFamily && style.fontFamily || 'sans-serif'}`
    ctx.font = font; advance = Math.min(8192,Math.ceil(ctx.measureText(run.text).width))
    canvas.width = advance + pad * 2; canvas.height = 512
    ctx.font = font; ctx.textBaseline = 'alphabetic'; ctx.fillText(run.text,pad,baseline)
    const rgba = ctx.getImageData(0,0,canvas.width,canvas.height).data
    frame = { width:canvas.width,height:canvas.height,pixels:Uint8Array.from({length:canvas.width*canvas.height},(_,i)=>rgba[i*4+3]>=128?1:0) }
  }
  let x1=frame.width,y1=frame.height,x2=-1,y2=-1
  frame.pixels.forEach((p,i) => { if(p) { const x=i%frame.width,y=Math.floor(i/frame.width); x1=Math.min(x1,x); y1=Math.min(y1,y); x2=Math.max(x2,x); y2=Math.max(y2,y) } })
  if (x2 < 0) { x1=pad; x2=pad-1; y1=baseline; y2=baseline-1 }
  const width=x2-x1+1,height=y2-y1+1,mask=new Uint8Array(width*height)
  for(let y=0;y<height;y++) mask.set(frame.pixels.subarray((y+y1)*frame.width+x1,(y+y1)*frame.width+x1+width),y*width)
  const result = { advance,left:x1-pad,top:y1-baseline,right:x2-pad+1,bottom:y2-baseline+1,width,height,mask }
  if (!pixelFamily) {
    if(cache.size>=512) cache.delete(cache.keys().next().value!)
    cache.set(key,result)
  }
  return result
}
export function richTextLayout(w: Widget, content = w.cells[0][0].richText!, style = w.style, width=w.width, height=w.height, mode:PageMode='dynamic') {
  return layoutRichText({ ...content, runs:content.runs.map(run=>{
    const text=resolveLabText(w,run.text),font=run.fontId||(run.style?.fontFamily?undefined:style.fontId)
    // Host-raster pages may use browser fallback; an MCU font subset is not an admission gate.
    return mode!=='dynamic'&&font&&missingDeviceGlyphs(font,text).length
      ? {...run,text,fontId:undefined,scale:undefined,style:{...run.style,fontId:undefined,fontFamily:run.style?.fontFamily||style.fontFamily||'sans-serif'}}
      : {...run,text}
  }) },style,width,height,(run, resolved) => measureTextRun(run, resolved, mode==='static'))
}
const colors = ['#ffffff','#000000','#dc3026','#ebc219','#2c61a5','#137463','#e18b23']
function colorIndex(hex: string) {
  const channels=(s:string)=>[1,3,5].map(i=>parseInt(s.slice(i,i+2),16))
  const input=channels(hex), distances=colors.map(c=>channels(c).reduce((sum,v,i)=>sum+(v-input[i])**2,0))
  return distances.indexOf(Math.min(...distances))
}
export function richTextRaster(w: Widget, content: RichText, style=w.style, width=w.width,height=w.height,mode:PageMode='dynamic'): DevicePixelFrame & {alpha:Uint8Array} {
  const length=Math.max(1,Math.round(width))*Math.max(1,Math.round(height))
  const frame = { width:Math.max(1,Math.round(width)),height:Math.max(1,Math.round(height)),pixels:new Uint8Array(length),alpha:new Uint8Array(length) }
  for (const r of richTextLayout(w,content,style,width,height,mode).runs) {
    const glyph=measureTextRun(r.run,r.style,mode==='static'), color=colorIndex(r.style.color)
    for(let y=0;y<glyph.height;y++) for(let x=0;x<glyph.width;x++) {
      const px=r.x+glyph.left+x,py=r.y+glyph.top+y
      if(px>=0&&py>=0&&px<frame.width&&py<frame.height&&glyph.mask[y*glyph.width+x]) {frame.pixels[py*frame.width+px]=color;frame.alpha[py*frame.width+px]=1}
    }
  }
  return frame
}
export function richTextWarnings(w: Widget, content: RichText, mode:PageMode='dynamic') {
  if(mode!=='dynamic')return []
  const missing=new Set<string>()
  for(const r of content.runs) {const font=r.fontId||(r.style?.fontFamily?undefined:w.style.fontId);if(font) missingDeviceGlyphs(font,resolveLabText(w,r.text)).forEach(c=>missing.add(c))}
  return [...missing]
}
