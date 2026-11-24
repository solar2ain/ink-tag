import type { Document, Widget } from '../../../../packages/inktag-editor-core/src/index'
import { optimizeCanvas } from '../../image/optimize'

export async function readCanvasImage(file: File): Promise<string> {
  if (file.size > 20 * 1024 * 1024) throw new Error('图片超过 20 MiB，请先缩小文件。')
  if (!['image/png','image/jpeg','image/webp'].includes(file.type) && !(file.type === '' && /\.(png|jpe?g|webp)$/i.test(file.name)))
    throw new Error('请选择 PNG、JPEG 或 WebP 图片；暂不支持 HEIC、SVG 或动图。')
  let bitmap: ImageBitmap
  try { bitmap = await createImageBitmap(file) } catch { throw new Error('图片无法解码，请重新导出为 PNG、JPEG 或 WebP 后再试。') }
  try {
    if (!bitmap.width || !bitmap.height || bitmap.width * bitmap.height > 40_000_000) throw new Error('图片像素过大，请先缩小至 4000 万像素以内。')
    const scale = Math.min(1,2560/Math.max(bitmap.width,bitmap.height)), canvas=document.createElement('canvas')
    canvas.width=Math.max(1,Math.round(bitmap.width*scale));canvas.height=Math.max(1,Math.round(bitmap.height*scale))
    const context=canvas.getContext('2d')!
    context.fillStyle='#ffffff';context.fillRect(0,0,canvas.width,canvas.height);context.drawImage(bitmap,0,0,canvas.width,canvas.height)
    const png=canvas.toDataURL('image/png')
    return png.length <= 2*1024*1024 ? png : canvas.toDataURL('image/jpeg',.9)
  } finally { bitmap.close() }
}

/** Derived previews never mutate the source or add undo entries. Export awaits the exact same pixels. */
export class CanvasImageCache {
  private entries = new Map<string,{url?: string; pending: Promise<void>; error?: Error}>()
  private disposed = false
  constructor(private changed: () => void = () => {}, private failed: (error: Error) => void = () => {}) {}
  private key(w: Widget, doc: Document) { return JSON.stringify([w.image,w.width,w.height,w.imageFit,doc.colorMode,w.imageDither,w.imageAdjustments]) }
  image(w: Widget, doc: Document) {
    if (!w.image || !w.imageDither || w.imageDither === 'source') return w.image
    const key=this.key(w,doc)
    if(!this.entries.has(key)) {
      const item: {url?:string;pending:Promise<void>;error?:Error}={pending:Promise.resolve()}
      this.entries.set(key,item)
      item.pending=this.process({...w,imageAdjustments:w.imageAdjustments?{...w.imageAdjustments}:undefined},{...doc}).then(url=>{item.url=url;if(!this.disposed)this.changed()})
        .catch(error=>{item.error=error instanceof Error?error:new Error('图片抖动失败');if(!this.disposed)this.failed(item.error)})
      if(this.entries.size>doc.widgets.length+1) {
        const active=new Set(doc.widgets.filter(w=>w.image).map(w=>this.key(w,doc)))
        for(const old of this.entries.keys())if(!active.has(old))this.entries.delete(old)
      }
    }
    return this.entries.get(key)?.url ?? w.image
  }
  private async process(w: Widget, doc: Document) {
    const response=await fetch(w.image!), bitmap=await createImageBitmap(await response.blob())
    try {
      const source=document.createElement('canvas');source.width=Math.max(1,Math.round(w.width));source.height=Math.max(1,Math.round(w.height))
      const context=source.getContext('2d')!, scale=(w.imageFit==='cover'?Math.max:Math.min)(source.width/bitmap.width,source.height/bitmap.height)
      context.fillStyle='#ffffff';context.fillRect(0,0,source.width,source.height)
      context.drawImage(bitmap,(source.width-bitmap.width*scale)/2,(source.height-bitmap.height*scale)/2,bitmap.width*scale,bitmap.height*scale)
      const preview=document.createElement('canvas'),device=document.createElement('canvas')
      const adjustments=w.imageAdjustments??{contrast:1,strength:1,heavyInk:false}
      await optimizeCanvas(source,preview,device,{algorithm:w.imageDither==='source'?'none':w.imageDither||'auto',preset:'balanced',...adjustments,color:doc.colorMode??2,strength:w.imageDither==='none'?Math.min(1,adjustments.strength):adjustments.strength})
      return device.toDataURL('image/png')
    } finally {bitmap.close()}
  }
  async ready(doc: Document) {
    for(const w of doc.widgets) if(w.image && w.imageDither && w.imageDither!=='source') {
      this.image(w,doc)
      const item=this.entries.get(this.key(w,doc))!
      await item.pending
      if(item.error)throw item.error
    }
  }
  dispose(){this.disposed=true;this.entries.clear()}
}
