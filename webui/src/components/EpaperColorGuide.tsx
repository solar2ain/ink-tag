import { useEffect, useRef, useState } from 'react'
import { loadOptionalAsset } from '../data/optionalAssets'
import { optimizeCanvas, type ImageAlgorithm } from '../image/optimize'
import { useLanguage } from '../preferences'

const samples: {zh:string;en:string;color:number;algorithm:ImageAlgorithm|null}[] = [
  {zh:'原始图片',en:'Original image',color:1,algorithm:null},
  {zh:'黑白 · 直接量化',en:'Black/white · quantization',color:1,algorithm:'none'},
  {zh:'黑白 · Atkinson',en:'Black/white · Atkinson',color:1,algorithm:'atkinson'},
  {zh:'黑白红 · Atkinson',en:'Black/white/red · Atkinson',color:2,algorithm:'atkinson'},
  {zh:'黑白黄 · Atkinson',en:'Black/white/yellow · Atkinson',color:5,algorithm:'atkinson'},
  {zh:'黑白红黄 · Atkinson',en:'Black/white/red/yellow · Atkinson',color:3,algorithm:'atkinson'},
  {zh:'四级灰阶 · Atkinson',en:'Four gray levels · Atkinson',color:4,algorithm:'atkinson'},
  {zh:'六色 · Atkinson',en:'Six colors · Atkinson',color:10,algorithm:'atkinson'},
  {zh:'七色 · Atkinson',en:'Seven colors · Atkinson',color:11,algorithm:'atkinson'},
]
let examples: Promise<HTMLCanvasElement[]> | undefined
function colorExamples() {
  return examples ??= (async()=>{
    const response=await fetch(await loadOptionalAsset('colorPhoto')), image=await createImageBitmap(await response.blob())
    try {
      const source=document.createElement('canvas');source.width=320;source.height=Math.round(320*image.height/image.width)
      source.getContext('2d')!.drawImage(image,0,0,source.width,source.height)
      return await Promise.all(samples.map(async sample=>{
        if(!sample.algorithm)return source
        const preview=document.createElement('canvas'),device=document.createElement('canvas')
        await optimizeCanvas(source,preview,device,{algorithm:sample.algorithm,preset:'balanced',contrast:1.1,heavyInk:false,strength:.8,color:sample.color})
        return device
      }))
    } finally {image.close()}
  })().catch(error=>{examples=undefined;throw error})
}
export function EpaperColorGuide() {
  const en=useLanguage()==='en', host=useRef<HTMLDivElement>(null),[status,setStatus]=useState('loading')
  useEffect(()=>{
    let cancelled=false
    void colorExamples().then(canvases=>{
      if(cancelled)return
      host.current?.querySelectorAll('canvas').forEach((target,i)=>{target.width=canvases[i].width;target.height=canvases[i].height;target.getContext('2d')!.drawImage(canvases[i],0,0)})
      setStatus('ready')
    }).catch(()=>{if(!cancelled)setStatus('error')})
    return()=>{cancelled=true}
  },[])
  return <div className="epaper-color-guide">
    <table><thead><tr><th>{en?'Screen type':'屏幕类型'}</th><th>{en?'Available inks':'实际可用墨色'}</th></tr></thead><tbody>
      <tr><td>{en?'Black/white':'黑白屏'}</td><td>{en?'Black and white':'黑、白'}</td></tr>
      <tr><td>{en?'Three / four colors':'三色 / 四色屏'}</td><td>{en?'For example black/white/red, or black/white/red/yellow':'例如黑白红，或黑白红黄；以具体屏幕为准'}</td></tr>
      <tr><td>{en?'Six / seven colors':'六色 / 七色屏'}</td><td>{en?'A fixed ink palette, not an RGB display':'固定的多色色板，并不是手机那样的 RGB 全彩屏'}</td></tr>
      <tr><td>{en?'Grayscale':'灰阶屏'}</td><td>{en?'Only the gray levels supported by the selected profile':'只能使用选定屏幕与驱动支持的灰阶'}</td></tr>
    </tbody></table>
    <h3>{en?'How dithering represents a photo':'照片为什么需要抖动处理？'}</h3>
    <p>{en?'Quantization maps each pixel to an available ink and can lose gradients. Dithering distributes ink pixels to suggest intermediate tones from a distance; it does not create new ink colors. Close up, a dotted texture is normal.':'直接量化把每个像素映射到一种可用墨色，容易丢失渐变。抖动算法通过分布相邻的墨色像素，在正常距离模拟明暗和颜色层次，并不会增加屏幕真正支持的颜色；近看有颗粒是正常现象。'}</p>
    <p className="field-note">{en?'Photo examples use Atkinson, strength 0.8 and contrast 1.1 to reduce background speckling. These settings suit this image; compare other photos in the editor.':'照片示例使用 Atkinson，强度 0.8、对比度 1.1，减少浅色背景的杂点。这组参数适合本图；其他图片可在编辑器中比较选择。'}</p>
    {status!=='ready'&&<p role="status">{en?(status==='error'?'Optional example image is unavailable. Other tools remain usable.':'Generating examples with the real image-processing pipeline…'):(status==='error'?'可选示例图片不可用，不影响其他功能。':'正在使用实际图片处理算法生成示例…')}</p>}
    <div className="color-example-grid" ref={host}>{samples.map(sample=><figure key={sample.zh}><canvas role="img" aria-label={en?sample.en:sample.zh}/><figcaption>{en?sample.en:sample.zh}</figcaption></figure>)}</div>
    <p className="field-note">{en?'AI-generated red tulip example. Compare the red petals across palettes. These are algorithm previews, not photos of panels; real ink, paper color and lighting affect the result.':'示例素材：AI 生成的红色郁金香。可对比不同色板对红色花瓣的表现。以上为算法预览，不是屏幕实拍；实际墨色、纸色和环境光会影响观感。'}</p>
    <h3>{en?'Prepare an image':'图片怎么准备？'}</h3>
    <ol>
      <li>{en?'Select the exact screen profile first. Its visible pixel dimensions and palette determine the output. DPI metadata does not add pixels.':'先选对屏幕 Profile，按可视区的像素尺寸和色板制作；图片的 DPI 数值不会增加设备像素。'}</li>
      <li>{en?'Use fit to keep the whole photo, or cover to crop without stretching. In the canvas, “Fill canvas” extends an image to every edge.':'“完整显示”保留整张图，“铺满裁切”保持比例但裁掉边缘；画板中的“铺满画板”可以覆盖到四边。'}</li>
      <li>{en?'The default remains Hybrid Adaptive. Compare Stucki or Burkes for photos, Atkinson for a lighter texture, or Bayer for a regular pattern. Judge at actual pixel size; no algorithm suits every photo.':'默认保持 Hybrid 自适应；照片可对比 Stucki、Burkes，较轻的颗粒可试 Atkinson，规则纹理可试 Bayer。请按原始像素大小比较，没有适合所有照片的算法。'}</li>
      <li>{en?'Keep small text and fine lines as separate text/shape controls. Avoid repeatedly dithering a previously dithered image.':'小字、细线尽量使用独立文字或图形控件，避免和照片一起抖动；不要对已经抖动的图片反复处理。'}</li>
    </ol>
    <p>{en?'Canvas image frames accept PNG/JPEG/WebP up to 20 MiB and resize large sources locally. HEIC, animated images and SVG are not accepted. More source pixels cannot exceed the screen’s detail or color limits.':'画板图片框支持 PNG / JPEG / WebP，单文件最多 20 MiB，大图在本机自动缩小；暂不接受 HEIC、动图和 SVG。更大的原图不能突破屏幕的像素和颜色限制。'}</p>
  </div>
}
